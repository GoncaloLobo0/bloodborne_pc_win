# bbport on Windows

The Windows build of bbport: the same game code running natively on the CPU, the same renderer,
launcher and scripts, on Windows 10/11 x86-64 instead of Linux.

> **Status: playable from the start; not yet played through.** With Bloodborne 1.09 (RX 6650 XT,
> Windows 10 22H2) the intro movie, character creation and the first area (Iosefka's Clinic) run
> at a steady 60 FPS with walking and the camera (audio output checked by dumping it); the
> runtime, memory, file, pad and
> Python test suites pass. Later areas have not been tested yet: please report problems with the
> log (launcher: *Save the log and statistics to a file*).

## Requirements

- Windows 10 version 1803 or newer (Windows 11 included), x86-64.
- A Vulkan 1.3 GPU and driver (the same as on Linux; see the main README).
- Your own decrypted dump of Bloodborne CUSA03173, updated to 1.09.
- About 5 GB of disk for the build environment, which lives in `.toolchain\` inside this folder.

## Set up, build, play

From a Command Prompt or PowerShell in this folder:

```bat
windows\setup.cmd            :: once: a private MSYS2 (CLANG64) with the compiler and libraries
windows\build.cmd            :: builds out\bb-probe.exe   (windows\build.cmd --test also runs the tests)
windows\launcher.cmd         :: the launcher: choose the game folder, settings, Start
```

or without the launcher:

```bat
windows\play.cmd D:\Games\CUSA03173
```

### Game from backup packages (.pkg)

Some PS4 dump tools write fake-signed backup packages instead of a folder. They are unpacked
into the game folder with the update copied over it, and checked:

```bat
windows\install-pkg.cmd roms\game.pkg roms\update-1.09.pkg
```

The game ends up in `roms\CUSA03173` (a third argument chooses another destination; `roms\` is
not tracked by Git). The extractor is shadPS4's (v0.7.0, `tools/pkg_extract`); it uses the
public fake-package keys, so retail-encrypted packages from the PlayStation Store cannot be
unpacked with it: dump the game installed on the console instead.

`windows\setup.cmd` downloads MSYS2 into `.toolchain\msys64` and installs everything there
(clang, CMake, SDL3, Vulkan loader, FFmpeg, GTK 4 for the launcher, …); libraries that MSYS2 does
not ship are built from source into the same place. Nothing is installed system-wide and the
build environment, the launcher's settings, GTK's caches, saves (`user\`) and generated files
(`out\`) all stay inside this folder. (The GPU driver keeps its own shader cache where it always
does, as for any game.)

`windows\msys.cmd` opens a shell in that environment (or runs one command in it), for the
scripts in `tools\`, e.g. the FSR 4 assets:

```bat
windows\msys.cmd bash tools/fetch_fsr4_assets.sh
```

## What differs from Linux

- **Memory model:** the default model (VRAM copies with write tracking) only. The experimental
  PC memory model (`BB_GUEST_IN_PLACE=1`, *New memory model*) needs Linux dma-buf; it turns
  itself off on Windows.
- **Not available:** `BB_UFFD` (userfaultfd), MangoHud (use another overlay), PGO builds, and
  the Linux-only diagnostics `BB_FREE_CHECK`, `BB_LABEL_TRAP`, `BB_SAMPLE_THREAD`,
  `BB_HEAP_SITES`; the AppImage is Linux only.
- **AMD's Windows driver** compiles a load from a constant 64-bit address as a 32-bit one; the
  object motion shaders (motion vectors for the upscaler) build their addresses so that it
  cannot (the first in-game scene lost the GPU device otherwise).
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
