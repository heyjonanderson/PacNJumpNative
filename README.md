# Pac'N-Jump native

A fan port of Pac'N-Jump, Namco's 2011 phone game that has since been pulled from the Android and iOS app stores. It plays like the original, with all 8 stages, the menus, Help pages and achievements, and it runs on Windows, macOS, Linux, Android, Nintendo 3DS, PS Vita, and Sega Dreamcast.

**You bring your own copy of the game.** This project has none of Namco's art, sound or levels in it. The first time you run it, it builds what it needs from the original Android APK file. That happens on your own device or in your own browser, and your APK is never uploaded anywhere.

## Features

**On every platform**
- All 8 stages and 42 level maps
- Stage select, high scores, achievements, Help pages and a pause menu
- Sound effects and music, each with its own on/off switch and volume slider
- Scores and settings are saved
- Tate mode (Windows, macOS, Linux, Vita, and Dreamcast): turn the picture 90 degrees and play with the screen on its side
- It makes no network connections

**Windows, macOS and Linux**
- Keyboard, mouse, touch screen or game controller, with remappable keys.
- Fullscreen, a resizable window that keeps the game's portrait shape, and smooth or sharp whole-number scaling.
- HDR output.
- Three screen rotations for tate, plus a one-button remap so the arrows follow the turned picture.
- Optional on-screen pad and pause button, shown automatically when you use touch or the mouse.

**Android**
- Touch controls with an analog-style slide pad or a digital pad.

**PS Vita**
- Tate mode, with the d-pad turning along with the console.
- The d-pad and both sticks steer, and buttons can be remapped.

**Nintendo 3DS**
- Stereo 3D
- Circle pad, d-pad or touch steering, with an A/B button swap.

**Sega Dreamcast**
- 60 fps, in progressive or interlaced video.
- Scores and settings save to the VMU, in about 1 block.
- Loading screens between stages, and the d-pad or stick steers.

## Get started

1. [Get the original APK](#1-get-the-original-apk).
2. [Pick your platform](#2-pick-your-platform) and follow its steps.
3. Play. You only do the import once.

### 1. Get the original APK

You need the Android version of the game, a file called `com.namcobandaigames.pacnjump_and.apk` (about 20 MB). The iPhone version will not work.

- **If the game is still on an Android phone:** copy the APK off it with any APK extractor app. Or, from a computer with [adb](https://developer.android.com/tools/releases/platform-tools) set up and the phone plugged in, run `adb shell pm path com.namcobandaigames.pacnjump_and`. It prints a path. Then run `adb pull <that path> pacnjump.apk`.
- **If you misplaced your APK,** someone on the INTERNET may have ARCHIVED it.

### 2. Pick your platform

| I want to play on | Download or start here | What you need |
| --- | --- | --- |
| [Android phone or tablet](#android) | The [project page](https://heyjonanderson.github.io/PacNJumpNative/) builds an installable app from your APK | Android 7.0 or newer, 64-bit |
| [Windows](#windows) | [`PacNJump-<version>-Windows-x64.zip`](https://github.com/heyjonanderson/PacNJumpNative/releases/latest) | Windows 10 or newer |
| [Mac](#mac) | [`PacNJump-<version>-macOS-universal.zip`](https://github.com/heyjonanderson/PacNJumpNative/releases/latest) | Intel or Apple silicon |
| [Linux and Steam Deck](#linux-and-steam-deck) | [`PacNJump-<version>-Linux-x64.tar.gz`](https://github.com/heyjonanderson/PacNJumpNative/releases/latest) | 64-bit Linux |
| [Nintendo 3DS](#nintendo-3ds) | [`PacNJump-<version>-3DS.zip`](https://github.com/heyjonanderson/PacNJumpNative/releases/latest) | A 3DS with custom firmware |
| [PS Vita](#ps-vita) | [`PacNJump-<version>-Vita.zip`](https://github.com/heyjonanderson/PacNJumpNative/releases/latest) | A Vita with custom firmware |
| [Sega Dreamcast](#sega-dreamcast) | The [Dreamcast page](https://heyjonanderson.github.io/PacNJumpNative/dreamcast.html) builds a disc image from your APK | Works on an emulator, or an ODE on a real console. Untested on burned disc, but should work! |

The downloads are all on the [latest release](https://github.com/heyjonanderson/PacNJumpNative/releases/latest) page, under Assets.

#### Android

1. On the phone, open the [project page](https://heyjonanderson.github.io/PacNJumpNative/).
2. Tap the box, choose your APK, and wait a short while. When it says Done, tap **Download PacNJump.apk**.
3. Open the downloaded file to install it. Android will ask you to allow installs from your browser or Files app the first time.
4. Open Pac'N-Jump. The game data is already inside it, so there is nothing else to import.

Building again later in the same browser makes an app that installs over the old one as an update.

#### Windows

1. Download `PacNJump-<version>-Windows-x64.zip`, right-click it and choose Extract All.
2. Open the folder and run `pacnjump.exe`. Windows may say it protected your PC because the app is new and unsigned. Click More info, then Run anyway.
3. The game asks for its data. Drag your APK onto the window. A progress bar shows while it imports, and this only happens once.

#### Mac

1. Download `PacNJump-<version>-macOS-universal.zip`, open it, and move `PacNJump.app` to your Applications folder.
2. The first launch is blocked because the app is not signed. See [macOS first launch](#macos-first-launch) for the clicks that get past it.
3. Drag your APK onto the window, or onto the app's icon. It imports once.

#### Linux and Steam Deck

1. Download `PacNJump-<version>-Linux-x64.tar.gz` and unpack it with `tar xzf PacNJump-<version>-Linux-x64.tar.gz`.
2. Run `./pacnjump` from the new folder, then drag your APK onto the window. You can also import from a terminal with `./pacnjump --import path/to/your.apk`.
3. On a Steam Deck, do this in Desktop Mode. The game starts fullscreen on its own. The Steam Deck has not been tested yet.

#### Nintendo 3DS

You need a 3DS running custom firmware (the [3DS Hacks Guide](https://3ds.hacks.guide/) covers it) and a way to install CIA files, such as [FBI](https://github.com/steveice10/FBI). A New 3DS XL is tested. The original 3DS and 2DS have not been tried yet.

1. Unzip `PacNJump-<version>-3DS.zip` (it holds `pacnjump.cia` and a short txt with these steps), copy `pacnjump.cia` to your SD card, and install it with FBI. A Pac'N-Jump icon appears on the Home menu.
2. Copy your APK to `SD:/3ds/pacnjump/` on the SD card. If the folder is not there, make it.
3. Start the game. It finds the APK and imports it, which takes a few minutes the first time. Keep the power on and the SD card in.
4. For sound, the 3DS needs its DSP firmware file at `SD:/3ds/dspfirm.cdc`. Without it the game runs silent. The DSP1 homebrew app makes that file from your own console.

#### PS Vita

You need a Vita running custom firmware (the [Vita Hacks Guide](https://vita.hacks.guide/) covers it) with [VitaShell](https://github.com/TheOfficialFloW/VitaShell) installed.

1. Unzip `PacNJump-<version>-Vita.zip` (it holds `pacnjump.vpk` and a short txt with these steps), copy `pacnjump.vpk` to the Vita (USB or FTP from VitaShell), and install it from VitaShell. A Pac'N-Jump bubble appears on the LiveArea.
2. Start the game once. It creates its data folder and shows where it is looking for your APK.
3. Copy your APK into `ux0:data/jon/pnj/`. The game finds it by itself and imports it, which takes up to a minute the first time.
4. To play with the Vita turned on its side, open Options > Video and set SCREEN ROTATION. Both sticks steer.

#### Sega Dreamcast

1. On a computer, open the [Dreamcast page](https://heyjonanderson.github.io/PacNJumpNative/dreamcast.html), drop your APK on it, and wait a minute or two. Download `PacNJump.cdi` when it finishes.
2. Play it in the [Flycast](https://github.com/flyinghead/flycast) emulator by opening the CDI as a game. On a real console, copy it to your disc emulator (GDEMU, MODE and similar) the way you would any other disc image.
3. Your scores and settings are saved to the VMU. Burned discs and PAL consoles have not been tried yet.

### Something went wrong?

- **"That does not look like the Pac'N-Jump Android APK."** The file is not the original game's APK. Check the name and the size.
- **"This needs the hdpi build of the game."** Your APK is a different edition of the game from the one this project reads. The standard Android release works.
- **The page says the app template could not be loaded.** Reload the page. If it keeps happening, [open an issue](https://github.com/heyjonanderson/PacNJumpNative/issues).
- **3DS or Vita does not start importing.** Check that the APK is in the right folder (above), that the name ends in `.apk`, and that the copy finished.
- **Windows or macOS blocks the app.** That is the missing code signature. The steps above and [Code signing](#code-signing) explain it.
- **Anything else:** [open an issue](https://github.com/heyjonanderson/PacNJumpNative/issues) and say which platform you are on.

### What has been tested

| Platform | Tested on | Not tested yet |
| --- | --- | --- |
| Android | An LG G8 | Other phones, foldables and two-screen phones |
| Windows, macOS, Linux | Windows with HDR, a Mac, a Linux VM | Steam Deck |
| Nintendo 3DS | New 3DS XL (with stereo 3D) and the Azahar emulator | Original 3DS and 2DS |
| PS Vita | A real Vita and Vita3K | |
| Sega Dreamcast | A real console with a MODE, in progressive and interlaced video on CRTs, and Flycast. VMU saves work | Burned discs and PAL consoles |

Reports from other hardware are welcome. Open an [issue](https://github.com/heyjonanderson/PacNJumpNative/issues).

### Known limits

- The analog stick steers like a d-pad for now. True analog control is [planned](#roadmap).
- The Help pages come from the original game, so they describe its phone controls (touch and tilt) on every platform, including the Dreamcast.
- The Windows app is not code-signed and the Mac app is only ad-hoc signed, so both show a warning the first time.
- The 3DS icon is a redraw, not the original game's.

### macOS first launch

The app is not signed, so Gatekeeper blocks it the first time.

- macOS 15 and later: double-click the app and click Done. Open System Settings > Privacy & Security, scroll to Security, click **Open Anyway**, enter your password, click Open. The button only stays for about an hour after you try to open the app.
- macOS 14 and earlier: right-click the app, choose Open, click Open.
- If macOS says the app is damaged: run `xattr -cr PacNJump.app` in Terminal.

## Controls and options

The game is played by steering left and right. On a phone you drag a finger, on the consoles you use the d-pad or stick (the touch screens on the 3DS and Vita work for the menus), and on a computer you use the keyboard, mouse, touch screen or a game controller. The list below is the desktop version in full, and most of it applies elsewhere too.

- Keyboard: arrows or WASD, Enter or Space to select, P to pause, Esc to go back (quit on the title screen), R to restart, 1-8 to pick a stage, F11 or Alt+Enter for fullscreen. Everything except R, 1-8 and F11 can be remapped in Options > Controls > KEYS (two keys and one pad button per action).
- Mouse: click buttons, drag to steer.
- Touch: one finger steers. Two fingers hold left and right like the phone's digital pad. Mac trackpads count as a mouse, not touch.
- Gamepad: d-pad or left stick, A to confirm, B to go back, Start to pause.
- Title and game over screens: any key, button, click or tap continues. The prompt says "TOUCH SCREEN" or "PRESS ANYTHING" depending on the last input you used.
- On-screen pad and pause button: Options > Controls > TOUCH UI cycles AUTO, ON and OFF (everywhere except the Dreamcast). AUTO shows them after a tap or click and hides them once you use a key or pad button.
- Tate: Options > Video > SCREEN ROTATION cycles STANDARD, TATE LEFT and TATE RIGHT (or start with `--orient standard|left|right`). It turns the picture 90 degrees on a normal landscape screen so you can tip the monitor over. TATE LEFT puts the top of the game on the left of your screen, TATE RIGHT on the right. Mouse and touch follow the turn. In Options > Controls > KEYS, MATCH SCREEN remaps the arrows to follow the turned picture and DEFAULTS puts them back. Use DEFAULTS if you rotate the display in your OS instead.
- The window resizes and keeps the 2:3 portrait shape. `--fullscreen` starts fullscreen and `--windowed` forces a window. On a Steam Deck (Steam sets `SteamDeck`) it starts fullscreen by itself.
- Options is split into Sound, Video and Controls. Video has a fullscreen switch (also F11), SCREEN, SCALE and HDR.
- Options > Video > SCALE: SMOOTH fills the window. INT SCALE uses whole-number sizes only, so the dots stay sharp, and leaves black bars. It also works in tate.

---

## For developers

Everything below is about how the project is built. You do not need any of it to play.

### How the web builders work

`web/builder.js` takes the original APK and the app template (`web/template.apk`, the app with no game data in it) and builds one installable APK in the browser:

1. Reads the original APK and builds the pack.
2. Stores the pack as `assets/pnj.pack` in the template and swaps the placeholder launcher icons for the icon from your own APK.
3. Signs the result (APK Signature Scheme v2, RSA 2048, SHA-256) with a key made in the browser and kept in IndexedDB. Building again in the same browser gives an APK that updates the installed one.

v2 signing means Android 7.0 and up. `web/test_builder.mjs` runs the same code in Node. `pip install apksigtool` then `apksigtool verify out.apk` checks the signature independently.

`.github/workflows/pages.yml` builds the template on GitHub, checks it has no game data in it, and publishes `web/` to GitHub Pages on each push to main (Settings > Pages > Source: GitHub Actions). You do not build or commit the template by hand. Any static host works. The page fetches `template.apk` from its own folder.

The power-up jingle is AAC. The builder asks the browser to decode it and falls back to `web/aac.wasm`, the importer's own decoder (`platform/sdl/aac_dec.c`) built with `tools/aac_wasm/build.sh`, for browsers that cannot. The output is the same either way.

#### The Dreamcast builder

`web/dreamcast.html` builds the pack the same way, then `web/dc_pack.js` turns it into a Dreamcast pack (sprite sheets become VQ-compressed textures, with the work spread over `web/dc_worker.js` workers). `web/dc_disc.js` then writes that pack into a ready-made disc image and recomputes the EDC and ECC for every sector it touches, so the result is the same disc `mkdcdisc` would have made.

The ready-made image (`pacnjump-dc.cdi` and its `.json`) comes from the Dreamcast workflow on the `dreamcast` branch. `pages.yml` copies the latest one into `web/dc/` when it deploys. `web/test_dcpack.mjs` and `web/test_dcdisc.mjs` check the JavaScript against the C tool and against `mkdcdisc`.

### Building

The release downloads are built by GitHub Actions (`.github/workflows/`: `desktop.yml`, `n3ds.yml`, `vita.yml`, `dreamcast.yml` and `pages.yml`), so you only need this if you want to build it yourself. The 3DS, Vita and Dreamcast builds use devkitPro, VitaSDK and KallistiOS. The workflow files show the exact toolchain steps.

Desktop:

```
git clone --recursive https://github.com/heyjonanderson/PacNJumpNative.git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                   # build/pacnjump
cmake --build build --target bundle   # macOS: PacNJump.app
```

CMake builds the bundled SDL3 (a submodule, hence `--recursive`) statically unless you pass `-DPNJ_SYSTEM_SDL=ON`. For quick dev builds, `make -f pnj.mk` works too (needs the SDL3 dev package), and `make -f pnj.mk test` runs a headless smoke test.

Import from a terminal with `pacnjump --import game.apk`. `python tools/make_pack.py game.apk` also writes a `pnj.pack`, but the power-up jingle needs ffmpeg on that route. The importer built into the app decodes the jingle itself.

Android needs Android Studio. `android\package.bat byo` builds an app with no game data in it. On first launch it asks you to pick the original APK, so it is the way to import on the phone itself instead of using the project page. `android\package.bat template` writes `web/template.apk` unsigned, which is what the project page builds on (CI does this for the live page). The script finds a JDK (Gradle 8.13 wants 21 or 17) and writes a debug-signed APK to `dist\`. Keep your `%USERPROFILE%\.android\debug.keystore`, or later builds will not install over earlier ones. The build is arm64-v8a only and portrait. Add `armeabi-v7a` in `app/build.gradle` if you need 32-bit.

### Layout

```
core/            portable C99 game core: physics, camera, generator, RNG, draw list, input model
platform/sdl/    SDL3 shell: main.c (render, input), menus.h, audio.h, pack.h (finds and reads pnj.pack),
                 import.h (APK to pack), aac_dec.c (jingle decoder), atlas.h
platform/n3ds/   3DS drawing (citro2d), the touch screen and the exit watchdog
platform/dc/     Dreamcast drawing on the PVR (dc_gfx.h) and the SDL calls it stands in for (dc_sdl.h); the VMU save code is in pack.h
android/         Android Studio project (SDL Java glue, CMake into ../core, file picker activity)
web/             the browser builders: builder.js (APK to pack to signed APK, client side), index.html, dreamcast.html, dc_pack.js, dc_disc.js, aac.wasm
tools/           make_pack.py, dexatlas.py, gen_maps.py, parity scripts, aac_wasm/ (builds web/aac.wasm)
third_party/     SDL (submodule), stb (image and vorbis, public domain)
```

### pnj.pack

Little endian: `"PNJPACK1"`, u32 count, then count entries of `{ u16 name_len, name, u32 offset, u32 size }`, then the data.

- `tex/<name>.png`: the APK's hdpi sheets, unchanged
- `audio/<name>.ogg` and `audio/wakawaka.wav`: sounds and music, plus the decoded power-up jingle
- `maps/<name>.map`: the 42 level maps
- `lang/language.*`: text, including the Help pages
- `meta/atlas.txt`: the 224 sprite rectangles, one `tex x y w h tw` per line
- `meta.json`

The sprite table is not a data file in the APK. It is built in code inside one class in `classes.dex`, so `tools/dexatlas.py`, `import.h` and `web/builder.js` each read it by walking that class's static initializer. Keep the three in sync. `web/test_pack.mjs` builds just the pack so you can diff them.

The app looks for the pack in this order: `--pack`, `$PNJ_PACK`, the program's own folder (Windows and Linux, if it is writable, otherwise the user data folder), then `pnj.pack` in the current directory. On macOS and Android it is always the user data folder. Scores are saved next to the pack. `PNJ_NO_BUNDLE=1` ignores loose files.

### Parity

The core was checked against the original's game logic step for step: same input, same state, over many seeds on all 8 stages. The only differences are last-bit `sin` and `cos` rounding. The scripts are in `tools/` (`trace_c.c`, `compare.py`, `sweep.sh`). They need a reference build that is not in this repo.

`fx` is `double` by default. `-DPNJ_FLOAT` makes it `float` for devices with an FPU and not much else.

To compile the 42 maps into the binary for a personal build, run `python tools/gen_maps.py game.apk`. It writes `core/pnj_maps_data.h` (gitignored) and the next build picks it up. `-DPNJ_BYO` turns that back off.

### What is in the repo

No art, sound, level data, sprite table or Help text. What is in the repo is the game's rules written as C (physics constants, platform and enemy generator tables, scoring) after watching how the original behaves, plus short UI labels and stage names.


## Roadmap

**Now: 0.9.0, then testing.** Android, Windows, macOS, Linux, Nintendo 3DS, PS Vita, and Sega Dreamcast are in the release. The work left is testing on more hardware (the original 3DS and 2DS, Steam Deck, PAL Dreamcasts) and getting to a 1.0. Also open: an icon on the 3DS that matches the Android app's (the repo cannot hold that art, so it would come from the player's own APK or be a close redraw), and PAL support on the Dreamcast, which needs a timing change because the game steps at 60 per second. A RecompLauncher manifest (launching plus in-launcher settings) is already in main and ships with the next release, once the launcher lists the project.

**Later, on the current platforms:** Windows code signing (see below), language select (the pack already carries every language file), tilt input (the original was tilt-controlled), foldable and two-screen Android phones, a 32-bit Android build.

**Planned for every version:** true analog stick control. Today the stick is read as a digital direction (d-pad style) on the platforms that have one, and the on-screen pad has an analog-style mode that is a touch drag. The plan is to feed the stick's real deflection into the core's input model so a gentle push steers gently, on desktop gamepads, the Vita, the 3DS circle pad and anything else with a stick.

**Future platforms.** Nothing here is started. Roughly easiest first.

| Platform | Route | Main work |
| --- | --- | --- |
| Nintendo Switch | devkitA64 and libnx, the SDL shell mostly carries over | Build target, pack on the SD card, pillarboxed portrait field |
| iOS | The SDL shell built in Xcode, import through the Files app | Xcode project, CI build, and how to distribute it (a sideload or TestFlight, since the data has to come from the player's own APK) |
| Nintendo DS | devkitARM and libnds, two 256x192 screens stack to 256x384 | No FPU, so a fixed-point core, and art and audio cut down for 4 MB of RAM |
| Game Boy Advance, Analogue Pocket | devkitARM, the Pocket through its GBA support | An adaptation more than a port: no FPU, tile and palette art, 240x160 landscape screen |
| MS-DOS (486, Pentium) | DJGPP with a VESA and Sound Blaster layer | 256-color art with a palette reserved for glow, software sprites |
| Sega Saturn | libyaul or Jo Engine | Fixed-point core, VDP1 and VDP2 setup, palettised art |
| Atari Jaguar, Jaguar CD | m68k plus Tom and Jerry RISC code | Fixed-point core, 2 MB RAM, hand-written blitter code. The CD version could stream music |
| Virtual Boy | The community V810 toolchain, VUEngine | Research. 64 KB of RAM, four shades of red, audio rewritten for the wavetable channels |

The small and old targets share two prerequisites: a per-target mode in `tools/make_pack.py` that scales, palettizes and re-encodes art and audio, and a float or fixed-point build of the core with a replay test that proves it plays the same run as the double build. There is no route to PS5, since there is no public homebrew SDK.

## Code signing

The Windows exe is built from this repo by GitHub Actions (`.github/workflows/desktop.yml`) and is not signed. Windows SmartScreen or Defender may warn about it the first time you run it. Choose More info, then Run anyway. We would like to get it signed once the project has some traction. The macOS app is ad-hoc signed only, so see the first launch steps above.

Privacy: this program will not transfer any information to other networked systems unless specifically requested by the user.

## License

MIT, for the code in this repo (see LICENSE). It does not cover the original game, which belongs to its owners. Not affiliated with Namco or Bandai Namco.
