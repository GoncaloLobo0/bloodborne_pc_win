# bbport on Windows

The Windows build of bbport: Bloodborne's own x86-64 code running natively, the same renderer,
launcher and scripts as on Linux, on Windows 10/11 x86-64.

> **Status: playable from the start; not played through.** With Bloodborne 1.09 on Windows 10
> and an AMD RX 6650 XT, the intro, character creation, Iosefka's Clinic and Central Yharnam run
> at a steady 60 FPS with a controller or the keyboard, and 30-minute sessions run without a
> crash. Later areas, NVIDIA/Intel GPUs and Windows 11 are untested. Please report problems
> with the log (see [Reporting problems](#reporting-problems)).

**What you download is source code only** — no game files, no keys, no executables. You build
the port on your PC and play your own copy of the game. This project is not affiliated with
Sony Interactive Entertainment, FromSoftware or AMD.

## Requirements

- Windows 10 version 1803 or newer (Windows 11 included), x86-64.
- A Vulkan 1.3 GPU with an up-to-date driver (tested: AMD RX 6650 XT, 8 GB).
- **Your own dump of Bloodborne CUSA03173, updated to 1.09** (the base game alone, 1.00, does
  not start). Either a dumped game folder or the backup packages (`.pkg`) a PS4 dump tool writes.
- [Git for Windows](https://git-scm.com/download/win) to download the repository.
- Disk space: ~5 GB for the build environment, ~31 GB for the game.

## Install

From a Command Prompt or PowerShell:

```bat
git clone https://github.com/GoncaloLobo0/bloodborne_pc_win.git
cd bloodborne_pc_win
windows\setup.cmd
windows\build.cmd
```

- `windows\setup.cmd` (once) downloads a private MSYS2 (CLANG64) with the compiler and libraries
  into `.toolchain\` and builds the few libraries MSYS2 does not ship. Nothing is installed
  system-wide; everything stays inside the repository folder.
- `windows\build.cmd` builds `out\bb-probe.exe`. Run it again after every `git pull`.
  `windows\build.cmd --test` also runs the test suites.

Use `git clone`: a "Download ZIP" copy has no Git metadata, and setup cannot fetch the
submodules (FSR, ImGui, LibAtrac9) without it.

## Your game

The game goes in `roms\` (not tracked by Git, so it is never uploaded):

**A dumped game folder** (`CUSA03173` with `eboot.bin`, `sce_module`, `sce_sys`, `dvdroot_ps4`):
copy it to `roms\CUSA03173`, then copy the dumped 1.09 update folder over it, replacing files.

**Backup packages (`.pkg`)**, as some dump tools write them (a fake-signed game package and the
1.09 update package): put both in `roms\` and run

```bat
windows\install-pkg.cmd roms\<game>.pkg roms\<update-1.09>.pkg
```

It unpacks the game to `roms\CUSA03173`, copies the update over it and checks the result. The
extractor (shadPS4's, `tools/pkg_extract`) uses the public fake-package keys: retail-encrypted
packages from the PlayStation Store cannot be unpacked — dump the game installed on the console
instead. `out\pkg-extract.exe --list <pkg> <folder>` lists what a package contains.

Either way, the start-up check tells you if the folder is not a 1.09 game.

## Play

```bat
windows\play.cmd roms\CUSA03173
```

or `windows\launcher.cmd` for the settings GUI (game folder, resolution, upscaler, effects,
controls, then *Play*). A console window shows the log while the game runs.

**Desktop shortcut:** right-click the desktop → *New → Shortcut*, location
`cmd /c "cd /d <repository folder> && windows\play.cmd roms\CUSA03173"`.

- **Saves:** `user\savedata\` (back them up). The shader cache is in `user\cache\`.
- **Settings:** `bbport.ini` in the repository folder, written by the in-game menu and the
  launcher. Delete it to get the defaults back.
- The first minutes in a new area may hitch while shaders compile; they are cached for later.

## Controls

**Controller:** any controller SDL3 knows (Xbox, DualShock 4, DualSense, ...). Xbox layout: A
Cross, B Circle, X Square, Y Triangle, View/Select the touchpad. With several connected, choose
one in the launcher (*Controls → Controller*).

**Keyboard and mouse** (used when no controller is connected — unplug or turn it off):

| Action (PS4 button) | Key / mouse |
|---|---|
| Move | W A S D |
| Camera | mouse (or arrow keys) |
| R1 — attack | left mouse button or 3 |
| L1 — left hand (gun) | right mouse button or 1 |
| R3 — lock on | middle mouse button or C |
| R2 / L2 | F / R |
| Cross — dodge, confirm | Space |
| Circle — sprint, back | Left Shift |
| Square — use item | E |
| Triangle — transform, interact | Q |
| L3 | Z |
| D-pad (items, weapons) | I up, K down, J left, L right |
| Options — menu | Enter |
| Touchpad — gestures | Tab (left side), Backspace (right side) |
| Free the mouse cursor | Esc (a click in the window captures it again) |

Keys can be remapped in the launcher or in `bbport.ini` (`key.r1=3`, `pad.cross=a`, ...).
The character's name is typed on the keyboard in a box over the game.

## In-game settings menu

**Insert** (or **L3 + R3**) opens the settings overlay; changes are saved to `bbport.ini`.

- **Upscaler:** *FSR 3.1* (default, smooth edges), *TAA*, or *Off* (sharpest in motion, jagged
  edges). FSR 4 needs extra assets (`windows\msys.cmd bash tools/fetch_fsr4_assets.sh`) and a GPU
  that supports it.
- **Sharpening / Sharpness:** raise it if FSR 3.1 looks soft.
- **Camera motion: y up** — keep it on (see [Fixes](#windows-port-fixes)).
- **Mouse look / Mouse sensitivity.** The mouse turns the camera directly, one to one like a PC
  game (a hook in the game's camera update, Bloodborne 1.09; `BB_MOUSE_HOOK=0` falls back to
  emulating the right stick).
- **Game effects** (apply after a restart): chromatic aberration, depth of field, motion blur
  (off by default: it draws a mirrored copy of the scene over the sky), SSAO, the game's own
  anti-aliasing, dynamic light shadows, model detail.
- **FPS counter.**

## Discord

When Discord is running, your profile shows **Playing Bloodborne** with the cover and the session
time. `set BB_DISCORD=0` before starting turns it off.

## Troubleshooting

- **"No eboot.bin" / the game check fails:** the folder must be `CUSA03173` itself (with
  `eboot.bin` in it) and updated to 1.09.
- **The game closes and the console shows "bb-probe exited with status N":** keep the console
  text (or the launcher's log) for the report.
- **Out of GPU memory:** close other games and GPU-heavy programs (browsers with many tabs,
  streaming); the game uses up to ~5 GB of VRAM.
- **Stutter:** expected while shaders compile the first time in an area. Persistent stutter:
  report it with the log.
- **No keyboard input:** a controller is connected (the keyboard only drives the game without
  one); the game window must have focus.

## Reporting problems

Start the game from the launcher with *Save the log and statistics to a file*, or keep the
console output of `windows\play.cmd`, and include: what you did, your GPU and driver version,
and the log.

## Windows port fixes

Problems found and fixed while bringing the game up on Windows; most apply to Linux too.

- **Device lost after the intro movie (AMD Windows driver):** the object motion shaders loaded
  from a constant 64-bit address that the driver compiled as a 32-bit one; the address now goes
  through a select the compiler cannot fold. Their addresses are part of the shader cache key.
- **Black character preview in character creation:** the texture cache compared two different
  hashes of an image's memory, so a CPU write anywhere on its page reloaded it from memory over
  what the GPU had written (the preview's exposure stayed 0).
- **FSR smearing and blur while the camera moves:** 0.3's camera motion vectors had the vertical
  direction reversed (measured: 2.6× the reprojection error while the camera pitches; upstream
  issues #42, #47). The 0.2 convention is back, as the *Camera motion: y up* setting.
- **Mirrored scene and streaks in the sky:** the game's motion blur misbehaves on sky pixels in
  this port (upstream issue #44); it is off by default.
- **Texture memory:** the collector's pressure mark was 40% of the VRAM budget below 16 GB,
  so it evicted textures constantly; it now follows the driver's live budget.
- **Malformed GPU command buffers** (rare, during area loads) no longer stop the game: decoding
  resumes at the next valid packet.
- Also: the AvPlayer thread stop race (intro movie freeze/crash), audio pacing on Windows,
  the English overlay menu, mouse look, Discord Rich Presence, PKG extraction.

## What differs from Linux

- **Memory model:** the default model (VRAM copies with write tracking) only. The experimental
  PC memory model (`BB_GUEST_IN_PLACE=1`, *New memory model*) needs Linux dma-buf; it turns
  itself off on Windows.
- **Not available:** `BB_UFFD` (userfaultfd), MangoHud (use another overlay), PGO builds, and
  the Linux-only diagnostics `BB_FREE_CHECK`, `BB_LABEL_TRAP`, `BB_SAMPLE_THREAD`,
  `BB_HEAP_SITES`; the AppImage is Linux only.
- **Present modes:** AMD's Windows driver has no Mailbox; it falls back to FIFO (vsync).
  Choose *Immediate* in the launcher for an uncapped frame rate without vsync.
- **Mods:** the merged game view uses symbolic links when Windows allows them (Developer Mode),
  otherwise directory junctions and hard links (copies when the mod is on another drive).
- **Restart** (in-game menu, resolution changes) ends the game with status 75 and `run.sh`
  starts it again, as Windows has no `exec`.
- **Red zone:** SysV code (the game) may keep data in the 128 bytes below the stack pointer;
  Linux leaves them alone when it delivers a signal, Windows writes its exception frame there.
  GPU write tracking delivers such exceptions in game code. shadPS4 reports crashes from this on
  Windows only with Intel 12th-generation and newer CPUs and has an opt-in static patcher for it
  (not ported here yet). If the game crashes at random on such a CPU, this is a suspect.
- **Diagnostics added on Windows:** VK_EXT_device_fault reports (faulting GPU address on device
  loss), `BB_LOG_BDA`, `BB_SCREENSHOT_TRIGGER`, `BB_DUMP_IMAGE`, `BB_WATCH_ADDR` (hardware write
  breakpoints on game threads), `BB_DEBUG_MOTION=1` with `BB_TOGGLE_FILE` for motion vector
  checks.

## How the port works

The port keeps bbport's design (the game's x86-64 code runs directly, a runtime replaces the PS4
system libraries, a shadPS4-derived renderer); what changed is the host underneath.

| | Linux | Windows |
|---|---|---|
| Guest thread pointer | the linker turns `mov rax, fs:[0]` into `gs:[0]`; GS base = guest TCB (`arch_prctl`) | GS is the TEB and cannot be moved: the TCB is kept in a TEB TLS slot and the loader rewrites the instruction to `mov rax, gs:[0x1480 + 8*slot]` (same length) |
| Guest memory | one `memfd`, mapped with `mmap(MAP_FIXED)`, holes punched on release | one pagefile-backed section (committed as the game allocates); the PS4 range is reserved as placeholders at start and mappings are views placed into them (`VirtualAlloc2`, `MapViewOfFile3`); a view cut by a partial unmap is mapped again in pieces with its page protections, and threads touching it meanwhile wait (`src/runtime_memory_win32.c`) |
| Faults | `SIGSEGV` handler | vectored exception handler; the GPU library's handlers get a Linux-layout `ucontext` built from the `CONTEXT` (`src/compat/win32/ucontext.h`) |
| Files | POSIX descriptors | Win32 handles with the position kept by the runtime (UTF-8 long paths, positional reads, directories, replacing rename) |
| Host pointers below 1 TiB | `mallopt`, low mappings | the executable is built without ASLR, so the heap and thread stacks stay at low addresses |
| Stacks | low mappings | system stacks committed in full (game code does not probe its stack page by page) |
| GPU library | `libbbgpu.so` | linked statically into `bb-probe.exe`; Win32 Vulkan surface |
| Scripts | bash, Python | the same, under MSYS2 bash (`run.sh` handles Windows paths, CRLF output and restarts) |

Windows-specific sources: `src/runtime_memory_win32.c`, `src/compat/win32/` (POSIX headers the
Linux code uses), `src/bb_platform.h`, `windows/`, the `_WIN32` parts of `src/probe.c`,
`src/runtime_*.c` and `gpu/`. Tests that cover them: `tests/test_memory.c` (random mappings,
protections and concurrent view splits against a model), `tests/test_file_mods.c`,
`tests/test_pad.c`, `tests/test_runtime.c`, and the guest thread pointer test in
`tests/test_probe.py`.

`windows\msys.cmd` opens a shell in the build environment (or runs one command in it), for the
scripts in `tools\`.
