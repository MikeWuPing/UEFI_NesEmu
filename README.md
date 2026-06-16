# NesEmu — UEFI Shell 下的 NES 模拟器

[English](README_EN.md) | 简体中文

把 [ObaraEmmanuel/NES](https://github.com/ObaraEmmanuel/NES) 那套基于 SDL3 的纯 C 语言 NES 模拟器移植到 UEFI Shell，做成一个能在 Shell 提示符后直接加载执行的图形化 `.efi` 应用。CPU、PPU、MMU、Mapper 这条核心仿真链路原样保留；声音被整体剥离，渲染走 UEFI GOP，文件 IO 走 UEFI 简单文件系统协议，输入走 UEFI SimpleTextInput。
<img width="955" height="892" alt="Snipaste_2026-06-16_22-09-19" src="https://github.com/user-attachments/assets/9c1db6b1-a565-459a-8f29-7925340fd3c3" />
<img width="1011" height="921" alt="Snipaste_2026-06-16_22-08-04" src="https://github.com/user-attachments/assets/5142b651-49b1-4f32-b34c-b264acf18209" />
<img width="626" height="477" alt="full2" src="https://github.com/user-attachments/assets/397150fc-4c0d-40b8-8caa-9c36bc1cbbe6" />


## 项目定位

本工程的目标用户是会折腾 UEFI 固件、OVMF、QEMU 的开发者：把一台软件 NES 装进 BIOS 设置画面之前那段空白时间，让 Shell 用户能直接挑 ROM 玩。换句话说，它是一个"裸机 NES"——不依赖任何操作系统，只依赖 EFI Boot Services 和 EFI Runtime Services。

## 特性

- **完整 6502 CPU 仿真**：官方指令、非官方指令、cycle accuracy、dummy reads 全部沿用上游实现
- **PPU**：NTSC / PAL，256×240 ARGB 输出
- **Mapper 覆盖**：NROM (#0)、MMC1 (#1)、UxROM (#2)、CNROM (#3)、MMC3 (#4)、AxROM (#7)、Color Dreams (#11)、GNROM (#66)、VRC1 (#75) 等
- **GOP 渲染**：自动选择 4:3 分辨率（默认 640×480），按整数倍居中缩放
- **ROM 浏览器**：启动后扫描 `fs0:\ROM\*.nes`，键盘上下选 + 回车启动，无需命令行参数
- **无声音**：APU 模块整体剔除，但 `0x4000–0x4017` 寄存器读写仍占位，保证游戏代码不会因访问 APU 崩溃
- **TSC + `gBS->Stall` 帧定时**：60 Hz (NTSC) / 50 Hz (PAL) 帧周期

## 目录结构

```
NesEmu/
├── CLAUDE.md             # 工程设计与移植取舍说明
├── build.ps1             # EDK2 build 封装脚本（X64 + VS2022 + DEBUG）
├── run.ps1               # QEMU + OVMF 启动封装
├── README.md             # 中文 README（本文）
├── README_EN.md          # 英文 README
├── ROM/                  # 测试 ROM（用户自行提供 .nes 文件）
├── Refer/                # 上游参考实现（只读，不参与构建）
└── NesEmuPkg/            # EDK2 package
    ├── NesEmuPkg.dsc     # platform description
    ├── NesEmuPkg.dec     # package declaration
    ├── NesEmu.inf        # UEFI application module
    ├── Emu/              # 从 Refer/src/ 移植的核心（剥离 SDL/APU/NSF）
    │   ├── cpu6502.c/h
    │   ├── ppu.c/h
    │   ├── mmu.c/h
    │   ├── controller.c/h
    │   ├── emulator.c/h
    │   ├── genie.c/h
    │   ├── utils.c/h
    │   └── mappers/
    └── Uefi/             # UEFI 适配层
        ├── entry.c       # UefiMain 入口
        ├── main.c        # NesEmuMain 顶层流程
        ├── rom_browser.c # ROM 选择 UI
        ├── uefi_gfx.c    # GraphicsContext + GOP 渲染
        ├── uefi_timer.c  # TSC + Stall 帧定时
        ├── uefi_fs.c     # SimpleFileSystem ROM 加载
        ├── uefi_input.c  # 键盘 → JoyPad
        ├── font.c        # 内嵌 5×8 点阵字体（ROM browser UI 用）
        └── nes_compat.h  # libc → EDK2 函数重定向 shim
```

## 构建与运行

### 依赖

- **EDK2**：克隆 [edk2/edk2](https://github.com/tedhudson/edk2) 或 Intel 官方仓库到本地（建议路径 `D:\Work\Code\edk2`），运行过一次 `edksetup.bat` 让 `Conf/` 和 `BaseTools/` 就绪
- **VS2022**：构建工具链（`TOOL_CHAIN_TAG = VS2022`）
- **QEMU**：测试用，需要 OVMF 固件（QEMU 自带的 `share\edk2-x86_64-code.fd` 即可）
- **NASM**：EDK2 BaseTools 编译需要

### 构建

```powershell
# 1. 设置环境变量（每次新开终端都要做）
$env:PACKAGES_PATH = "D:\Work\Code\edk2;D:\Work\Code\NesEmu"

# 2. 一键构建
.\build.ps1
```

`build.ps1` 内部会调 `edksetup.bat` 初始化环境、再执行 `build -p NesEmuPkg/NesEmuPkg.dsc -m NesEmuPkg/NesEmu.inf -a X64 -t VS2022 -b DEBUG`，产物在 `D:\Work\Code\edk2\Build\NesEmuPkg\DEBUG_VS2022\X64\NesEmu.efi`（约 50 KB）。

### 运行（QEMU）

```powershell
# 把 ROM 放进 ROM/ 目录，然后
.\run.ps1
```

`run.ps1` 会：
1. 把 `ROM\*.nes` + `NesEmu.efi` + `startup.nsh` 打包到 `esp\` FAT 目录
2. 用 `qemu-system-x86_64 -pflash <OVMF.fd> -drive file=fat:rw:esp,format=raw` 启动
3. OVMF 启动 UEFI Shell，5 秒后自动执行 `startup.nsh`，进入 `NesEmu.efi`

无头测试模式（不需要 GUI，捕获 serial 到 `serial.log`）：

```powershell
$env:NESEMU_HEADLESS_SECONDS = 20
.\run.ps1
```

### 运行（真机）

把 `NesEmu.efi` 和 `ROM/` 目录拷到任意 FAT32 U 盘根目录，写一份 `startup.nsh`：

```nsh
fs0:
cd \
NesEmu.efi
```

插到一台带 UEFI Shell 的机器上，从 U 盘启动即可。

## 操作按键

### ROM 浏览器

| 按键 | 功能 |
|------|------|
| ↑ / ↓ | 上下移动选中项 |
| Enter | 启动选中的 ROM |
| Esc | 退出模拟器 |

### 游戏内

| 按键 | NES 手柄 |
|------|---------|
| ↑ ↓ ← → | D-Pad 方向 |
| Z | A |
| X | B |
| A | Turbo A（连打 A） |
| S | Turbo B（连打 B） |
| Enter | START |
| Space | SELECT |
| P | Pause 切换 |
| F5 | Reset |
| Esc | 退出游戏，回到 ROM 浏览器 |

**关于键盘协议的注意事项**：UEFI SimpleTextInput 没有 key-up 事件，本模拟器内部用「连续 3 帧（约 50 ms）没收到刷新就视为松开」的近似策略。长按方向键 OK，但快速点按可能被丢帧。这是 UEFI 输入协议的固有限制，不是 bug。

## 移植取舍

完整设计文档见 [CLAUDE.md](CLAUDE.md)。核心要点：

- **可直接复用**（不改动或仅增宏分支）：`cpu6502`、`ppu`、`mmu`、`mappers/*`、`genie`、`font.h`
- **重写**：`gfx`（SDL → GOP `Blt`）、`timers`（Win/Linux → TSC + `gBS->Stall`）、`utils`（`LOG` → `AsciiPrint`、`SDL_IOFromFile` → `EFI_FILE_PROTOCOL`）、`main`（→ `UefiMain`）
- **完全剔除**：`apu`、`biquad`、`nsf`、`nsf_gfx`、`gamepad`、`touchpad`
- **libc 兼容 shim**（`nes_compat.h`）：通过 `/FI` 强制 include 到每个翻译单元，把 `malloc` / `free` / `memset` / `memcpy` / `strlen` 等重定向到 EDK2 的 `AllocatePool` / `FreePool` / `SetMem` / `CopyMem` / `AsciiStrLen`，上游 NES 源码本身完全不改

## 已知限制

- 没有声音（设计取舍，不是 bug）
- 输入协议无 key-up，长按方向键 OK，快速点按可能丢帧
- GOP 模式必须是 `PixelBlueGreenRedReserved8BitPerColor` 或 `PixelRedGreenBlueReserved8BitPerColor` 之一；其他格式走 `Blt()` 回退路径（性能稍差）
- 仅在 X64 OVMF + QEMU 上验证过；真机 IA32/X64 UEFI 应该也能跑，但需要相应架构的 NesEmu.efi

## 致谢

- **上游模拟器**：[ObaraEmmanuel/NES](https://github.com/ObaraEmmanuel/NES) —— 完整的 6502 + PPU + APU + Mapper 实现，本工程的 CPU/PPU/MMU/Mapper 代码直接搬自这套源码
- **开发过程参考**：[MikeWuPing/emulator-uefi-shell-app](https://github.com/MikeWuPing/emulator-uefi-shell-app) skill —— Claude Code skill，提供了 UEFI Shell 应用从 EDK2 工程骨架到 QEMU 调试循环的标准流程指导
- **EDK2**：[tianocore/edk2](https://github.com/tianocore/edk2) —— UEFI 固件参考实现，提供了所有 Boot Services / Runtime Services / Protocol 定义

## License

上游 [ObaraEmmanuel/NES](https://github.com/ObaraEmmanuel/NES) 是 MIT License，本移植维持 MIT。请在分发时同时保留上游版权声明。
