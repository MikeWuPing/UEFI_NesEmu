# NesEmu — A NES Emulator for UEFI Shell

English | [简体中文](README.md)

This project ports [ObaraEmmanuel/NES](https://github.com/ObaraEmmanuel/NES) — a pure-C NES emulator originally built on SDL3 — to the UEFI Shell environment, packaged as a graphical `.efi` application that you can launch straight from the Shell prompt. The CPU / PPU / MMU / Mapper emulation chain is preserved verbatim from upstream; audio is dropped entirely, rendering goes through UEFI GOP, file IO goes through the UEFI SimpleFileSystem protocol, and input goes through UEFI SimpleTextInput.

## What it is

The intended audience is developers who already tinker with UEFI firmware, OVMF and QEMU. The goal is to wedge a software NES into the empty slot before BIOS setup, so a Shell user can pick a ROM and start playing without any OS underneath. In other words: a bare-metal NES that depends only on EFI Boot Services and EFI Runtime Services.

## Features

- **Full 6502 CPU emulation**: official + unofficial opcodes, cycle accuracy, dummy reads — all carried over from upstream
- **PPU**: NTSC and PAL, 256×240 ARGB output
- **Mapper coverage**: NROM (#0), MMC1 (#1), UxROM (#2), CNROM (#3), MMC3 (#4), AxROM (#7), Color Dreams (#11), GNROM (#66), VRC1 (#75), and more
- **GOP rendering**: auto-selects a 4:3 mode (defaults to 640×480), scales the 256×240 framebuffer up by an integer factor and letterboxes it
- **ROM browser**: on startup, scans `fs0:\ROM\*.nes`, lets you pick a ROM with arrow keys + Enter — no command-line args required
- **No audio**: the APU module is removed entirely, but `0x4000–0x4017` register reads / writes still go through stubs so games that program the APU don't crash
- **TSC + `gBS->Stall` frame timing**: 60 Hz (NTSC) / 50 Hz (PAL)

## Directory layout

```
NesEmu/
├── CLAUDE.md             # Design notes and porting rationale
├── build.ps1             # EDK2 build wrapper (X64 + VS2022 + DEBUG)
├── run.ps1               # QEMU + OVMF launch wrapper
├── README.md             # Chinese README
├── README_EN.md          # English README (this file)
├── ROM/                  # Test ROMs (provide your own .nes files)
├── Refer/                # Upstream reference (read-only, not built)
└── NesEmuPkg/            # EDK2 package
    ├── NesEmuPkg.dsc     # platform description
    ├── NesEmuPkg.dec     # package declaration
    ├── NesEmu.inf        # UEFI application module
    ├── Emu/              # Core emulator ported from Refer/src/ (SDL/APU/NSF stripped)
    │   ├── cpu6502.c/h
    │   ├── ppu.c/h
    │   ├── mmu.c/h
    │   ├── controller.c/h
    │   ├── emulator.c/h
    │   ├── genie.c/h
    │   ├── utils.c/h
    │   └── mappers/
    └── Uefi/             # UEFI adapter layer
        ├── entry.c       # UefiMain entry point
        ├── main.c        # NesEmuMain top-level flow
        ├── rom_browser.c # ROM selection UI
        ├── uefi_gfx.c    # GraphicsContext + GOP rendering
        ├── uefi_timer.c  # TSC + Stall frame timing
        ├── uefi_fs.c     # SimpleFileSystem ROM loader
        ├── uefi_input.c  # keyboard → JoyPad
        ├── font.c        # embedded 5×8 bitmap font (for the browser UI)
        └── nes_compat.h  # libc → EDK2 function-redirect shim
```

## Build & Run

### Prerequisites

- **EDK2**: clone [tianocore/edk2](https://github.com/tianocore/edk2) locally (suggested path `D:\Work\Code\edk2`) and run `edksetup.bat` once to materialize `Conf/` and `BaseTools/`
- **VS2022**: toolchain (`TOOL_CHAIN_TAG = VS2022`)
- **QEMU**: for testing, plus an OVMF firmware image (QEMU ships one at `share\edk2-x86_64-code.fd`)
- **NASM**: required by EDK2 BaseTools

### Build

```powershell
# 1. Set the environment variable (do this in every fresh terminal)
$env:PACKAGES_PATH = "D:\Work\Code\edk2;D:\Work\Code\NesEmu"

# 2. One-shot build
.\build.ps1
```

`build.ps1` invokes `edksetup.bat` to initialize the environment, then runs `build -p NesEmuPkg/NesEmuPkg.dsc -m NesEmuPkg/NesEmu.inf -a X64 -t VS2022 -b DEBUG`. The output lands at `D:\Work\Code\edk2\Build\NesEmuPkg\DEBUG_VS2022\X64\NesEmu.efi` (~50 KB).

### Run (QEMU)

```powershell
# Drop ROMs into ROM/ and then
.\run.ps1
```

`run.ps1` will:
1. Pack `ROM\*.nes` + `NesEmu.efi` + `startup.nsh` into a `esp\` FAT directory
2. Launch `qemu-system-x86_64 -pflash <OVMF.fd> -drive file=fat:rw:esp,format=raw`
3. OVMF boots UEFI Shell, waits 5 seconds, then runs `startup.nsh` to enter `NesEmu.efi`

Headless smoke test (no GUI, captures serial to `serial.log`):

```powershell
$env:NESEMU_HEADLESS_SECONDS = 20
.\run.ps1
```

### Run (real hardware)

Copy `NesEmu.efi` and the `ROM/` directory onto the root of any FAT32 USB stick, then drop a `startup.nsh` next to the `.efi`:

```nsh
fs0:
cd \
NesEmu.efi
```

Plug it into a machine with a UEFI Shell, boot from the stick, and you're in.

## Controls

### ROM browser

| Key | Action |
|-----|--------|
| ↑ / ↓ | Move selection |
| Enter | Launch the selected ROM |
| Esc | Quit the emulator |

### In-game

| Key | NES gamepad |
|-----|-------------|
| ↑ ↓ ← → | D-Pad |
| Z | A |
| X | B |
| A | Turbo A (auto-fire A) |
| S | Turbo B (auto-fire B) |
| Enter | START |
| Space | SELECT |
| P | Toggle pause |
| F5 | Reset |
| Esc | Quit game, return to ROM browser |

**A note on the keyboard protocol**: UEFI SimpleTextInput has no key-up events. The emulator fakes release detection with a "no refresh for 3 frames (~50 ms) means released" heuristic. Long presses work fine; very quick taps may be dropped. This is an inherent limitation of UEFI input, not a bug.

## Porting rationale

The full design doc lives in [CLAUDE.md](CLAUDE.md). Key takeaways:

- **Reused as-is** (with at most macro tweaks): `cpu6502`, `ppu`, `mmu`, `mappers/*`, `genie`, `font.h`
- **Rewritten**: `gfx` (SDL → GOP `Blt`), `timers` (Win/Linux → TSC + `gBS->Stall`), `utils` (`LOG` → `AsciiPrint`, `SDL_IOFromFile` → `EFI_FILE_PROTOCOL`), `main` (→ `UefiMain`)
- **Removed entirely**: `apu`, `biquad`, `nsf`, `nsf_gfx`, `gamepad`, `touchpad`
- **libc compat shim** (`nes_compat.h`): force-included into every translation unit via `/FI`, redirects `malloc` / `free` / `memset` / `memcpy` / `strlen` etc. to EDK2's `AllocatePool` / `FreePool` / `SetMem` / `CopyMem` / `AsciiStrLen`. The upstream NES source itself stays untouched.

## Known limitations

- No audio (a design choice, not a bug)
- Input protocol has no key-up; long presses are fine, very fast taps may be dropped
- GOP mode must be `PixelBlueGreenRedReserved8BitPerColor` or `PixelRedGreenBlueReserved8BitPerColor`; other formats fall back to `Blt()` (slightly slower)
- Only verified on X64 OVMF + QEMU; real-hardware IA32/X64 UEFI should also work, but you need an architecture-matched `NesEmu.efi`

## Acknowledgments

- **Upstream emulator**: [ObaraEmmanuel/NES](https://github.com/ObaraEmmanuel/NES) — full 6502 + PPU + APU + Mapper implementation. The CPU / PPU / MMU / Mapper code in this repo is lifted directly from that source tree
- **Development process**: [MikeWuPing/emulator-uefi-shell-app](https://github.com/MikeWuPing/emulator-uefi-shell-app) skill — a Claude Code skill that provides the standard workflow for going from EDK2 project scaffolding to a QEMU debug loop for UEFI Shell apps
- **EDK2**: [tianocore/edk2](https://github.com/tianocore/edk2) — the reference UEFI firmware implementation that supplies every Boot Service / Runtime Service / Protocol definition used here

## License

Upstream [ObaraEmmanuel/NES](https://github.com/ObaraEmmanuel/NES) is MIT-licensed; this port keeps MIT. Please retain the upstream copyright notice when redistributing.
