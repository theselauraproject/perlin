#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "client/replay/ReplayFormat.hpp"

namespace perlin::replay {
    enum class CameraMode { Free = 0, Follow = 1, Keyframes = 2 };

    class ReplayController {
    public:
        using Tap = std::function<void(int dir, uint32_t id, const std::vector<uint8_t>& body)>;
        Tap NewSessionTap();
        void DetachSession();
        struct LiveInfo {
            std::string server, gameVersion, playerName;
            int protocol = 0;
            uint64_t runtimeId = 0;
            Pose pose;
            struct Pack {
                std::string path, fileName, uuid, key;
            };
            std::vector<Pack> packs;
            uint32_t snapshotMs = 120000;
        };
        bool StartRecording(const LiveInfo& info, const std::string& perlinVersion, std::string& error);
        bool StopRecording(std::string& outPath, std::string& error);
        bool Recording() const { return recorder.Active(); }
        uint32_t RecordingMs() const { return recorder.ElapsedMs(); }
        uint64_t RecordingBytes() const { return recorder.BytesWritten(); }
        void AddMarker(const std::string& name) { recorder.AddMarker(name); }
        static bool RecoverInterrupted(std::string& recoveredPath);

        bool Open(const std::string& path, std::string& error);
        void Close();
        bool Playing() const { return reader != nullptr; }
        const Metadata& Meta() const { return meta; }
        const std::string& Path() const { return path; }
        void Rewind();
        void SetClientSink(std::function<void(const Record&)> sink) { clientSink = std::move(sink); }
        bool Advance(double dtMs, const std::function<void(const Record&)>& inject, double budgetMs);
        void Seek(double ms);
        bool SeekPending() const { return seekTarget >= 0; }
        double SeekTargetMs() const { return seekTarget >= 0 ? seekTarget : timeMs; }
        bool NeedsRestart() const { return restartRequested; }
        void ClearRestart() { restartRequested = false; }
        double TimeMs() const { return timeMs; }
        uint32_t DurationMs() const { return meta.durationMs; }
        double Speed() const { return speed; }
        void SetSpeed(double s) { speed = s < 0.01 ? 0.01 : (s > 64 ? 64 : s); }
        bool Paused() const { return paused; }
        void SetPaused(bool p) { paused = p; }
        bool AtEnd() const { return ended; }
        struct StoredPack {
            std::string file, uuid, key;
        };
        std::vector<StoredPack> StoredPacks();
        Pose RecordedPose() const { return recorded; }
        Pose InterpolatedPose() const;
        bool CatchingUp() const { return seekTarget >= 0; }
        const std::vector<Marker>& Markers() const { return meta.markers; }
        void AddPlaybackMarker(const std::string& name, uint32_t atMs);
        bool SaveMarkers(std::string& error);
        bool SaveArchiveFile(const std::string& name, const std::string& content, std::string& error);
        std::string ReadArchiveFile(const std::string& name) {
            return reader ? reader->ReadFile(name) : std::string();
        }
        Reader* GetReader() { return reader.get(); }

        CameraMode cameraMode = CameraMode::Free;

    private:
        std::function<void(const Record&)> clientSink;
        void LoadChunkFor(size_t index);
        Recorder recorder;
        std::vector<LiveInfo::Pack> recordingPacks;
        StateCache cache;
        std::atomic<uint64_t> generation{0};
        std::string workDir;

        std::unique_ptr<Reader> reader;
        Metadata meta;
        std::string path;
        std::vector<Record> chunkRecords;
        size_t chunkIndex = 0, recordIndex = 0;
        bool chunkLoaded = false;
        double timeMs = 0, speed = 1.0, seekTarget = -1.0;
        bool paused = false, ended = false, restartRequested = false, useSnapshot = false;
        Pose recorded;
        double recordedAtMs = 0;
    };
}
