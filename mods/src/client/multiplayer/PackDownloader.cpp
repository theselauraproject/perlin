#include "client/multiplayer/PackDownloader.hpp"
#include "client/resources/ServerPacks.hpp"
#include "network/Http.hpp"
#include "util/Strings.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "util/Log.hpp"

namespace perlin {

    namespace {

        std::string BareId(const std::string& id) {
            const size_t us = id.find('_');
            return str::Lower(us == std::string::npos ? id : id.substr(0, us));
        }

        std::string SafeName(std::string s) {
            for (char& c : s)
                if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.'))
                    c = '_';
            return s;
        }

        std::vector<uint8_t> Sha256(const std::vector<uint8_t>& data) {
            std::vector<uint8_t> out(32);
            unsigned int len = 0;
            EVP_Digest(data.data(), data.size(), out.data(), &len, EVP_sha256(), nullptr);
            out.resize(len);
            return out;
        }

        bool SplitUrl(const std::string& url, bool& tls, std::string& host, uint16_t& port,
                      std::string& path) {
            size_t p = 0;
            if (url.starts_with("https://")) {
                tls = true;
                p = 8;
            } else if (url.starts_with("http://")) {
                tls = false;
                p = 7;
            } else {
                return false;
            }
            const size_t slash = url.find('/', p);
            std::string authority = url.substr(p, slash == std::string::npos ? std::string::npos : slash - p);
            path = slash == std::string::npos ? "/" : url.substr(slash);
            port = tls ? 443 : 80;
            const size_t colon = authority.rfind(':');
            if (colon != std::string::npos) {
                port = static_cast<uint16_t>(std::atoi(authority.c_str() + colon + 1));
                authority = authority.substr(0, colon);
            }
            host = authority;
            return !host.empty();
        }
    }

    std::string PackDownloader::UuidString(const net::Uuid& id) {
        char buf[40];
        uint8_t b[16];
        for (int i = 0; i < 8; i++) {
            b[i] = id.bytes[7 - i];
            b[8 + i] = id.bytes[15 - i];
        }
        std::snprintf(
            buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0],
            b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return buf;
    }

    void PackDownloader::Reset() {
        awaiting.clear();
        currentKey.clear();
        queuedKeys.clear();
        fileFor.clear();
        active.clear();
        downloaded = cached = 0;
        totalPacks = donePacks = 0;
        receivedBytes = expectedBytes = 0;
    }

    bool PackDownloader::Have(const std::string& fileName, uint64_t size) const {
        std::error_code ec;
        const auto path = std::filesystem::path(directory) / fileName;
        if (!std::filesystem::is_regular_file(path, ec))
            return false;
        return size == 0 || std::filesystem::file_size(path, ec) == size;
    }

    bool PackDownloader::Store(const std::string& fileName, const std::vector<uint8_t>& bytes) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        const auto path = std::filesystem::path(directory) / fileName;
        const auto tmp = std::filesystem::path(directory) / (fileName + ".part");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!out)
                return false;
        }
        std::filesystem::rename(tmp, path, ec);
        return !ec;
    }

    std::vector<std::string> PackDownloader::Plan(const std::vector<proto::TexturePackInfosEntry>& offered) {
        std::vector<std::string> request;
        offeredPacks.clear();
        totalPacks = offered.size();
        donePacks = 0;
        receivedBytes = 0;
        expectedBytes = 0;
        for (const auto& pack : offered) {
            const std::string uuid = UuidString(pack.uuid);
            const std::string fileName = SafeName(uuid + "_" + pack.version) + ".zip";
            offeredPacks.push_back({fileName, uuid, pack.contentKey});
            if (Have(fileName, pack.size)) {
                cached++;
                donePacks++;
                Log("packs", "{} {} already downloaded", uuid, pack.version);
                continue;
            }
            if (!pack.cdnUrl.empty()) {
                bool tls = true;
                std::string host, path;
                uint16_t port = 443;
                if (SplitUrl(pack.cdnUrl, tls, host, port, path)) {
                    Log("packs", "{} from {}", uuid, pack.cdnUrl);
                    auto resp = http::Request(host, port, tls, "GET", path, {}, {}, {}, 60);
                    if (resp.status >= 200 && resp.status < 300 && !resp.body.empty()) {
                        std::vector<uint8_t> bytes(resp.body.begin(), resp.body.end());
                        if (Store(fileName, bytes)) {
                            downloaded++;
                            donePacks++;
                            Log("packs", "{} downloaded ({} bytes)", uuid, bytes.size());
                            continue;
                        }
                    }
                    Log("packs", "CDN download failed (HTTP {}), asking the server instead", resp.status);
                }
            }
            awaiting.insert(str::Lower(uuid));
            fileFor[str::Lower(uuid)] = fileName;
            request.push_back(uuid + "_" + pack.version);
        }
        return request;
    }

    std::vector<PackDownloader::PackFile> PackDownloader::Files() const {
        std::vector<PackFile> out;
        for (const Offered& o : offeredPacks) {
            const auto path = std::filesystem::path(directory) / o.fileName;
            std::error_code ec;
            if (std::filesystem::exists(path, ec))
                out.push_back({path.string(), o.fileName, o.uuid, o.key});
        }
        return out;
    }

    void PackDownloader::MountAll() {
        const ServerPacks::Batch batch;
        ServerPacks::Instance().Clear();
        for (const Offered& o : offeredPacks) {
            const auto path = std::filesystem::path(directory) / o.fileName;
            std::error_code ec;
            if (std::filesystem::exists(path, ec))
                ServerPacks::Instance().Mount(path.string(), o.uuid, o.key);
        }
    }

    PackDownloader::Result PackDownloader::OnDataInfo(const proto::ResourcePackDataInfoPacket& info) {
        Result r;
        const std::string key = BareId(info.packId);
        auto want = awaiting.find(key);
        if (want == awaiting.end()) {
            r.message = "unrequested pack " + info.packId;
            return r;
        }
        awaiting.erase(want);
        Active a;
        a.packId = info.packId;
        a.chunkCount = info.chunkCount;
        if (info.maxChunkSize > 0) {
            const uint64_t computed = (info.size + info.maxChunkSize - 1) / info.maxChunkSize;
            if (computed <= 0xFFFFFFFFull)
                a.chunkCount = static_cast<uint32_t>(computed);
        }
        a.size = info.size;
        expectedBytes += info.size;
        a.hash = info.hash;
        a.fileName = fileFor.contains(key) ? fileFor[key] : SafeName(key) + ".zip";
        a.data.reserve(static_cast<size_t>(std::min<uint64_t>(info.size, 512ull << 20)));
        Log("packs", "{}: {} bytes in {} chunks", info.packId, info.size, a.chunkCount);
        if (info.size > (512ull << 20)) {
            r.step = Step::Failed;
            r.message = "pack " + info.packId + " is larger than 512 MB";
            return r;
        }
        if (a.chunkCount == 0) {
            r.step = Step::PackDone;
            r.packId = info.packId;
            Store(a.fileName, a.data);
            downloaded++;
            return r;
        }
        auto& slot = active[key] = std::move(a);
        if (currentKey.empty()) {
            currentKey = key;
            requestedAt = std::chrono::steady_clock::now();
            stallReported = false;
            Refill(slot, r);
        } else {
            queuedKeys.push_back(key);
        }
        return r;
    }

    void PackDownloader::Refill(Active& a, Result& r) {
        r.packId = a.packId;
        while (a.requested < a.chunkCount && a.requested - a.next < kWindow)
            r.chunks.push_back(a.requested++);
        if (!r.chunks.empty())
            r.step = Step::RequestChunk;
    }

    PackDownloader::Result PackDownloader::Tick() {
        // a chunk behind later ones that never shows up is asked for again
        if (const auto cur = active.find(currentKey);
            cur != active.end() && !cur->second.early.empty() &&
            std::chrono::steady_clock::now() - cur->second.gapSince > std::chrono::seconds(3)) {
            Active& a = cur->second;
            a.gapSince = std::chrono::steady_clock::now();
            Log("packs", "chunk {} of {} is missing, asking again", a.next, a.packId);
            Result again;
            again.step = Step::RequestChunk;
            again.packId = a.packId;
            again.chunks.push_back(a.next);
            return again;
        }
        if (!currentKey.empty() && !stallReported &&
            std::chrono::steady_clock::now() - requestedAt > std::chrono::seconds(10)) {
            stallReported = true;
            Log("packs", "no chunk for 10 s, still waiting");
        }
        return {};
    }

    PackDownloader::Result PackDownloader::OnChunk(const proto::ResourcePackChunkDataPacket& chunk) {
        Result r;
        const std::string key = BareId(chunk.packId);
        auto it = active.find(key);
        if (it == active.end())
            return r;
        Active& a = it->second;
        // chunks may arrive out of order: hold early ones, drain in order
        if (chunk.chunkIndex < a.next)
            return r;
        if (chunk.chunkIndex != a.next) {
            if (chunk.chunkIndex < a.chunkCount)
                a.held.emplace(chunk.chunkIndex, chunk.payload);
            Refill(a, r);
            return r;
        }
        a.data.insert(a.data.end(), chunk.payload.begin(), chunk.payload.end());
        receivedBytes += chunk.payload.size();
        a.next++;
        while (true) {
            auto held = a.held.find(a.next);
            if (held == a.held.end())
                break;
            a.data.insert(a.data.end(), held->second.begin(), held->second.end());
            receivedBytes += held->second.size();
            a.held.erase(held);
            a.next++;
        }
        requestedAt = std::chrono::steady_clock::now();
        stallReported = false;
        if (a.next < a.chunkCount) {
            Refill(a, r);
            return r;
        }
        if (a.size != 0 && a.data.size() != a.size)
            Log("packs", "warning: {} is {} bytes, the server said {}", a.packId, a.data.size(), a.size);
        if (!a.hash.empty() && Sha256(a.data) != a.hash)
            Log("packs", "warning: {} does not match the SHA-256 the server announced", a.packId);
        if (!Store(a.fileName, a.data)) {
            r.step = Step::Failed;
            r.message = "could not write " + a.fileName + " to " + directory;
            return r;
        }
        Log("packs", "{} saved as {} ({} bytes)", a.packId, a.fileName, a.data.size());
        downloaded++;
        donePacks++;
        r.step = Step::PackDone;
        r.packId = a.packId;
        active.erase(it);
        currentKey.clear();
        if (!queuedKeys.empty()) {
            currentKey = queuedKeys.front();
            queuedKeys.erase(queuedKeys.begin());
            const auto next = active.find(currentKey);
            if (next != active.end()) {
                requestedAt = std::chrono::steady_clock::now();
                Result follow;
                Refill(next->second, follow);
                r.nextPackId = follow.packId;
                r.chunks = std::move(follow.chunks);
            }
        }
        return r;
    }

}
