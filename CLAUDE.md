# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目定位

本工程的目标，是把 `Refer/` 目录中那套基于 SDL3 的纯 C 语言 NES 模拟器移植到 **UEFI Shell** 环境下，做成一个可在 Shell 提示符后直接加载执行的图形化 `.efi` 应用。移植范围明确收敛在三点：去声音、改用 UEFI GOP 输出图像、用 UEFI 简单文件系统协议加载 ROM。换句话说，CPU/PPU/MMU/Mapper 这条核心仿真链路原样保留，所有与宿主平台耦合的"外壳"（窗口、音频、事件循环、文件 IO、定时器、入口函数）都要重写为 UEFI 等价物。

仓库目前由三块组成：`Refer/` 是上游参考实现（**只读**，不要修改），`ROM/` 存放用于回归测试的 NES ROM，剩下的源码目录是本次移植要新建的工作区。`Refer/NES-master/` 是空的镜像目录，真正的源码在 `Refer/src/` 与 `Refer/android/` 下。

## 参考工程的分层与移植取舍

理解 `Refer/src/` 的分层，是判断"哪段可以直接搬、哪段必须替换"的前提。核心仿真层包括 `cpu6502.c/h`（完整 6502 指令集，含非官方指令与周期精度）、`ppu.c/h`（NTSC/PAL PPU，最终把一帧 256×240 的 ARGB8888 像素写入 `ppu->screen`）、`mmu.c/h`（内存与 IO 寄存器，0x2000–0x4017）、`mappers/*.c`（已实现 NROM/MMC1/UxROM/CNROM/MMC3/AxROM/Color Dreams/GNROM/VRC1 等十余种 mapper）以及 `genie.c`（Game Genie 金手指）。这一层与平台完全无关，移植时可以原样引入，唯一需要注意的是 `mmu.c` 中对 APU 寄存器的分发——既然不做声音，应保留寄存器读写占位以免 CPU 因访问 `0x4000–0x4017` 而崩溃，但 `execute_apu` 调用、`queue_audio` 推流都要在 `emulator.c` 的主循环里剔除。

平台耦合层是真正需要重写的部分。`gfx.c/h` 把 SDL_Window/Renderer/Texture 包了一层 `GraphicsContext`，UEFI 端需要把这层抽象替换为基于 `EFI_GRAPHICS_OUTPUT_PROTOCOL` 的等价实现：`SDL_UpdateTexture + SDL_RenderPresent` 对应 GOP 的 `Blt()`，注意 PPU 输出是 ARGB8888，要按 GOP 实际的 `PixelFormat`（通常是 `PixelBlueGreenRedReserved8BitPerColor`）做一次字节序转换或直接走 `EfiBltVideoToVideo`/Buffer 模式。`timers.c/h` 内部已经按 `_WIN32` 与 Linux 分叉，逻辑是"高精度计数 + 帧周期补差睡眠"，UEFI 端应再加一分支，用 `gBS->HandleProtocol` 拿 `EFI_RUNTIME_SERVICES->GetTime` 或更精确的 Timer Protocol 做计时，睡眠用 `gBS->Stall`（微秒级）。

输入侧的取舍同样需要明确。参考工程把键盘映射写在 `controller.c::keyboard_mapper`、把游戏手柄放在 `gamepad.c`、把 Android 触屏虚拟摇杆放在 `touchpad.c`，三者最终都汇入 `update_joypad`。移植到 UEFI 时只需要保留键盘路径，事件源换成 `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL`（注意是 EX 版本，能拿到 Shift/Ctrl 等修饰键），原有的 `keyboard_mapper` 主体可以复用，把 `SDLK_*` 常量换成 UEFI ScanCode 即可。`SDL_PollEvent` 那个事件循环改成对 SimpleTextInputEx 的非阻塞 `ReadKeyStrokeEx` 轮询。

文件加载是另一个必须替换的点。参考工程里 `mappers/mapper.c::load_file` 用 `SDL_IOFromFile` 打开 ROM、`SDL_ReadIO` 读出整块到 `malloc` 出的缓冲。UEFI 端对应物是 `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` → `EFI_FILE_PROTOCOL` → `Read`，整段 `read_file_to_buffer` 重写时保留其签名（`long long read_file_to_buffer(char *path, uint8_t **buf)`），调用方就不需要改。配套的 `utils.c::file_size`/`get_file_name`/`LOG` 也要做 UEFI 化——`LOG` 直接走 `ConOut->OutputString`。

最后是入口与构建系统。`main.c` 现在只是 `init_emulator / run_emulator / free_emulator` 三段式，UEFI 端要改为 `EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)`（或者用 ShellPkg 的 `ShellAppMain` 以保留类 `main(int argc, char** argv)` 的入口，便于复用现有 `argv[1]` 取 ROM 路径的逻辑）。CMake 与 SDL3/SDL3_ttf submodule 一并弃用，改为 EDK2 的 `.inf` + `.dsc`；NSF 播放器（`nsf.c`、`nsf_gfx.c`）与触摸/手柄相关源文件直接从编译列表里剔除，能显著压缩最终 `.efi` 体积。

## ROM 选择界面的参考蓝本

用户要求"仿照 `Refer/android`"做一个简单的 ROM 加载界面，因此需要先看懂 Android 端是怎么组织这块的。`MainActivity.java` 在 `onCreate` 里调用 `getAssets().list("roms")` 扫包内 ROM 目录，对每个文件去掉扩展名得到展示名、匹配同名 `.jpg` 作为封面，组装成 `NESItemModel` 列表交给 `NESItemAdapter`（RecyclerView + `GridAutoFitLayoutManager`）。`ROMList.java` 在此基础上叠了 All Games / Recent / Favorites / Music 四个分类，并从 `assets/games.json` 拉取年份、开发商、发行商等元数据。选中条目后 `EmulatorActivity.launchROM` 通过 Intent 把 `"rom"` 路径传给 SDLActivity 子类，最终在 `getArguments()` 里拼成 `argv` 传回 native `main`。

UEFI Shell 上不需要这一整套 Java/UI 框架，但要保留它的"形态"。建议的等价实现是用 GOP 在 framebuffer 上手绘一个垂直列表：扫描当前 Shell 所在 `fs0:`（或所有可枚举的 SimpleFileSystem Handle）根目录与 `ROM\` 子目录下的 `*.nes` 文件，把文件名画到固定行高的矩形里，用方向键上下移动选中项、回车确认、Esc 返回。列表不需要封面图和元数据——`games.json` 仅供"形态"参考，UEFI 端无字体引擎可用时，可以直接复用 `Refer/src/font.h` 那份内嵌的点阵字体（约 16 万字节，原工程已经用它做无 TTF 时的回退）。这一界面跑在主循环里，选中 ROM 后再进入 `run_emulator` 的主仿真循环。

## 关键路径与约定

工作目录布局约定如下，方便后续 Claude 实例快速定位：

- `D:\Work\Code\NesEmu\Refer\` — **只读**参考实现，源码主体在 `Refer/src/`，Android UI 蓝本在 `Refer/android/app/src/main/java/com/barracoder/android/`。
- `D:\Work\Code\NesEmu\ROM\` — 测试 ROM。当前已有 `Contra (USA).nes`（131088 字节，iNES 头，mapper #2 UxROM，NTSC），是默认的冒烟测试目标。
- 新建的 UEFI 工程源码建议放 `D:\Work\Code\NesEmu\src\`，构建产物（`Build/`、`.efi`）需要加入 `.gitignore`。

可直接复用（不改动或仅增宏分支）的源文件清单：`cpu6502.c/h`、`ppu.c/h`、`mmu.c/h`、`controller.c/h`（删除 SDL 依赖部分）、`mappers/*.c`、`genie.c/h`、`debugtools.c/h`、`trace.c/h`、`utils.h` 的枚举与宏、`font.h`。需要重写的：`gfx.c/h`、`timers.c/h`、`utils.c`（LOG/quit/file_size/get_file_name）、`emulator.c`（删除 APU/NSF/Touch 分支）、`main.c`（换入口）、`mappers/mapper.c` 中的 `read_file_to_buffer`。完全剔除：`apu.c/h`、`biquad.c/h`、`nsf.c/h`、`nsf_gfx.c/h`、`gamepad.c/h`、`touchpad.c/h`。

## 构建与运行

工具链与运行环境已经全部就绪，无需额外下载：

- **EDK2 源码树**位于上一级目录 `D:\Work\Code\edk2`（已初始化，`Conf/`、`BaseTools/` 可用，默认 `tools_def.txt` 已指向 VS2022）。本工程的代码以独立 package 形式放在 `D:\Work\Code\NesEmu\NesEmuPkg\`，构建时通过环境变量 `PACKAGES_PATH=D:\Work\Code\edk2;D:\Work\Code\NesEmu` 让 EDK2 同时看到 `MdePkg` 和 `NesEmuPkg`。**不要把 NesEmuPkg 直接复制进 edk2 树**，保持用户工作目录与上游 EDK2 解耦。
- **工具链**用 VS2022（`Conf/target.txt` 里 `TOOL_CHAIN_TAG = VS2022`）。目标架构选 **X64**，与 `D:\Work\Code\GopApp\OVMF.fd`（其实就是 QEMU 自带的 `edk2-x86_64-code.fd`，md5 完全一致）匹配；之前误用 IA32 时 Shell 报 `lasterror=0x3`，PE32+ 跨架构加载会被 X64 OVMF 拒绝。
- **QEMU** 装在 `C:\Program Files\qemu\qemu-system-x86_64.exe`，自带 `share\edk2-x86_64-code.fd` 可作 OVMF 固件；`D:\Work\Code\GopApp\OVMF.fd` 是已经验证可用的副本，直接复用即可。
- **NASM / mingw64** 等本地工具放在 `D:\Work\Code\tools\` 下，EDK2 BaseTools 已经能找到，无需手动添加 PATH。

构建步骤由仓库根的 `build.ps1` 封装：脚本会先调 `edksetup.bat` 初始化环境、设好 `PACKAGES_PATH`，再 `build -p NesEmuPkg/NesEmuPkg.dsc -m NesEmuPkg/NesEmu.inf -a X64 -t VS2022 -b DEBUG`，产物在 `D:\Work\Code\edk2\Build\NesEmuPkg\DEBUG_VS2022\X64\NesEmu.efi`。运行步骤由 `run.ps1` 封装：把 `NesEmu.efi` 与 `ROM/` 目录一起放入一个 FAT 目录，写一份 `startup.nsh` 自动启动，再用 `qemu-system-x86_64 -pflash OVMF.fd -m 256 -drive file=fat:rw:esp,format=raw -nographic` 加载。

参考工程 `D:\Work\Code\GopApp` 是一份现成的 UEFI GOP 应用模板（俄罗斯方块），它的 `src/entry.c`、`src/render.c`、`src/input.c` 已经把 GOP 初始化、`SetPixel`/`DrawRect`/5×8 点阵字体、`PollKey` 这些底层活做完，移植 NES 模拟器时可以直接照搬这套基础设施，区别只在渲染目标从游戏板换成 PPU 的 256×240 framebuffer。

调试层面，参考工程原用 `LOG(LEVEL, fmt, ...)` 宏统一输出，移植后应保留这一约定，把 `LOG` 重定向到 `SystemTable->ConOut`（QEMU `-nographic` 下走串口可见）。`utils.h` 里 `LOGLEVEL` 宏的取值（0=TRACE / 1=DEBUG / 2=默认）同样沿用，发布构建应把 `DEBUGGING_ENABLED` 关闭以减小体积。

调试层面，参考工程原用 `LOG(LEVEL, fmt, ...)` 宏统一输出，移植后应保留这一约定，把 `LOG` 重定向到 `SystemTable->ConOut` 或串口（`SerialDxe`）。`utils.h` 里 `LOGLEVEL` 宏的取值（0=TRACE / 1=DEBUG / 2=默认）同样沿用，发布构建应把 `DEBUGGING_ENABLED` 关闭以减小体积。

## 工作目录与产物

为避免 EDK2 工作树污染，本工程在 `D:\Work\Code\NesEmu\` 下保持自洽：

- `NesEmuPkg/` — EDK2 package（`NesEmuPkg.dsc`、`NesEmuPkg.dec`、`NesEmu.inf`、源码子目录）。这是 EDK2 构建系统的入口。
- `Emu/` — 从 `Refer/src/` 移植过来的核心仿真层（CPU/PPU/MMU/Mapper/Controller），剥离 SDL 依赖。
- `Uefi/` — UEFI 适配层（GOP 渲染、定时器、文件 IO、输入、ROM 浏览器、UEFI 入口）。
- `build.ps1` / `run.ps1` — 构建与 QEMU 运行脚本。
- `ROM/` — 测试 ROM，运行时会被 `run.ps1` 软链到 FAT 镜像。
- `Refer/` — 上游只读参考，不要改动。
- EDK2 构建产物（`Build/NesEmuPkg/`）会落在 `D:\Work\Code\edk2\Build\` 下，不进本仓库。

## 常见陷阱

UEFI Shell 下的 EDK2 libc（来自 `StdLib` 或 ShellPkg 自带的子集）覆盖有限。`malloc/free` 通过 `MemoryAllocationServices` 桥接可用，但 `printf` 浮点格式化、`fseek/ftell`、`time.h` 的 `clock_gettime` 都不可直接套用——`utils.c` 中的 `file_size`、`timers.c` 中的 timespec 计算都需要按上述重写。`SDL_PollEvent` 是阻塞式事件泵，UEFI 端若同样阻塞会卡住帧调度，必须用非阻塞的 `ReadKeyStrokeEx` 并在主循环里自驱动。PPU 的 `screen` 缓冲默认是 ARGB8888，若 GOP 是 `PixelRedGreenBlueReserved8BitPerColor` 需要在 `render_graphics` 里做 R/B 交换，不要在 PPU 内部改格式——保持 PPU 平台无关。`Refer/src/utils.c` 中的 `SDL_RenderDrawCircle` 等 SDL 工具函数仅 ROM 选择界面可能用到，可以一并替换为 GOP 上的 Bresenham 实现，或干脆删掉只画矩形。
