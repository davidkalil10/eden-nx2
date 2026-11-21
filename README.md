<!--
# SPDX-FileCopyrightText: Copyright 2026 David Kalil Braga (@davidkalil10) - Eden-NX2
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
-->

<h1 align="center">
  <br>
  <a href="https://github.com/davidkalil10/eden-nx2">
    <img src="./dist/qt_themes/default/icons/256x256/eden.png" alt="Eden-NX2" width="180">
  </a>
  <br>
  <b>Eden-NX2</b>
  <br>
</h1>

<h4 align="center">
  Native Nintendo Switch 2 (<code>aarch64</code> / <code>OUN0</code> NRO) homebrew port of the <b>Eden</b> Nintendo Switch 1 emulator.
  <br>
  Maintained by <b>David Kalil Braga (<a href="https://github.com/davidkalil10">@davidkalil10</a>)</b>.
</h4>

<p align="center">
  <a href="https://github.com/davidkalil10/eden-nx2/actions/workflows/build-switch2.yml">
    <img src="https://github.com/davidkalil10/eden-nx2/actions/workflows/build-switch2.yml/badge.svg" alt="Build Eden-NX2 (Nintendo Switch 2)">
  </a>
  <a href="./LICENSE.txt">
    <img src="https://img.shields.io/badge/License-GPLv3%2B-blue.svg" alt="License: GPLv3+">
  </a>
  <a href="https://ko-fi.com/davidkalil10">
    <img src="https://img.shields.io/badge/Ko--fi-Support%20@davidkalil10-FF5E5B?logo=ko-fi&logoColor=white" alt="Support on Ko-fi">
  </a>
</p>

<p align="center">
  <a href="#overview">Overview / Visão Geral</a> |
  <a href="#key-features">Key Features</a> |
  <a href="#sd-card-layout">SD Card Setup</a> |
  <a href="#controls">Controls</a> |
  <a href="#building-from-source">Building & CI</a> |
  <a href="#documentation">Documentation</a> |
  <a href="#support">Support</a> |
  <a href="#license--credits">License & Credits</a>
</p>

---

## <a id="overview"></a>Overview / Visão Geral

**English:**  
**Eden-NX2** runs Nintendo Switch 1 games (`.nsp`, `.xci`, `.nca`, `.nro`) natively as a Switch 2 homebrew application (`eden-switch2.nro`), built directly on **`ounce-devkit`**, **`libounce`**, **`OunceVK`** (static Vulkan 1.4), and **`kaiHEN`** dual-mapped JIT memory.

**Português (BR):**  
O **Eden-NX2** é um port nativo de homebrew para **Nintendo Switch 2** (`OUN0` NRO / Cortex-A78C) do emulador **Eden**, utilizando **Vulkan 1.4 (`OunceVK`)**, **JIT ARM64 via `kaiHEN`**, launcher gráfico integrado em 1280×720, suporte bilíngue (PT/EN) e geração de frames via **Lossless Scaling (`LSFG`)**.

> [!NOTE]
> You must supply your own legitimately dumped Switch 1 decryption keys (`prod.keys`) and game dumps. No copyrighted keys, firmware, or ROMs are included in this repository.

> [!WARNING]
> **Known issues:** NSZ or Switch 1 NUP not working yet.

---

## <a id="key-features"></a>Key Features

- **OunceVK Static Vulkan 1.4 Renderer (`VK_NN_vi_surface`):**
  - Renders directly via `nwindowGetDefault()` and `vkCreateViSurfaceNN`, without desktop X11/Wayland/Qt dependencies.
  - Supports internal resolution scaling (`0.5x`, `0.75x`, `1.0x`, `1.5x`, `2.0x`), Handheld (`720p`) / Docked (`1080p`) modes, disk shader cache, asynchronous shader compilation, and **Lossless Scaling Frame Generation (`LSFG` 2x / 3x / 4x)** when `Lossless.dll` is placed on the SD card.
- **`kaiHEN` Dual-Pointer (`RW` / `RX`) ARM64 JIT (`Dynarmic`):**
  - Uses a 96 MiB shared `kaiHEN` JIT pool with separate `RW`/`RX` views, batched IPC range publishing, configurable ahead-block pre-compilation (`4` / `8` / `16` blocks), 64K lookup cache, Cortex-A78C core pinning, CPU boost clock option, and `CpuAccuracy::Auto` / `Unsafe` toggles.
- **Horizon-Safe Memory & Storage:**
  - 36-bit / 38-bit / 39-bit demand-paged software page tables (`MultiLevelPageTable`) paired with `memalign`-backed `HostMemory` (no `mmap`/`SIGSEGV` dependency).
  - Switch 2 SD card (`sdmc:`) VFS/stdio fixes for buffered reads, handle cleanup, `fstat` sizing, and atomic save-file renames.
- **1280×720 Graphical Launcher:**
  - Bilingual interface (**Português / English**, toggle anytime with **`-`**).
  - Global settings (**`X`**) and per-game overrides (**`Y`**), touch navigation, and a live controller tester (buttons, D-Pad, left/right sticks, `L3`/`R3`, `SL`/`SR`, gyro status).
- **Full Switch 2 Input & In-Game Features:**
  - Profiles for **Pro Controller**, **Handheld** (recommended for *Pokémon: Let's Go*), **Dual Joy-Con**, **Right Joy-Con** (with `SL`/`SR`), and **Left Joy-Con**.
  - Touch screen forwarding, 6-axis gyro/accelerometer (`hidSixAxisSensor`), HD rumble, optional on-screen FPS/speed overlay, and **hold `+` + `-` (~2s)** in game to return directly to the **Eden-NX2 Launcher**.

---

## <a id="sd-card-layout"></a>SD Card Setup (`sdmc:`)

Copy `eden-switch2.nro` from the [GitHub Actions artifacts](https://github.com/davidkalil10/eden-nx2/actions/workflows/build-switch2.yml) to your Switch 2 microSD card using the layout below:

```text
sdmc:/
├── switch/
│   └── eden-nx2/
│       ├── eden-switch2.nro          # Main Switch 2 homebrew binary (OUN0)
│       ├── roms/                     # Place .nsp / .xci / .nca / .nro files here
│       ├── updates/                  # Place game Update .nsp / .xci files here (optional subfolders)
│       ├── dlc/                      # Place DLC .nsp / .xci files here (or per-game subfolders)
│       ├── keys/
│       │   └── prod.keys             # Switch 1 decryption keys (or in .local/share/eden/keys/)
│       ├── Lossless.dll              # (Optional) Enables LSFG 2x/3x/4x Frame Generation
│       ├── settings.ini              # Auto-generated global launcher settings
│       └── games/                    # Auto-generated per-game .ini overrides
└── .local/
    └── share/
        └── eden/
            ├── keys/                 # Alternative/Eden standard keys location (prod.keys)
            └── nand/                 # Emulated Switch NAND & game save data
```

### Tips for First Boot
- **Keys (`prod.keys`):** Place `prod.keys` inside `sdmc:/switch/eden-nx2/keys/prod.keys` or `sdmc:/.local/share/eden/keys/prod.keys`.
- **Updates & DLC:** Put Update `.nsp`/`.xci` files in `sdmc:/switch/eden-nx2/updates/` and DLC `.nsp`/`.xci` files (or subfolders) in `sdmc:/switch/eden-nx2/dlc/` for automatic discovery, or assign/disable them individually per game via **`[Y]` Per-Game Settings**.
- **Lossless Scaling (`LSFG`):** Copy your `Lossless.dll` to `sdmc:/switch/eden-nx2/Lossless.dll` (or `sdmc:/Lossless.dll`). On launch, the top bar displays **`LSFG OK`** once imported and cached.
- **3D Performance:** In **`[X]` Global** or **`[Y]` Per-Game** settings, try `Resolution = 0.5x` or `0.75x`, `GPU = Fast`, `CPU = Unsafe`, `CPU Boost = On`, and `Address Space = 38-bit` (or `36-bit` to free extra RAM).
- ***Pokémon: Let's Go Pikachu / Eevee*:** Set `Controller (Player 1)` to **`Handheld`** or **`Right Joy-Con`** in per-game settings (**`Y`**).

---

## <a id="controls"></a>Controls

### Launcher
| Button / Input | Action |
| :--- | :--- |
| **D-Pad Up / Down / Left / Right** (or Touch tap) | Navigate ROM list / page jump |
| **`A`** (or tap selected ROM twice) | Launch game / open folder |
| **`B`** | Go up to parent directory / cancel settings |
| **`X`** | Open **Global Settings** (13 options) |
| **`Y`** | Open **Per-Game Settings** for highlighted ROM |
| **`-` (Minus)** | Quick-toggle launcher UI language (**Português / English**) |
| **`+` (Plus)** | Exit launcher to Homebrew Menu |

### In-Game
| Button / Input | Action |
| :--- | :--- |
| **Hold `+` and `-` together (~2 seconds)** | Close active game and return to the **Eden-NX2 Launcher** |

---

## <a id="building-from-source"></a>Building from Source & CI

CI builds run automatically via [`.github/workflows/build-switch2.yml`](./.github/workflows/build-switch2.yml) inside the `devkitpro/devkita64:20260219` container with **CMake 3.31.6+**, **`ounce-devkit` (`20261115b`)**, Switch portlibs, and Switch 2 **SDL3** + **OunceVK**.

To build inside a matching `devkita64` + `ounce-devkit` environment:

```bash
bash scripts/build-switch2.sh
```

The build script compiles `eden-cli.elf`, packages [`dist/eden-switch2.nro`](./scripts/build-switch2.sh) with `nacptool` / `elf2nro`, and validates `OUN0` / `kaiHEN` / Switch 2 compliance using [`tools/verify_switch2_package.py`](./tools/verify_switch2_package.py).

> [!TIP]
> You can download pre-built `eden-switch2-nro` artifacts directly from the latest green run in [GitHub Actions](https://github.com/davidkalil10/eden-nx2/actions/workflows/build-switch2.yml).

---

## <a id="documentation"></a>Documentation

- **[Switch 2 Porting Progress & Engineering Log](./docs/switch2/EDEN_NX2_PROGRESS.md)** — Architecture Decision Records (ADRs), phased roadmap, and milestone changelog.
- **[Build Script](./scripts/build-switch2.sh)** & **[GitHub Actions Workflow](./.github/workflows/build-switch2.yml)** — Full cross-compilation and packaging pipeline.
- **[NRO Verification Script](./tools/verify_switch2_package.py)** — Automated `OUN0` + `kaiHEN` binary verification.

---

## <a id="support"></a>Support

If you find **Eden-NX2** and our Nintendo Switch 2 homebrew ports useful, consider buying me a coffee to support continued development and maintenance:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/davidkalil10)

👉 **[https://ko-fi.com/davidkalil10](https://ko-fi.com/davidkalil10)**

---

## <a id="license--credits"></a>License & Credits

- **License:** Licensed under the **GNU General Public License v3.0 or later** — see [LICENSE.txt](./LICENSE.txt).
- **Switch 2 Port Maintainer:** **David Kalil Braga** ([@davidkalil10](https://github.com/davidkalil10)) — Support on [Ko-fi](https://ko-fi.com/davidkalil10).
- **Switch 2 Toolchain & Ecosystem:** [`ouncebrew/ounce-devkit`](https://github.com/ouncebrew/ounce-devkit), `libounce`, `OunceVK`, `kaiHEN`, and `devkitPro`.
- **Upstream Projects:** Built upon **Eden** and its upstream heritage including **yuzu**, **Ryujinx**, **Dynarmic**, **Sudachi**, **Citron**, **Torzu**, **Suyu**, and **Ryubing** contributors.
