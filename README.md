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
**Eden-NX2** runs Nintendo Switch 1 games (`.nsp`, `.nsz`, `.xci`, `.xcz`, `.nca`, `.nro`) natively as a Switch 2 homebrew application (`eden-switch2.nro`), built directly on **`ounce-devkit`**, **`libounce`**, **`OunceVK`** (static Vulkan 1.4), and **`kaiHEN`** dual-mapped JIT memory.

**Português (BR):**  
O **Eden-NX2** é um port nativo de homebrew para **Nintendo Switch 2** (`OUN0` NRO / Cortex-A78C) do emulador **Eden**, utilizando **Vulkan 1.4 (`OunceVK`)**, **JIT ARM64 via `kaiHEN`**, launcher gráfico integrado em 1280×720, suporte bilíngue (PT/EN) e geração de frames via **Lossless Scaling (`LSFG`)**.

> [!NOTE]
> You must supply your own legitimately dumped Switch 1 decryption keys (`prod.keys`) and game dumps. No copyrighted keys, firmware, or ROMs are included in this repository.

> [!WARNING]
> **Known issues:** Switch 1 / Switch 2 NUP packages are experimental and not fully working yet.

---

## <a id="key-features"></a>Key Features

- **OunceVK Static Vulkan 1.4 Renderer (`VK_NN_vi_surface`):**
  - Renders directly via `nwindowGetDefault()` and `vkCreateViSurfaceNN`, without desktop X11/Wayland/Qt dependencies.
  - Supports internal resolution scaling (`0.5x`–`2.0x`), Handheld (`720p`) / Docked (`1080p`) modes, **GPU Upscaling** (**AMD FSR 1.0**, **SGSR**, **ScaleForce**, **MMPX**, **Lanczos**, **Bilinear**, **Nearest**), **FXAA / SMAA**, `EDS3`, Async Presentation, 3 parallel pipeline workers + 2 texture transcode workers, disk shader cache, **NVK stability hardening** (`VK_EXT_robustness2`, 4 GPU modes: `Fast` / `Accurate` / `Safe (NVK Compat)` / `Ultra-Safe (NAK Serial)`), and **Lossless Scaling Frame Generation (`LSFG` 2x / 3x / 4x)** when `Lossless.dll` is placed on the SD card.
- **`kaiHEN` Dual-Pointer (`RW` / `RX`) ARM64 JIT (`Dynarmic`):**
  - Uses a `208 MiB` shared `kaiHEN` JIT pool (`48 MiB`/core with `144 MiB` fallback), **inline ARM64 hardware atomics (`LDAXR`/`STLXR`)**, EL0-safe native `CNTPCT_EL0` `WallClock`, fused page-table lookups in `CPU Unsafe`, coalesced IPC range publishing, 6-core Cortex-A78C pinning (`0x3f`), and full GPU clocks by default (`FastLoad = Off`).
- **Horizon-Safe Memory & FAT32 Split Storage (> 4 GB):**
  - 36-bit / 38-bit / 39-bit demand-paged software page tables (`MultiLevelPageTable`) paired with `memalign`-backed `HostMemory` (no `mmap`/`SIGSEGV` dependency).
  - Native streaming support via `ConcatenatedVfsFile` for **FAT32 split games, Updates, and DLCs** (`Game.nsp/` or `Game/` folders containing `00`, `01`, `02`..., `.nsp.00`, 7-Zip `.001`, `.xc0`/`.ns0`) with zero extra RAM or merging required.
- **1280×720 Graphical Launcher, Firmware Installer & Auto-Updater (`[L]`):**
  - Bilingual interface (**Português / English**, toggle anytime with **`-`**), **GitHub Releases Auto-Updater (`[L]`)**, on-screen load/GPU crash diagnostics, global settings (**`X`**), per-game overrides + Update/DLC managers (**`Y`**), touch navigation, and a live 4-player controller tester (`P1..P4`).
  - **Built-In Switch 1 Firmware Manager (`[X]` -> `Switch 1 Firmware`):** Install, switch, or remove Switch 1 firmware dumps directly from **`.zip`** archives or **`.nca`** folders on the SD card with a live progress bar (enables real Miis, system fonts/button icons, and system archives).
  - **Native Switch 2 Software Keyboard (`swkbd`) & Profile Name Editor:** Supports Normal & Inline in-game keyboard prompts via Horizon OS `swkbd` with automatic **built-in QWERTY Virtual Keyboard overlay** fallback (controller + touch), plus editable Switch 1 **Profile Name** in `[X]`.
- **4-Player Local Multiplayer, Individual Joy-Cons & Live In-Game Quick Menu (`L + R + (+)`):**
  - Supports **up to 4 simultaneous players (P1–P4)** with **independent 6-axis gyro/accelerometer and HD Rumble** per player, including **`Auto (Real Switch 2)`** hot-plug detection and **individual Left / Right Joy-Con** support (*Super Mario Party*, *1-2-Switch*, *Mario Kart 8*, *Pokémon: Let's Go*).
  - Press **`L + R + (+)`** in-game to open the live **Quick Menu** overlay (adjust Speed Limit, VSync, Upscaler, AA, Docked/Handheld, GPU Accuracy, FastLoad, FPS overlay, Volume, and P1/P2 controllers in real time, save per-game, or return to launcher).
  - Hold **`+` + `-` (~2s)** anytime in-game to return cleanly to the **Eden-NX2 Launcher**.

---

## <a id="sd-card-layout"></a>SD Card Setup (`sdmc:`)

Copy `eden-switch2.nro` from [GitHub Releases](https://github.com/davidkalil10/eden-nx2/releases) (or [GitHub Actions artifacts](https://github.com/davidkalil10/eden-nx2/actions/workflows/build-switch2.yml)) to your Switch 2 microSD card using the layout below:

```text
sdmc:/
└── switch/
    └── eden-nx2/
        ├── eden-switch2.nro          # Main Switch 2 homebrew binary (OUN0)
        ├── roms/                     # Place .nsp / .nsz / .xci / .xcz / .nca / .nro (or split folders) here
        ├── updates/                  # Place game Update .nsp / .nsz / .xci files here (optional subfolders)
        ├── dlc/                      # Place DLC .nsp / .nsz / .xci files here (or per-game subfolders)
        ├── firmware/                 # Place Switch 1 firmware dumps (.zip or .nca folders) here
        ├── keys/
        │   └── prod.keys             # Switch 1 decryption keys
        ├── Lossless.dll              # (Optional) Enables LSFG 2x/3x/4x Frame Generation
        ├── settings.ini              # Auto-generated global launcher settings
        ├── games/                    # Auto-generated per-game .ini overrides
        └── .local/
            └── share/
                └── eden/
                    └── nand/         # Emulated Switch 1 NAND, installed firmware & game save data
```

### Tips for First Boot
- **Keys (`prod.keys`):** Place `prod.keys` inside `sdmc:/switch/eden-nx2/keys/prod.keys` (or `sdmc:/switch/eden-nx2/.local/share/eden/keys/prod.keys`).
- **Switch 1 Firmware (Recommended for Miis & System Fonts):** Place a Switch 1 firmware `.zip` (or extracted `.nca` folder) inside `sdmc:/switch/eden-nx2/firmware/`, then open **`[X]` Global Settings -> `Switch 1 Firmware`** and press **`[A]`** to install directly into the emulated NAND (fixes Mii screens in *Mario Kart 8 Deluxe*, *Nintendo Switch Sports*, *Super Mario Party / Jamboree*, and missing system button icons/fonts).
- **In-Game Keyboard & Profile Name:** Press **`[X]` -> `Profile Name`** to customize your Switch 1 username. In-game text prompts (`swkbd`, e.g. naming characters in *Pokémon*) automatically use the native Switch 2 keyboard with a built-in QWERTY overlay fallback (configurable via **`In-Game Keyboard`** in `[X]` / `[Y]`).
- **FAT32 Split Games (> 4 GB):** Split clean base `.nsp` / `.xci` / `.nsz` files into `00`, `01`, `02`... folders (via SAK / `splitNSP`) or `.nsp.00` / 7-Zip `.001` files. Verified working on Switch 2 hardware with ***Pokémon Sword*** (3-part split) and ***Super Mario Party Jamboree*** (split). **Tip:** keep Updates separate in `sdmc:/switch/eden-nx2/updates/` instead of consolidating Base + Update into a single `(1G+1U)` NSP before splitting, as some repacked Super-NSPs omit the `.tik` ticket (`ErrorMissingTitlekey`).
- **Updates & DLC:** Put Update `.nsp`/`.xci` files in `sdmc:/switch/eden-nx2/updates/` and DLC `.nsp`/`.xci` files (or subfolders) in `sdmc:/switch/eden-nx2/dlc/` for automatic discovery, or assign/disable them individually per game via **`[Y]` Per-Game Settings**.
- **2-Player Individual Joy-Cons (*Super Mario Party*, *1-2-Switch*, *Mario Kart 8*):**
  1. In the **Switch 2 System Settings / Controllers** menu, detach and pair the two Joy-Cons as **two separate horizontal controllers**.
  2. In **Eden-NX2**, press **`[Y]`** on the game (or open **`L + R + (+)`** live in-game) and set **`Controller (Player 1)` = `Left Joy-Con`** and **`Controller (Player 2)` = `Right Joy-Con`** *(or leave both on **`Auto`**)*. Tested and working cleanly on *Super Mario Party* with independent gyro & HD Rumble!
- ***Pokémon: Let's Go Pikachu / Eevee*:** Set `Controller (Player 1)` to **`Handheld`** or **`Right Joy-Con`** in per-game settings (**`Y`**).
- **Lossless Scaling (`LSFG`):** Copy your `Lossless.dll` to `sdmc:/switch/eden-nx2/Lossless.dll` (or `sdmc:/Lossless.dll`). On launch, the top bar displays **`LSFG OK`** once imported and cached.
- **3D Performance & NVK Compatibility:** Keep **`Clock (FastLoad)` = `Normal (Full GPU)`**, **`GPU = Fast`**, **`CPU = Unsafe`**, **`VSync = Relaxed`** (or `Off`), and try **`AMD FSR 1.0`** / **`SGSR`** upscaling live via **`L + R + (+)`**. If a 3D game freezes or crashes on NVK (e.g. ***Super Mario Party Jamboree***), switch **`GPU Accuracy`** to **`Safe (NVK Compat)`** (or **`Ultra-Safe (NAK Serial)`** for stubborn cutscenes) in **`[Y]`**.

---

## <a id="controls"></a>Controls

### Launcher
| Button / Input | Action |
| :--- | :--- |
| **D-Pad Up / Down / Left / Right** (or Touch tap) | Navigate ROM list / page jump |
| **`A`** (or tap selected ROM twice) | Launch game / open folder |
| **`B`** | Go up to parent directory / cancel settings |
| **`X`** | Open **Global Settings** |
| **`Y`** | Open **Per-Game Settings** (Update / DLC / Controller / Overrides) |
| **`L`** | Open **GitHub Auto-Updater** |
| **`-` (Minus)** | Quick-toggle launcher UI language (**Português / English**) |
| **`+` (Plus)** | Exit launcher to Homebrew Menu |

### In-Game
| Button / Input | Action |
| :--- | :--- |
| **`L` + `R` + `+`** | Open / close the live **In-Game Quick Menu** (settings, P1/P2 controllers, save profile, return to launcher) |
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

- **[Roadmap de Futuras Features (Switch 2)](./docs/switch2/ROADMAP.md)** — Planned phases, storage/multiplayer milestones, and future Eden features.
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
