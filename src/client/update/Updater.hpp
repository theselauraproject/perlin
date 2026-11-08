#pragma once

#include <string>

namespace perlin::update {
    enum class State {
        Disabled,
        Checking,
        UpToDate,
        Downloading,
        Ready,
        Available,
        Failed,
    };

    struct Status {
        State state = State::Disabled;
        float progress = 0;
        std::string latest;
        std::string message;
    };

    bool Supported();

    std::string BuildLabel();

    void OnStartup();

    void Start(bool enabled);
    Status Current();

    bool Act();

    void OnExit();
}
