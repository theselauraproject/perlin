#pragma once
#include "client/systems/replay/ExportConfig.hpp"
#include "client/systems/replay/Ffmpeg.hpp"

#include <string>
#include <string_view>

namespace perlin::replay {
    inline constexpr int kMinExportSide = 16;
    inline constexpr int kMaxExportSide = 16384;

    struct ExportDerived {
        const ffmpeg::Container* container = nullptr;
        const ffmpeg::Codec* codec = nullptr;
        bool custom = false;
        bool imageSequence = false;
        double rangeStartMs = 0;
        double rangeEndMs = 0;
        int resolutionPreset = 0;
        int fpsPreset = -1;
        const AspectRatio* aspect = nullptr;
    };

    bool IsExportTextField(std::string_view id);
    std::string ExportFieldText(const ExportConfig& c, std::string_view id);
    void CommitExportField(ExportConfig& c, std::string_view id, const std::string& text);

    ExportDerived DeriveExportForm(ExportConfig& c, double durationMs, double inMs, double outMs);

    void ApplyAspectToWidth(ExportConfig& c, const AspectRatio& aspect);
    void ApplyAspectToHeight(ExportConfig& c, const AspectRatio& aspect);

    bool StepExportOption(ExportConfig& c, std::string_view option, int direction, bool coarse,
                          const ExportDerived& derived);

    bool SplitCycleId(std::string_view id, std::string& base, int& direction);
}
