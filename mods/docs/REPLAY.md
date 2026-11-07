# Perlin replays

* **Record**: pause menu -> *Start Replay* / *Stop Replay* (side by side; the inactive one is locked). Recording can start mid-game: the world, entities and players are reconstructed from a state cache. Files go to `<data dir>/replays/*.perlinreplay` (a zip: `metadata.json`, `chunk_N.bin`, optional `thumbnail.png`, `editor.json`). Drop a `.perlinreplay` on the window to import it.
* **Play**: title screen -> *Replays*. Esc toggles the control bar. Keys: Space play/pause, Left/Right +-5 s (Shift +-1 s), `,` `.` speed, C camera mode (free / recorded player / keyframe path), I/O in/out points, M marker, K camera keyframe (Shift+K removes), F1 hides the bar.
* **Keyframes** (bar -> Keyframes): camera path (position, rotation, roll, FOV), playback speed (timelapse/slow motion), time of day (with freeze). Smooth, linear, hold and ease interpolation. Stored in `editor.json` inside the replay.
* **Export** (bar -> Export): any resolution up to 16384, any frame rate (e.g. `23.976`, `30000/1001`, `144`), 1-4x supersampling, range (whole / in-out / custom), hide GUI, file name template, output folder, screenshot, queue. Video goes through ffmpeg (`<data dir>/ffmpeg/ffmpeg(.exe)` or PATH; Windows can download it from the Advanced tab). Formats: MP4, MKV, WebM, MOV, AVI, MXF, TS, FLV, OGV, GIF, WebP, APNG and PNG/JPEG/BMP/TIFF/EXR/WebP sequences, plus *Custom* where any ffmpeg encoder/muxer/extension/extra arguments can be typed in. PNG sequences work without ffmpeg.

## Built-in encoders (no ffmpeg program)
Perlin links FFmpeg's libraries (libavcodec / libavformat / libswscale) statically, so video export does not use or need any `ffmpeg.exe`. The Windows CI build installs them with vcpkg (`ffmpeg[...x264,x265,vpx,aom,webp,nvcodec,amf,qsv]:x64-windows-static`, which makes the binary GPL because of x264/x265) and passes `-DFFMPEG_ROOT=...`; Linux/macOS pick them up via pkg-config. The export dialog lists exactly the encoders and muxers that were compiled in, and "Custom" accepts any of them by name plus `-option value` extras.

If a build has no FFmpeg libraries (`-DPERLIN_LIBAV=OFF` or they were not found) Perlin falls back to an `ffmpeg` executable next to it / in `<data dir>/ffmpeg` / on PATH, and if that is missing too to its own MJPEG `.avi` encoder. PNG sequences and screenshots never need FFmpeg.

## Smooth rewinding, skins and server packs

- **Rewinding never rebuilds terrain.** While a replay plays, the world keeps a per-second undo log (up to 768 MB). Seeking backwards rolls the loaded chunks back to that second and keeps every chunk already meshed; chunks that only exist later in the replay are simply kept. Only if the undo log no longer reaches that far (very long replays) or the dimension changed does Perlin fall back to rebuilding the world from the nearest snapshot.
- **Seeking forwards** just applies the packets in between faster; there is no restart.
- **Recorded player**: drawn standing on the ground (recorded positions are eye height) with movement and rotation interpolated between samples, wearing the skin from their PlayerList entry (updated by skin-change packets).
- **Server resource packs** in use when recording are stored inside the `.perlinreplay` (`packs/` + `packs.json`, including the pack content key) and are mounted when the replay opens, so it looks the same on any computer. Replays recorded before this change have no stored packs.
- **Mouse**: stays free in the replay; hold the right button or press Tab to capture it, Esc/Tab to release.

## Export speed and progress

- Frames are captured without waiting: the renderer keeps drawing the next frame while finished frames are scaled and encoded on a separate thread (memory is capped, so a slow encoder just throttles the renderer).
- VSync is switched off while exporting, and only the first frame waits for the world to be fully drawn; later frames wait one step.
- The default x264/x265 preset is now `veryfast` (change it under Advanced > Encoder preset).
- Progress is shown in its own small window ("Frame 12/300", a progress bar, percent, speed, time left, and a Cancel button; Esc also cancels) and in the main window's title. Nothing is drawn into the frames being captured, so there is no flicker and nothing can leak into the video. Platforms without that window fall back to an on-screen panel.
- Replay camera: WASD/Space/Shift fly with smooth acceleration, arrow keys turn, scroll changes speed, hold the right mouse button (or press Tab) to look with the mouse. The game HUD and HUD mods are hidden during replays.

## Editor

Open it with **Editor** on the replay bar. It has a menu bar (close, export, options, camera mode, HUD, marker), an inspector on the right and a timeline dock at the bottom:

- Transport (|< << Play >> >|), a zoomable ruler, and tracks for **Camera**, **Speed**, **Time of day** and **Markers**.
- Click or drag on the ruler or an empty track area to scrub - the replay follows live (rewinding is seamless). Wheel zooms the timeline, Shift+wheel pans, drag the scrollbar, or press F / **Fit** to show everything.
- Click a key to select it (the replay jumps there); drag it to retime (snaps to 10 ms, hold Shift for free). The inspector edits its time, easing, FOV, roll, speed value or time of day, and can set a camera key from the current view.
- K adds a camera key at the playhead, Del removes the selected key, I/O set in/out points, M adds a marker, Space plays/pauses, J/L seek 5 s.
- Visuals: hide the HUD, hide players, entities or nametags (these apply to exports too).
- Replays always use first person. In **Cam: Player** mode you see through the recorded player's eyes including their held item/hand.
- Dragging the bar's progress slider now rewinds/fast-forwards the replay while you drag.
- The multiplayer connect screen shows a yellow stability/anticheat notice above Cancel.

## Client-side actions (swings, mining, hotbar)

Besides the player's position, recordings now store the serverbound packets that show what the player *did*: arm swings (Animate), block actions inside PlayerAuthInput and PlayerAction (start / crack / continue / predict / stop / abort) and hotbar changes (MobEquipment). During playback they are decoded and acted out: the first-person hand swings, break cracks grow on the block being mined, hit particles and hit/break sounds play, and the selected hotbar slot follows the recording. Older replays only contain the position packets, so they show none of this. Seeking discards these events until the target time is reached.

Limits: mining speed is recomputed from the held item (haste, enchantments and effects are not known), and place/use animations rely on the recorded swing packets.

## Offscreen export

While exporting, the game is rendered into an offscreen target (the frame-generation target is reused) and that target is what gets captured. The window shows a copy of it with the progress panel drawn on top in its own last view, so the panel stays on screen the whole time and never appears in the video. Set `PERLIN_EXPORT_DIRECT=1` to go back to capturing the window directly (the panel then hides on captured frames).
