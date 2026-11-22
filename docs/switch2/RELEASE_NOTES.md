## Eden-NX2 — Native Nintendo Switch 2 Port of Eden

**Eden-NX2** is a native **Nintendo Switch 2** (`aarch64` / `OUN0` NRO) homebrew port of the **Eden** Nintendo Switch 1 emulator, built on **`ounce-devkit`**, **`OunceVK`** (static Vulkan 1.4), and **`kaiHEN`** dual-mapped ARM64 JIT (`Dynarmic`).

> **Português (BR):** O **Eden-NX2** permite rodar jogos de **Nintendo Switch 1** nativamente como homebrew no **Nintendo Switch 2** (`eden-switch2.nro`), com renderização **Vulkan 1.4 (`OunceVK`)**, **JIT ARM64 (`kaiHEN`)**, launcher gráfico bilíngue (PT/EN) em 1280×720, gerenciador de Updates/DLCs e suporte a geração de frames via **Lossless Scaling (`LSFG`)**.

---

### What's New / Novidades
- **Built-In GitHub Auto-Updater (`[L]` in Launcher):**
  - Checks `davidkalil10/eden-nx2` latest GitHub Release directly from your Switch 2, downloads `eden-switch2.nro` with a live progress bar, verifies `NRO0` + `OUN0` headers before replacing on SD, and relaunches into the new version.
- **Major Switch 2 Performance & Memory Tuning:**
  - **Full GPU Clocks by Default:** Changed `FastLoad` clock mode to **Off (Normal / Full GPU)** by default (`FastLoad` boosts CPU but throttles GPU — still available per-game under `[Y]` for CPU-bound titles).
  - **Vulkan & Unified RAM Upgrades:** Enabled **Asynchronous Presentation**, **Extended Dynamic State 3 (`EDS3`)**, `FifoRelaxed` VSync fallback, **3 parallel shader/pipeline workers** + **2 texture transcode workers**, expanded VRAM cache budget (`3 GiB`, `1 GiB` GC floor), `128 MiB` stream buffer, and `64 MiB` VMA blocks.
  - **Dynarmic ARM64 JIT & Horizon OS Optimizations:** Built with `-O3 -fomit-frame-pointer`, native ARM64 `cntvct_el0` hardware `WallClock`, fused `LDR [Xpt, Xidx, LSL #3]` + branchless `UBFX` page-table lookups in `CPU Unsafe`, coalesced `kaiHEN` IPC range flushes, `208 MiB` JIT pool (`48 MiB`/core with automatic `144 MiB` fallback), and `svcSetThreadPriority` + adaptive 6-core pinning.
- **Supported Game Formats:** `.nsp`, `.nsz` (block & solid compressed), `.xci`, `.xcz`, `.nca`, and `.nro`.
- **Uniform Settings UI & Per-Game Update / DLC Manager (`[Y]`):**
  - Unified crisp font sizing (`scale = 2`) across all Global `[X]` and Per-Game `[Y]` settings rows.
  - Dedicated **Update Picker** with readable filenames, sizes, and built-in **SD card file browser**.
  - Dedicated **Multi-Select DLC Manager** (`[X]` checkboxes) supporting multiple DLC files and/or folders per game.
- **Lossless Scaling Frame Generation (`LSFG` 2x / 3x / 4x):**
  - Place your `Lossless.dll` at `sdmc:/switch/eden-nx2/Lossless.dll` (top bar badge shows **`LSFG OK`**).
  - On-screen FPS overlay displays `base -> generated FPS` when LSFG is active.
- **1280×720 Bilingual Graphical Launcher (PT / EN):**
  - Toggle language anytime with **`-`**, global settings with **`X`**, per-game settings with **`Y`**, auto-updater with **`L`**, touch navigation, and live controller/gyro tester.
  - Hold **`+` + `-` (~2 seconds)** in-game to return cleanly to the **Eden-NX2 Launcher**.

---

### Quick SD Card Setup (`sdmc:`)
Download **`eden-switch2.nro`** from the **Assets** section below and place your files on your Switch 2 microSD card:

```text
sdmc:/
├── switch/
│   └── eden-nx2/
│       ├── eden-switch2.nro          # Homebrew binary (OUN0)
│       ├── roms/                     # .nsp / .nsz / .xci / .xcz / .nca / .nro games
│       ├── updates/                  # Game Update packages (optional subfolders)
│       ├── dlc/                      # Game DLC packages (files or per-game subfolders)
│       ├── keys/
│       │   └── prod.keys             # Your dumped Switch 1 decryption keys
│       └── Lossless.dll              # (Optional) Enables LSFG 2x/3x/4x Frame Generation
└── .local/
    └── share/
        └── eden/
            ├── keys/                 # Alternative standard keys path (prod.keys)
            └── nand/                 # Emulated Switch NAND & saves
```

> [!NOTE]
> You must supply your own legitimately dumped Switch 1 decryption keys (`prod.keys`) and game dumps. No copyrighted keys, firmware, or ROMs are included.

---

### Controls
- **Launcher:** `D-Pad` / Touch to browse · **`A`** Launch / Open · **`B`** Parent folder · **`X`** Global Settings · **`Y`** Per-Game Settings (Update / DLC / Overrides) · **`L`** Auto-Updater · **`-`** PT/EN · **`+`** Exit
- **In-Game:** Hold **`+` + `-` (~2s)** to close the active game and return to the launcher.

---

### Known Issues
- **`.nup` packages** (Switch 1 / Switch 2) are experimental and not fully working yet.
- Large multi-GB **solid** `.nsz` files may take longer to load than `.nsp` or block-compressed `.nsz` (`nsz -C -B`).

---

### Support
Maintained by **David Kalil Braga ([@davidkalil10](https://github.com/davidkalil10))**. If you enjoy **Eden-NX2** and our Switch 2 homebrew ports, consider supporting development on Ko-fi:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/davidkalil10)

👉 **[https://ko-fi.com/davidkalil10](https://ko-fi.com/davidkalil10)**
