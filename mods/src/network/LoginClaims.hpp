#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::claims {

    struct Header {
        std::string alg = "ES384";
        std::string x5u;
    };

    struct Algorithm {
        std::string alg = "ES384";
    };

    struct Multiplayer {
        std::string cpk;
        std::string xid = "0";
        std::string xname;
        std::string identity;
        std::string leguuid;
        std::string mid;
        std::string iss = "self";
        std::string aud = "api://auth-minecraft-services/multiplayer";
        int64_t iat = 0;
        int64_t nbf = 0;
        int64_t exp = 0;
    };

    struct Fingerprint {
        std::string algorithm;
        std::string digest;
    };

    struct Fingerprints {
        std::vector<Fingerprint> fingerprint;
    };

    struct Assertion {
        std::string fingerprints;
        std::string token;
    };

    struct IdentityProvider {
        std::string domain;
        std::string protocol = "default";
    };

    struct Envelope {
        std::string assertion;
        IdentityProvider idp;
    };

    struct Chain {
        std::vector<std::string> chain{""};
    };

    struct ChainLink {
        int64_t exp = 0;
        int64_t nbf = 0;
        std::string identityPublicKey;
        bool certificateAuthority = true;
    };

    struct ExtraData {
        std::string displayName;
        std::string identity;
        std::string XUID;
    };

    struct SelfSignedLink {
        int64_t exp = 0;
        int64_t nbf = 0;
        std::string identityPublicKey;
        ExtraData extraData;
    };

    struct Identity {
        std::string Certificate;
        int AuthenticationType = 2;
        std::string Token;
    };

    struct SkinAnimation {
        double Frames = 0;
        std::string Image;
        int ImageHeight = 0;
        int ImageWidth = 0;
        int Type = 0;
        int AnimationExpression = 0;
    };

    struct PersonaPiece {
        bool IsDefault = false;
        std::string PackId;
        std::string PieceId;
        std::string PieceType;
        std::string ProductId;
    };

    struct PersonaPieceTintColor {
        std::vector<std::string> Colors;
        std::string PieceType;
    };

    struct ClientData {
        std::vector<SkinAnimation> AnimatedImageData;
        std::string ArmSize = "wide";
        std::string CapeData;
        std::string CapeId;
        bool CapeOnClassicSkin = false;
        int CapeImageHeight = 0;
        int CapeImageWidth = 0;
        int ClientEditorConnectionIntent = 0;
        bool ClientIsEditorCapable = false;
        int64_t ClientRandomId = 0;
        bool CompatibleWithClientSideChunkGen = false;
        int CurrentInputMode = 1;
        int DefaultInputMode = 1;
        std::string DeviceId;
        std::string DeviceModel = "Windows";
        int DeviceOS = 12;
        bool FilterProfanity = false;
        std::string GameVersion;
        int GraphicsMode = 1;
        int GuiScale = 0;
        std::string LanguageCode = "en_US";
        int MaxViewDistance = 32;
        int MemoryTier = 4;
        bool OverrideSkin = false;
        std::vector<PersonaPiece> PersonaPieces;
        bool PersonaSkin = false;
        std::vector<PersonaPieceTintColor> PieceTintColors;
        std::string PlatformOfflineId;
        std::string PlatformOnlineId;
        int PlatformType = 0;
        std::string PlayFabId;
        bool PremiumSkin = false;
        std::string SelfSignedId;
        std::string ServerAddress;
        std::string SkinAnimationData;
        std::string SkinColor = "#b37b62";
        std::string SkinData;
        std::string SkinGeometryData;
        std::string SkinGeometryDataEngineVersion;
        std::string SkinId = "Standard_Steve";
        int SkinImageHeight = 64;
        int SkinImageWidth = 64;
        std::string SkinResourcePatch = "{}";
        std::string ThirdPartyName;
        bool ThirdPartyNameOnly = false;
        bool TrustedSkin = false;
        int UIProfile = 0;
        std::string PartyId;
        bool IsPartyLeader = false;
        std::string ProfileHash;
        std::string Nonce;
    };

}
