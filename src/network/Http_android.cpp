// HTTP(S) through Android's own network stack (io.perlin.client.PerlinHttp over JNI): Conscrypt TLS with
// the system and user certificate store, Private DNS, per-app VPNs, proxies and the network security
// config all apply, as they do for every other app. Http.cpp falls back to its OpenSSL client when this
// path cannot complete a request.
#if defined(__ANDROID__)
#include "network/Http.hpp"
#include "util/Log.hpp"

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <cctype>
#include <mutex>
#include <vector>

namespace perlin::http::android {
    namespace {
        struct Bridge {
            jclass cls = nullptr;
            jmethodID request = nullptr;
            jmethodID download = nullptr;
            jclass integer = nullptr;
            jmethodID intValue = nullptr;
        };

        std::mutex gMutex;
        Bridge gBridge;
        bool gReported = false;

        JNIEnv* Env() {
            // Attaches the calling thread to the VM if needed; SDL detaches it when the thread exits.
            return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        }

        bool Failed(JNIEnv* env) {
            if (!env->ExceptionCheck())
                return false;
            env->ExceptionClear();
            return true;
        }

        // Classes of the app are only visible through the activity's class loader: FindClass on a native
        // worker thread searches the system class loader and would not find io.perlin.client.PerlinHttp.
        bool Resolve(JNIEnv* env, Bridge& out) {
            std::lock_guard lock(gMutex);
            if (gBridge.cls) {
                out = gBridge;
                return true;
            }
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!activity)
                return false;
            Bridge b;
            jclass activityClass = env->GetObjectClass(activity);
            jmethodID getLoader = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
            jobject loader = getLoader && !Failed(env) ? env->CallObjectMethod(activity, getLoader) : nullptr;
            if (Failed(env))
                loader = nullptr;
            jclass loaderClass = env->FindClass("java/lang/ClassLoader");
            jmethodID loadClass =
                loaderClass ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")
                            : nullptr;
            jobject cls = nullptr;
            if (loader && loadClass && !Failed(env)) {
                jstring name = env->NewStringUTF("io.perlin.client.PerlinHttp");
                cls = env->CallObjectMethod(loader, loadClass, name);
                if (Failed(env))
                    cls = nullptr;
            }
            jclass integer = env->FindClass("java/lang/Integer");
            if (Failed(env))
                integer = nullptr;
            if (cls && integer) {
                b.cls = static_cast<jclass>(env->NewGlobalRef(cls));
                b.integer = static_cast<jclass>(env->NewGlobalRef(integer));
                b.request = env->GetStaticMethodID(
                    b.cls, "request", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[BI)[Ljava/lang/Object;");
                b.download = env->GetStaticMethodID(
                    b.cls, "download", "(Ljava/lang/String;Ljava/lang/String;Ljava/nio/ByteBuffer;I)Ljava/lang/String;");
                b.intValue = env->GetMethodID(b.integer, "intValue", "()I");
                if (Failed(env) || !b.request || !b.download || !b.intValue) {
                    env->DeleteGlobalRef(b.cls);
                    env->DeleteGlobalRef(b.integer);
                    b = {};
                }
            }
            if (!b.cls) {
                if (!gReported) {
                    gReported = true;
                    Log("http", "the Android HTTPS bridge is unavailable; using OpenSSL");
                }
                return false;
            }
            gBridge = b;
            out = b;
            Log("http", "HTTPS goes through the Android network stack (OpenSSL is the fallback)");
            return true;
        }

        std::string Utf(JNIEnv* env, jstring s) {
            if (!s)
                return {};
            const char* chars = env->GetStringUTFChars(s, nullptr);
            std::string out = chars ? chars : "";
            if (chars)
                env->ReleaseStringUTFChars(s, chars);
            return out;
        }

        // Every local reference of one call lives in its own frame: worker threads stay attached to the VM
        // for their whole life and would otherwise fill the local reference table.
        struct LocalFrame {
            JNIEnv* env;
            bool pushed;
            explicit LocalFrame(JNIEnv* e) : env(e), pushed(e->PushLocalFrame(64) == 0) {}
            ~LocalFrame() {
                if (pushed)
                    env->PopLocalFrame(nullptr);
            }
        };
    }

    bool Available() {
        JNIEnv* env = Env();
        if (!env)
            return false;
        LocalFrame frame(env);
        Bridge b;
        return frame.pushed && Resolve(env, b);
    }

    bool Request(const std::string& url, const std::string& method, const std::map<std::string, std::string>& headers,
                 const std::string& body, int timeoutSecs, Response& out) {
        JNIEnv* env = Env();
        if (!env)
            return false;
        LocalFrame frame(env);
        Bridge b;
        if (!frame.pushed || !Resolve(env, b))
            return false;
        jclass stringClass = env->FindClass("java/lang/String");
        if (!stringClass || Failed(env))
            return false;
        std::vector<std::pair<std::string, std::string>> pairs(headers.begin(), headers.end());
        bool agent = false;
        for (const auto& [k, v] : pairs) {
            std::string lower = k;
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            agent = agent || lower == "user-agent";
        }
        if (!agent)
            pairs.emplace_back("User-Agent", "Perlin");
        jobjectArray headerArray =
            env->NewObjectArray(static_cast<jsize>(pairs.size() * 2), stringClass, nullptr);
        if (!headerArray || Failed(env))
            return false;
        for (size_t i = 0; i < pairs.size(); i++)
            for (int half = 0; half < 2; half++) {
                jstring text = env->NewStringUTF(half ? pairs[i].second.c_str() : pairs[i].first.c_str());
                env->SetObjectArrayElement(headerArray, static_cast<jsize>(i * 2 + half), text);
                env->DeleteLocalRef(text);
            }
        jbyteArray bodyArray = nullptr;
        if (!body.empty()) {
            bodyArray = env->NewByteArray(static_cast<jsize>(body.size()));
            if (!bodyArray || Failed(env))
                return false;
            env->SetByteArrayRegion(bodyArray, 0, static_cast<jsize>(body.size()),
                                    reinterpret_cast<const jbyte*>(body.data()));
        }
        jstring jurl = env->NewStringUTF(url.c_str());
        jstring jmethod = env->NewStringUTF(method.c_str());
        auto* result = static_cast<jobjectArray>(env->CallStaticObjectMethod(
            b.cls, b.request, jurl, jmethod, headerArray, bodyArray, static_cast<jint>(timeoutSecs * 1000)));
        if (Failed(env) || !result || env->GetArrayLength(result) < 4) {
            out = {};
            out.error = "android http bridge threw";
            return true;
        }
        out = {};
        jobject status = env->GetObjectArrayElement(result, 0);
        out.status = status ? env->CallIntMethod(status, b.intValue) : 0;
        if (Failed(env))
            out.status = 0;
        if (auto* bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(result, 1))) {
            const jsize n = env->GetArrayLength(bytes);
            out.body.resize(static_cast<size_t>(n));
            if (n > 0)
                env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(out.body.data()));
        }
        if (auto* names = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 2))) {
            const jsize n = env->GetArrayLength(names);
            for (jsize i = 0; i + 1 < n; i += 2) {
                auto* name = static_cast<jstring>(env->GetObjectArrayElement(names, i));
                auto* value = static_cast<jstring>(env->GetObjectArrayElement(names, i + 1));
                std::string key = Utf(env, name);
                for (char& c : key)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                out.headers.emplace(std::move(key), Utf(env, value));
                env->DeleteLocalRef(name);
                env->DeleteLocalRef(value);
            }
        }
        out.error = Utf(env, static_cast<jstring>(env->GetObjectArrayElement(result, 3)));
        if (Failed(env)) {
            out = {};
            out.error = "android http bridge threw";
        }
        return true;
    }

    bool DownloadFile(const std::string& url, const std::string& path, void* state, size_t stateBytes,
                      int timeoutSecs, std::string& error) {
        JNIEnv* env = Env();
        if (!env) {
            error = "no JNI environment";
            return false;
        }
        LocalFrame frame(env);
        Bridge b;
        if (!frame.pushed || !Resolve(env, b)) {
            error = "the Android HTTPS bridge is unavailable";
            return false;
        }
        jobject buffer = state ? env->NewDirectByteBuffer(state, static_cast<jlong>(stateBytes)) : nullptr;
        if (Failed(env))
            buffer = nullptr;
        jstring jurl = env->NewStringUTF(url.c_str());
        jstring jpath = env->NewStringUTF(path.c_str());
        auto* reason = static_cast<jstring>(
            env->CallStaticObjectMethod(b.cls, b.download, jurl, jpath, buffer, static_cast<jint>(timeoutSecs * 1000)));
        if (Failed(env)) {
            error = "android download bridge threw";
            return false;
        }
        if (reason) {
            error = Utf(env, reason);
            return false;
        }
        return true;
    }
}
#endif
