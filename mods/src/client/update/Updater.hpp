#pragma once
// keeps nightly builds on the newest nightly: the commit of the `nightly` tag on GitHub is compared with the commit
// this build was made from (baked in by CI). What happens when they differ depends on what the platform allows:
//   windows / linux / macos  the package is downloaded and unpacked beside the game in the background, and swapped in
//                            when the game closes (or right away with "restart to update")
//   android                  the apk is downloaded, then the system installer is opened (Android always asks)
//   ios                      the nightly page opens (AltStore / SideStore sources update by themselves)
//   web                      the page reloads past its caches (the site is the nightly)
// Local builds carry no commit and never update.
#include <string>

namespace perlin::update {
    enum class State {
        Disabled, // a local build, or turned off in the options
        Checking,
        UpToDate,
        Downloading,
        Ready,     // staged (desktop) or downloaded (android): Act() applies it
        Available, // newer build exists, Act() opens / reloads it (ios, web)
        Failed,
    };

    struct Status {
        State state = State::Disabled;
        float progress = 0;  // 0..1 while downloading
        std::string latest;  // short commit of the newest nightly
        std::string message; // what to show the player
    };

    // true for builds that know their commit and where nightlies come from
    bool Supported();
    // "nightly 1a2b3c4" or "dev build"
    std::string BuildLabel();

    // process start, before any window: removes what the previous update replaced
    void OnStartup();
    // after the options are loaded: checks (and on desktop downloads) in the background
    void Start(bool enabled);
    Status Current();
    // the player chose to apply it now: true when the game should quit (desktop restarts into the new build)
    bool Act();
    // process exit: installs a staged desktop update; relaunches it when Act() asked for that
    void OnExit();
}
