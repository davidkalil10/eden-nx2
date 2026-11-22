## Eden-NX2 — Native Nintendo Switch 2 Port of Eden

**Eden-NX2** is a native **Nintendo Switch 2** (`aarch64` / `OUN0` NRO) homebrew port of the **Eden** Nintendo Switch 1 emulator, built on **`ounce-devkit`**, **`OunceVK`** (static Vulkan 1.4), and **`kaiHEN`** dual-mapped ARM64 JIT (`Dynarmic`).

> **Português (BR):** O **Eden-NX2** permite rodar jogos de **Nintendo Switch 1** nativamente como homebrew no **Nintendo Switch 2** (`eden-switch2.nro`), com renderização **Vulkan 1.4 (`OunceVK`)**, **JIT ARM64 (`kaiHEN`)**, launcher gráfico bilíngue (PT/EN) em 1280×720, gerenciador de Updates/DLCs e suporte a geração de frames via **Lossless Scaling (`LSFG`)**.

---

### What's New / Novidades
- **FAT32 Split Game / Update / DLC Support (> 4 GB):**
  - Stream split Switch 1 containers seamlessly via `ConcatenatedVfsFile` without re-merging or extra RAM!
  - Automatically detects **split folders** (`Game.nsp/` or plain `Game/` containing `00`, `01`, `02`, ... from SAK / `splitNSP` / DBI), **dot-numbered splits** (`.nsp.00`, `.01`, ...), **7-Zip splits** (`.nsp.001`, `.002`, ...), and **`.xc0`/`.ns0`** splits — shown as a single clean entry with a **`SPLIT`** badge and total size in the launcher, Update picker, and DLC manager.
- **4-Player Local Multiplayer + Individual Joy-Con Auto-Detection (*Super Mario Party*, *1-2-Switch*, *Mario Kart*):**
  - Expanded Switch 2 HID input to **4 simultaneous players (P1–P4)** with **independent 6-axis gyro/accelerometer and HD Rumble** per player.
  - Added **`Auto (Real Switch 2)`** mode for P1 and **`Auto (If Connected)`** default for P2 / P3–P4 with live hot-plug detection (`Pro`, `Dual Joy-Con`, `Left Joy-Con`, `Right Joy-Con`, `Handheld`), plus live `P1..P4` status badges in the launcher and live **Controller P1 / P2** toggles in the Quick Menu (`L + R + (+)`).
  - **Tested 2-Player Joy-Con Setup (*Super Mario Party*):** In the Switch 2 Controllers menu, pair both Joy-Cons as two separate controllers; then in Eden-NX2 per-game settings (`[Y]`) or Quick Menu (`L + R + (+)`), set **Controller P1 = `Left Joy-Con`** and **Controller P2 = `Right Joy-Con`** (or `Auto`)!
- **On-Screen Load Error Diagnostics & Faster Split Streaming (`v0.1.8`):**
  - Added a clear on-screen error modal in the launcher whenever a ROM fails to load (showing the exact Eden error code/description, VFS size, and container header magic) plus direct split chunk opening and read-only VFS size caching.
  - **FAT32 Split Tip:** Split clean base `.nsp`/`.xci` dumps and keep Updates separate in `updates/` — avoid consolidating Base + Update into a single `(1G+1U)` NSP before splitting, since repacked Super-NSPs can omit the `.tik` ticket (`ErrorMissingTitlekey`).
- **Live In-Game Quick Menu (`L + R + (+)`):**
  - Open an on-screen overlay menu directly over any running game with **`L + R + (+)`** to tweak **Speed Limit**, **VSync**, **GPU Upscaler (FSR/SGSR/ScaleForce/MMPX/Lanczos)**, **Anti-Aliasing (FXAA/SMAA)**, **Docked/Handheld Mode**, **GPU Accuracy**, **Clock (FastLoad)**, **FPS Overlay**, and **Audio Volume** in real time while watching live FPS/visual changes — plus **Save to Game Profile** or **Return to Launcher** with one press!
- **Fixed Exit Hang with Unlocked Speed Limit (`Speed Limit = Off`):**
  - Fixed a `CoreTiming::Advance` infinite drain loop and thread priority starvation when closing a game with Speed Limit disabled (`+` + `-` or Quick Menu exit now returns cleanly to the launcher).
- **Built-In GitHub Auto-Updater (`[L]` in Launcher):**
  - Checks `davidkalil10/eden-nx2` latest GitHub Release directly from your Switch 2, downloads `eden-switch2.nro` with a live progress bar, verifies `NRO0` + `OUN0` headers before replacing on SD, and relaunches into the new version.
- **Major Switch 2 Performance & Memory Tuning:**
  - **Full GPU Clocks by Default:** Changed `FastLoad` clock mode to **Off (Normal / Full GPU)** by default (`FastLoad` boosts CPU but throttles GPU — still available per-game under `[Y]` for CPU-bound titles).
  - **Vulkan & Unified RAM Upgrades:** Enabled **Asynchronous Presentation**, **Extended Dynamic State 3 (`EDS3`)**, `FifoRelaxed` VSync fallback, **3 parallel shader/pipeline workers** + **2 texture transcode workers**, expanded VRAM cache budget (`3 GiB`, `1 GiB` GC floor), `128 MiB` stream buffer, and `64 MiB` VMA blocks.
  - **Dynarmic ARM64 JIT & Horizon OS Optimizations:** Built with `-O3 -fomit-frame-pointer`, **inline ARM64 hardware atomics (`LDAXR`/`STLXR`, no global spinlock)**, safe native `CNTPCT_EL0` `WallClock`, fused `LDR [Xpt, Xidx, LSL #3]` + branchless `UBFX` page-table lookups in `CPU Unsafe`, coalesced `kaiHEN` IPC range flushes, `208 MiB` JIT pool (`48 MiB`/core with automatic `144 MiB` fallback), and `svcSetThreadPriority` + adaptive 6-core pinning.
  - **GPU Upscaling & Anti-Aliasing (`[X]` / `[Y]`):** Added **AMD FSR 1.0**, **SGSR**, **ScaleForce**, **MMPX (Pixel-Art)**, **Lanczos**, **Bilinear**, and **Nearest** scaling filters, plus **FXAA** and **SMAA** anti-aliasing on Switch 2's Vulkan GPU.
  - **VSync & Speed Limit Controls + Detailed X-Ray Telemetry:** Added `VSync` (`Relaxed`, `Off/Immediate`, `Mailbox`, `FIFO`) and `Speed Limit` (`100%`, `Unlocked`, `150%`, `200%`) options in `[X]`/`[Y]`, plus live `J/I/E/S` counters in `Detailed` FPS overlay.
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
- **In-Game:** Press **`L` + `R` + `+`** to open the live **Quick Menu** · Hold **`+` + `-` (~2s)** to close the active game and return to the launcher.

---

### Known Issues
- **`.nup` packages** (Switch 1 / Switch 2) are experimental and not fully working yet.
- Large multi-GB **solid** `.nsz` files may take longer to load than `.nsp` or block-compressed `.nsz` (`nsz -C -B`).

---

### Support
Maintained by **David Kalil Braga ([@davidkalil10](https://github.com/davidkalil10))**. If you enjoy **Eden-NX2** and our Switch 2 homebrew ports, consider supporting development on Ko-fi:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/davidkalil10)

👉 **[https://ko-fi.com/davidkalil10](https://ko-fi.com/davidkalil10)**
