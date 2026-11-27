#include "client/systems/multiplayer/SessionPolicy.hpp"
#include "network/Transport.hpp"

namespace perlin::session {

    BatchCompression SelectBatchCompression(uint16_t algorithm, uint16_t threshold, size_t size) {
        if (algorithm == 0xFFFF || threshold == 0 || size < threshold)
            return BatchCompression::None;
        return algorithm == 1 ? BatchCompression::Snappy : BatchCompression::Zlib;
    }

    LoginProtocolSelection SelectLoginProtocol(int transportProtocol, int advertisedProtocol,
                                                int previousProtocol, bool transfer,
                                                int overrideProtocol) {
        using Source = LoginProtocolSelection::Source;
        if (overrideProtocol > 0)
            return {overrideProtocol, Source::Override};
        if (advertisedProtocol >= kOldestBedrockProtocol && advertisedProtocol < kBedrockProtocol)
            return {advertisedProtocol, Source::Advertisement};
        const bool advertisedSupported = advertisedProtocol >= kOldestBedrockProtocol &&
                                         advertisedProtocol <= kBedrockProtocol;
        const bool previousSupported = previousProtocol >= kOldestBedrockProtocol &&
                                       previousProtocol <= kBedrockProtocol;
        // Some transfer destinations advertise a placeholder version. It does not
        // select a new wire format for an already working connection profile.
        if (transfer && !advertisedSupported && previousSupported)
            return {previousProtocol, Source::Transfer};
        return {transportProtocol, Source::Transport};
    }

    PlayStatusDecision DecidePlayStatus(PlayStatusKind status, int statusCode, Phase phase,
                                        bool cacheStatusSent) {
        using Action = PlayStatusDecision::Action;
        PlayStatusDecision d;
        if ((status == PlayStatusKind::FailedClient || status == PlayStatusKind::FailedSpawn) &&
            phase != Phase::InGame) {
            const bool clientOld = status == PlayStatusKind::FailedClient;
            d.action = Action::Fail;
            d.error = clientOld ? "Perlin is outdated for this server" : "the server is outdated for Perlin";
            d.outdated = clientOld ? OutdatedSide::Client : OutdatedSide::Server;
            return d;
        }
        if (phase == Phase::WaitingPlayStatus) {
            if (status != PlayStatusKind::LoginSuccess) {
                d.action = Action::Fail;
                d.error = "login rejected, PlayStatus=" + std::to_string(statusCode);
            } else if (!cacheStatusSent) {
                d.action = Action::SendCacheStatus;
            }
        } else if (status == PlayStatusKind::LoginSuccess && !cacheStatusSent) {
            d.action = Action::SendCacheStatus;
        } else if (status == PlayStatusKind::PlayerSpawn) {
            d.action = Action::EnterGame;
        }
        return d;
    }

    std::string DescribeDisconnect(int reason, const std::string& message) {
        std::string text = "server disconnect, reason=" + std::to_string(reason) +
                           (message.empty() ? "" : " message='" + message + "'");
        switch (reason) {
        case 44:
            text += " (ServerIdConflict: the server still has a session with this name/id from an earlier"
                    " run. Wait ~30 s, restart the server, or join with a different --name)";
            break;
        case 35:
        case 8:
            text += " (version mismatch with Perlin's 1.26.60)";
            break;
        case 46:
            text += " (the server wants an Xbox login: use --login)";
            break;
        default:
            break;
        }
        return text;
    }

}
