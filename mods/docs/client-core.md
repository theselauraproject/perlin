# Client core layout

`Perlin` (src/client/Perlin.hpp) owns the game client: the render thread, every screen, the session and the mod
runtime. Its implementation is split by concern; each file holds one area and nothing else.

| File | What it holds |
| --- | --- |
| `Perlin.cpp` | startup, shutdown, connecting to a server, window title |
| `PerlinFrame.cpp` | the render thread and `FrameStep`, input polling, look, session pump, chat, frame pacing |
| `PerlinInteraction.cpp` | hotbar, aiming, block breaking and placing, debug key combos |
| `PerlinWorldRender.cpp` | eye environment, fog, frame parameters, level/entity/particle/hand rendering, hud and screen dispatch |
| `PerlinScreens.cpp` | small overlay screens: forms, bed, credits, death, server layer, chat links |
| `PerlinInventory.cpp`, `PerlinCreative.cpp` | survival and creative inventories |
| `PerlinPause.cpp` | the pause menu; dispatches to the menus below |
| `PerlinOptions.cpp` | the options menu: an `OptionsModel` builder and one `Build*Options` method per page |
| `PerlinPackMenu.cpp`, `PerlinWardrobe.cpp` | resource/shader/mod lists, skins and capes |
| `PerlinTitle.cpp` | title, asset download, loading and disconnect screens |
| `PerlinServers.cpp` | accounts, sign-in, server list tabs, realms, friends, lan |
| `PerlinPacks.cpp` | texture/shader pack loading and hot reload |
| `PerlinHud.cpp`, `PerlinAudio.cpp` | hud state, debug overlay, sounds and music |
| `PerlinMods*.cpp` | the mod api: `SetupMods` registers it in themed groups |
| `PerlinReplay.cpp`, `PerlinEditor.cpp`, `PerlinExport*.cpp` | replay recording, playback, timeline editor, video export |

## The frame

`FrameStep` reads top to bottom as the frame does. Values one phase computes and a later one reads live in
`FrameContext` (`PerlinFrame.hpp`); state that lasts from the first frame to the last lives in `Perlin::FrameLoop`.
A phase starts by naming the context fields it uses (`auto& dt = f.dt;`), so its body reads like plain locals.

## Screens

Full-screen and overlay screens are registered once, in priority order, with the `ScreenManager`
(`src/client/ScreenManager.hpp`) in `Perlin::RegisterScreens()` (`PerlinScreens.cpp`). Each frame
`screens.Update(frame)` walks them; the first one whose `Update` returns true has taken the frame and the rest are
skipped. A screen is either a `Screen` subclass (`Name`, `Update`, `IsOpen`, `CoversGame`) or a `Callbacks` record
of lambdas for screens that live as `Perlin` members:

```cpp
screens.Add({.name = "title",
             .update = [this](ScreenFrame& f) { return UpdateTitleScreen(f.hud, f.fbW, f.fbH); },
             .isOpen = [this] { return titleScreen.shown; }});
```

`screens.TookFrame("title")`, `IsOpen(name)` and `AnyCoveringOpen()` replace the per-screen flags the frame code
used to test (the crosshair, for example, is hidden while any screen that covers the game is open). A new screen is
one `Add` call; nothing else in the frame needs to change.

Every rml overlay screen keeps an `OverlayState` (`shown`, `leftDown`, `signature`, `openedAt`); a screen with a
user-editable template adds a `ui::TemplateSlot`, which re-reads `<data>/ui/<file>` when it changes on disk.
`detail::OverlayViewport` gives a screen its gui size; `str::Key(...)` builds the signature that decides when the
document is rebuilt.

## Shared helpers

- `util/Strings.hpp`: `str::Lower`, `Trim`, `Split`, `Join`, `Cat`, `Key`, `StripNamespace`, `Parse`
- `util/FileIo.hpp`: `file::Read`, `ReadBytes`, `Write`
- `util/Log.hpp`: `Log("tag", "text {}", value)`, a `std::format` line written in one call
- `client/PerlinInternal.hpp`: helpers private to the `Perlin*.cpp` files (`detail::`)
