#include "client/systems/multiplayer/SessionPolicy.hpp"
#include "network/Transport.hpp"

#include <cstdio>
#include <string>

using namespace perlin::session;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what, int line) {
        if (!ok) {
            std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
            failures++;
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    struct Ref {
        enum class S { Waiting, InGame, Other, Failed };
        S state = S::Other;
        bool cacheStatusSent = false;
        bool spawnSeen = false;
        std::string lastError;
        int outdated = -1;
        int cacheSends = 0;
    };

    Ref MakeRef(Ref::S state, bool sent) {
        Ref r;
        r.state = state;
        r.cacheStatusSent = sent;
        return r;
    }

    void ReferenceHandler(Ref& r, PlayStatusKind status, int code) {
        if ((status == PlayStatusKind::FailedClient || status == PlayStatusKind::FailedSpawn) &&
            r.state != Ref::S::InGame) {
            const bool clientOld = status == PlayStatusKind::FailedClient;
            r.lastError =
                clientOld ? "Perlin is outdated for this server" : "the server is outdated for Perlin";
            r.outdated = clientOld ? 1 : 2;
            r.state = Ref::S::Failed;
            return;
        }
        if (r.state == Ref::S::Waiting) {
            if (status != PlayStatusKind::LoginSuccess) {
                r.lastError = "login rejected, PlayStatus=" + std::to_string(code);
                r.state = Ref::S::Failed;
            } else if (!r.cacheStatusSent) {
                r.cacheSends++;
                r.cacheStatusSent = true;
            }
        } else if (status == PlayStatusKind::LoginSuccess && !r.cacheStatusSent) {
            r.cacheSends++;
            r.cacheStatusSent = true;
        } else if (status == PlayStatusKind::PlayerSpawn) {
            r.spawnSeen = true;
            r.state = Ref::S::InGame;
        }
    }

    void Apply(Ref& r, const PlayStatusDecision& d) {
        using A = PlayStatusDecision::Action;
        switch (d.action) {
        case A::Fail:
            r.lastError = d.error;
            if (d.outdated != OutdatedSide::None)
                r.outdated = d.outdated == OutdatedSide::Client ? 1 : 2;
            r.state = Ref::S::Failed;
            break;
        case A::SendCacheStatus:
            r.cacheSends++;
            r.cacheStatusSent = true;
            break;
        case A::EnterGame:
            r.spawnSeen = true;
            r.state = Ref::S::InGame;
            break;
        case A::Ignore:
            break;
        }
    }

    Phase PhaseOf(Ref::S s) {
        return s == Ref::S::Waiting  ? Phase::WaitingPlayStatus
               : s == Ref::S::InGame ? Phase::InGame
                                     : Phase::Other;
    }
}

int main() {
    using ProtocolSource = LoginProtocolSelection::Source;
    const int latest = perlin::kBedrockProtocol;
    for (const int previous : {944, 2193, latest}) {
        for (const int placeholder : {0, 121, 943, latest + 1}) {
            const auto transferred = SelectLoginProtocol(latest, placeholder, previous, true);
            CHECK(transferred.protocol == previous);
            CHECK(transferred.source == ProtocolSource::Transfer);
            const auto fresh = SelectLoginProtocol(latest, placeholder, previous, false);
            CHECK(fresh.protocol == latest);
            CHECK(fresh.source == ProtocolSource::Transport);
        }
        for (const int supported : {944, 2193}) {
            const auto selected = SelectLoginProtocol(latest, supported, previous, true);
            CHECK(selected.protocol == supported);
            CHECK(selected.source == ProtocolSource::Advertisement);
        }
        const auto explicitLatest = SelectLoginProtocol(latest, latest, previous, true);
        CHECK(explicitLatest.protocol == latest);
        CHECK(explicitLatest.source == ProtocolSource::Transport);
        const auto overridden = SelectLoginProtocol(latest, 121, previous, true, 2193);
        CHECK(overridden.protocol == 2193);
        CHECK(overridden.source == ProtocolSource::Override);
    }
    for (const int invalidPrevious : {0, 121, 943, latest + 1}) {
        const auto selected = SelectLoginProtocol(latest, 121, invalidPrevious, true);
        CHECK(selected.protocol == latest);
        CHECK(selected.source == ProtocolSource::Transport);
    }

    for (const uint16_t algorithm : {0, 1}) {
        for (const size_t size : {0, 1, 255, 256, 4096})
            CHECK(static_cast<uint8_t>(SelectBatchCompression(algorithm, 0, size)) == 0xFF);
        CHECK(static_cast<uint8_t>(SelectBatchCompression(algorithm, 256, 255)) == 0xFF);
        CHECK(static_cast<uint8_t>(SelectBatchCompression(algorithm, 256, 256)) == algorithm);
        CHECK(static_cast<uint8_t>(SelectBatchCompression(algorithm, 256, 257)) == algorithm);
        CHECK(static_cast<uint8_t>(SelectBatchCompression(algorithm, 1, 1)) == algorithm);
    }
    for (const uint16_t threshold : {0, 1, 256})
        CHECK(static_cast<uint8_t>(SelectBatchCompression(0xFFFF, threshold, 4096)) == 0xFF);

    const PlayStatusKind kinds[] = {PlayStatusKind::LoginSuccess, PlayStatusKind::FailedClient,
                                    PlayStatusKind::FailedSpawn, PlayStatusKind::PlayerSpawn,
                                    PlayStatusKind::Other};
    const Ref::S states[] = {Ref::S::Waiting, Ref::S::InGame, Ref::S::Other, Ref::S::Failed};
    for (PlayStatusKind k : kinds)
        for (Ref::S st : states)
            for (bool sent : {false, true})
                for (int code : {0, 3, 7}) {
                    Ref a = MakeRef(st, sent), b = MakeRef(st, sent);
                    ReferenceHandler(a, k, code);
                    Apply(b, DecidePlayStatus(k, code, PhaseOf(st), sent));
                    CHECK(a.state == b.state);
                    CHECK(a.cacheStatusSent == b.cacheStatusSent);
                    CHECK(a.spawnSeen == b.spawnSeen);
                    CHECK(a.lastError == b.lastError);
                    CHECK(a.outdated == b.outdated);
                    CHECK(a.cacheSends == b.cacheSends);
                }

    CHECK(DescribeDisconnect(0, "") == "server disconnect, reason=0");
    CHECK(DescribeDisconnect(7, "bye") == "server disconnect, reason=7 message='bye'");
    CHECK(DescribeDisconnect(44, "").ends_with("--name)"));
    CHECK(DescribeDisconnect(44, "x").starts_with(
        "server disconnect, reason=44 message='x' (ServerIdConflict"));
    CHECK(DescribeDisconnect(35, "") ==
          DescribeDisconnect(8, "").replace(DescribeDisconnect(8, "").find("reason=8"), 8, "reason=35"));
    CHECK(DescribeDisconnect(8, "").ends_with("(version mismatch with Perlin's 1.26.60)"));
    CHECK(DescribeDisconnect(46, "").ends_with("(the server wants an Xbox login: use --login)"));
    CHECK(DescribeDisconnect(45, "").find('(') == std::string::npos);

    if (failures) {
        std::fprintf(stderr, "session_policy_selftest FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("session_policy_selftest passed");
    return 0;
}
