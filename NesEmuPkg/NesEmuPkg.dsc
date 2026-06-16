## @file
#  NesEmuPkg platform description. Builds the UEFI Shell NES emulator application.
##
#  Copyright (c) 2026 NesEmu contributors. All rights reserved.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = NesEmuPkg
  PLATFORM_GUID                  = 9F4E2B71-3C46-4D6F-8E1A-2A5B9C0D1E3F
  PLATFORM_VERSION               = 0.10
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/NesEmuPkg
  SUPPORTED_ARCHITECTURES        = IA32|X64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

[BuildOptions]
  # The NES emulator core uses unsigned-underflow, size_t conversions and
  # legacy C patterns that are deliberate. Silence the noisy warnings so
  # the VS toolchain does not error out under /W4. Module-specific build
  # options (notably the libc-compat /FI shim) live in NesEmu.inf so they
  # do not leak into MdePkg libraries.
  MSFT:*_*_*_CC_FLAGS   = /wd4146 /wd4244 /wd4245 /wd4267 /wd4305 /wd4306 /wd4996 /wd4333 /GL- /Oi-
  GCC:*_*_*_CC_FLAGS    = -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function -Wno-sign-compare -Wno-format -Wno-char-subscripts

[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  CompilerIntrinsicsLib|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[LibraryClasses.IA32]
  # Same as default; placeholder for future arch-specific overrides.

[Components]
  NesEmuPkg/NesEmu.inf
