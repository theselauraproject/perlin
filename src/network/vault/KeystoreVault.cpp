#include "network/SecretVault.hpp"

#ifdef __ANDROID__
#include "network/AtomicFile.hpp"
#include "util/FileIo.hpp"

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <filesystem>
#endif

namespace perlin::auth::vault {
#ifdef __ANDROID__
    namespace {
        constexpr const char* kAlias = "perlin_auth_wrap_v1";

        struct Local {
            JNIEnv* env;
            jobject ref;
            Local(JNIEnv* e, jobject r) : env(e), ref(r) {}
            ~Local() {
                if (ref)
                    env->DeleteLocalRef(ref);
            }
            Local(const Local&) = delete;
            Local& operator=(const Local&) = delete;
            explicit operator bool() const { return ref != nullptr; }
        };

        bool Failed(JNIEnv* env) {
            if (!env->ExceptionCheck())
                return false;
            env->ExceptionClear();
            return true;
        }

        class KeystoreVault final : public SecretVault {
        public:
            const char* Name() const override { return "Android Keystore"; }
            bool Available() override { return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv()) != nullptr; }

            Result Load(const std::string& id, std::string& secret) override {
                std::string blob;
                if (!file::Read(id + ".key", blob))
                    return Result::Missing;
                if (Unwrap(blob, secret))
                    return Result::Ok;
                if (auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv())) {
                    Local key(env, Key(env, false));
                    if (!key) {
                        Erase(id);
                        return Result::Missing;
                    }
                }
                return Result::Unavailable;
            }

            bool Store(const std::string& id, const std::string& secret) override {
                std::string blob;
                return Wrap(secret, blob) && WriteFileAtomic(id + ".key", blob);
            }

            void Erase(const std::string& id) override {
                std::error_code ec;
                std::filesystem::remove(id + ".key", ec);
            }

        private:
            static jobject Key(JNIEnv* env, bool create) {
                jclass ksClass = env->FindClass("java/security/KeyStore");
                if (!ksClass || Failed(env))
                    return nullptr;
                jmethodID getInstance =
                    env->GetStaticMethodID(ksClass, "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;");
                jstring type = env->NewStringUTF("AndroidKeyStore");
                Local ks(env, env->CallStaticObjectMethod(ksClass, getInstance, type));
                env->DeleteLocalRef(type);
                if (!ks || Failed(env))
                    return nullptr;
                jmethodID load = env->GetMethodID(ksClass, "load", "(Ljava/security/KeyStore$LoadStoreParameter;)V");
                env->CallVoidMethod(ks.ref, load, static_cast<jobject>(nullptr));
                if (Failed(env))
                    return nullptr;
                jstring alias = env->NewStringUTF(kAlias);
                jmethodID getKey = env->GetMethodID(ksClass, "getKey", "(Ljava/lang/String;[C)Ljava/security/Key;");
                jobject key = env->CallObjectMethod(ks.ref, getKey, alias, static_cast<jobject>(nullptr));
                if (Failed(env))
                    key = nullptr;
                if (key || !create) {
                    env->DeleteLocalRef(alias);
                    return key;
                }
                jclass kgClass = env->FindClass("javax/crypto/KeyGenerator");
                jmethodID kgInstance = env->GetStaticMethodID(
                    kgClass, "getInstance", "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/KeyGenerator;");
                jstring aes = env->NewStringUTF("AES");
                jstring provider = env->NewStringUTF("AndroidKeyStore");
                Local generator(env, env->CallStaticObjectMethod(kgClass, kgInstance, aes, provider));
                jclass builderClass = env->FindClass("android/security/keystore/KeyGenParameterSpec$Builder");
                jmethodID builderInit = env->GetMethodID(builderClass, "<init>", "(Ljava/lang/String;I)V");
                Local builder(env, env->NewObject(builderClass, builderInit, alias, 3));
                jclass stringClass = env->FindClass("java/lang/String");
                jobjectArray modes = env->NewObjectArray(1, stringClass, env->NewStringUTF("GCM"));
                jobjectArray paddings = env->NewObjectArray(1, stringClass, env->NewStringUTF("NoPadding"));
                const char* builderSig = "Landroid/security/keystore/KeyGenParameterSpec$Builder;";
                jmethodID setModes = env->GetMethodID(builderClass, "setBlockModes",
                                                      (std::string("([Ljava/lang/String;)") + builderSig).c_str());
                jmethodID setPaddings = env->GetMethodID(builderClass, "setEncryptionPaddings",
                                                         (std::string("([Ljava/lang/String;)") + builderSig).c_str());
                jmethodID setSize = env->GetMethodID(builderClass, "setKeySize",
                                                     (std::string("(I)") + builderSig).c_str());
                jmethodID build = env->GetMethodID(builderClass, "build",
                                                   "()Landroid/security/keystore/KeyGenParameterSpec;");
                if (Failed(env) || !generator || !builder)
                    return nullptr;
                env->DeleteLocalRef(env->CallObjectMethod(builder.ref, setModes, modes));
                env->DeleteLocalRef(env->CallObjectMethod(builder.ref, setPaddings, paddings));
                env->DeleteLocalRef(env->CallObjectMethod(builder.ref, setSize, 256));
                Local spec(env, env->CallObjectMethod(builder.ref, build));
                jmethodID init = env->GetMethodID(kgClass, "init", "(Ljava/security/spec/AlgorithmParameterSpec;)V");
                env->CallVoidMethod(generator.ref, init, spec.ref);
                jmethodID generate = env->GetMethodID(kgClass, "generateKey", "()Ljavax/crypto/SecretKey;");
                jobject made = env->CallObjectMethod(generator.ref, generate);
                if (Failed(env))
                    return nullptr;
                return made;
            }

            static bool Wrap(const std::string& plain, std::string& blob) {
                auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
                if (!env)
                    return false;
                Local key(env, Key(env, true));
                if (!key)
                    return false;
                jclass cipherClass = env->FindClass("javax/crypto/Cipher");
                jmethodID getInstance =
                    env->GetStaticMethodID(cipherClass, "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;");
                jstring transformation = env->NewStringUTF("AES/GCM/NoPadding");
                Local cipher(env, env->CallStaticObjectMethod(cipherClass, getInstance, transformation));
                env->DeleteLocalRef(transformation);
                jmethodID init = env->GetMethodID(cipherClass, "init", "(ILjava/security/Key;)V");
                env->CallVoidMethod(cipher.ref, init, 1, key.ref);
                jmethodID getIv = env->GetMethodID(cipherClass, "getIV", "()[B");
                jmethodID doFinal = env->GetMethodID(cipherClass, "doFinal", "([B)[B");
                if (Failed(env))
                    return false;
                jbyteArray input = env->NewByteArray(static_cast<jsize>(plain.size()));
                env->SetByteArrayRegion(input, 0, static_cast<jsize>(plain.size()),
                                        reinterpret_cast<const jbyte*>(plain.data()));
                Local ivArray(env, env->CallObjectMethod(cipher.ref, getIv));
                Local output(env, env->CallObjectMethod(cipher.ref, doFinal, input));
                env->DeleteLocalRef(input);
                if (Failed(env) || !ivArray || !output)
                    return false;
                const jsize ivSize = env->GetArrayLength(static_cast<jbyteArray>(ivArray.ref));
                const jsize outSize = env->GetArrayLength(static_cast<jbyteArray>(output.ref));
                blob.assign(1, static_cast<char>(ivSize));
                blob.resize(1 + static_cast<size_t>(ivSize) + static_cast<size_t>(outSize));
                env->GetByteArrayRegion(static_cast<jbyteArray>(ivArray.ref), 0, ivSize,
                                        reinterpret_cast<jbyte*>(blob.data() + 1));
                env->GetByteArrayRegion(static_cast<jbyteArray>(output.ref), 0, outSize,
                                        reinterpret_cast<jbyte*>(blob.data() + 1 + ivSize));
                return !Failed(env);
            }

            static bool Unwrap(const std::string& blob, std::string& plain) {
                auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
                if (!env || blob.size() < 2)
                    return false;
                const size_t ivSize = static_cast<unsigned char>(blob[0]);
                if (blob.size() < 1 + ivSize)
                    return false;
                Local key(env, Key(env, false));
                if (!key)
                    return false;
                jclass cipherClass = env->FindClass("javax/crypto/Cipher");
                jmethodID getInstance =
                    env->GetStaticMethodID(cipherClass, "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;");
                jstring transformation = env->NewStringUTF("AES/GCM/NoPadding");
                Local cipher(env, env->CallStaticObjectMethod(cipherClass, getInstance, transformation));
                env->DeleteLocalRef(transformation);
                jclass specClass = env->FindClass("javax/crypto/spec/GCMParameterSpec");
                jmethodID specInit = env->GetMethodID(specClass, "<init>", "(I[B)V");
                jbyteArray iv = env->NewByteArray(static_cast<jsize>(ivSize));
                env->SetByteArrayRegion(iv, 0, static_cast<jsize>(ivSize),
                                        reinterpret_cast<const jbyte*>(blob.data() + 1));
                Local spec(env, env->NewObject(specClass, specInit, 128, iv));
                env->DeleteLocalRef(iv);
                jmethodID init = env->GetMethodID(cipherClass, "init",
                                                  "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V");
                env->CallVoidMethod(cipher.ref, init, 2, key.ref, spec.ref);
                jmethodID doFinal = env->GetMethodID(cipherClass, "doFinal", "([B)[B");
                if (Failed(env))
                    return false;
                const size_t bodySize = blob.size() - 1 - ivSize;
                jbyteArray body = env->NewByteArray(static_cast<jsize>(bodySize));
                env->SetByteArrayRegion(body, 0, static_cast<jsize>(bodySize),
                                        reinterpret_cast<const jbyte*>(blob.data() + 1 + ivSize));
                Local output(env, env->CallObjectMethod(cipher.ref, doFinal, body));
                env->DeleteLocalRef(body);
                if (Failed(env) || !output)
                    return false;
                const jsize outSize = env->GetArrayLength(static_cast<jbyteArray>(output.ref));
                plain.resize(static_cast<size_t>(outSize));
                env->GetByteArrayRegion(static_cast<jbyteArray>(output.ref), 0, outSize,
                                        reinterpret_cast<jbyte*>(plain.data()));
                return !Failed(env);
            }
        };
    }

    std::shared_ptr<SecretVault> MakeKeystore() {
        return std::make_shared<KeystoreVault>();
    }
#else
    std::shared_ptr<SecretVault> MakeKeystore() {
        return nullptr;
    }
#endif
}
