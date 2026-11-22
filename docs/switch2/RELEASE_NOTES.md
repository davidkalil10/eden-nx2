## Eden-NX2 — Native Nintendo Switch 2 Port of Eden

**Eden-NX2** is a native **Nintendo Switch 2** (`aarch64` / `OUN0` NRO) homebrew port of the **Eden** Nintendo Switch 1 emulator, built on **`ounce-devkit`**, **`OunceVK`** (static Vulkan 1.4), and **`kaiHEN`** dual-mapped ARM64 JIT (`Dynarmic`).

> **Português (BR):** O **Eden-NX2** permite rodar jogos de **Nintendo Switch 1** nativamente como homebrew no **Nintendo Switch 2** (`eden-switch2.nro`), com renderização **Vulkan 1.4 (`OunceVK`)**, **JIT ARM64 (`kaiHEN`)**, launcher gráfico bilíngue (PT/EN) em 1280×720, gerenciador de Updates/DLCs e suporte a geração de frames via **Lossless Scaling (`LSFG`)**.

---

### What's New / Novidades
- **Supported Game Formats:** `.nsp`, `.nsz` (block & solid compressed), `.xci`, `.xcz`, `.nca`, and `.nro`.
- **Redesigned Per-Game Settings (`[Y]`) & Update / DLC Manager:**
  - Dedicated **Update Picker** with full readable filenames, sizes, and built-in **SD card file browser**.
  - Dedicated **Multi-Select DLC Manager** (`[X]` checkboxes) supporting multiple DLC files and/or folders per game.
  - Per-game `.ini` overrides (`sdmc:/switch/eden-nx2/games/`).
- **Lossless Scaling Frame Generation (`LSFG` 2x / 3x / 4x):**
  - Fixed Vulkan `shaderFloat16` handling on Switch 2 (`T239` Ampere) and Switch 2 frame-gen pacing.
  - Place your `Lossless.dll` at `sdmc:/switch/eden-nx2/Lossless.dll` (status badge shows **`LSFG OK`** in the launcher).
  - On-screen FPS overlay displays `base -> generated FPS` when LSFG is active.
- **1280×720 Bilingual Graphical Launcher (PT / EN):**
  - Toggle language anytime with **`-`**, global settings with **`X`**, per-game settings with **`Y`**, touch navigation, and live controller/gyro tester.
  - Hold **`+` + `-` (~2 seconds)** in-game to return cleanly to the **Eden-NX2 Launcher**.
  - Automatic return to launcher if a package fails to load.
- **Performance & Compatibility:**
  - Resolution scaling (`0.5x`–`2.0x`), Handheld/Docked profiles, `GPU Fast` / `CPU Unsafe` / `CPU Boost` toggles, disk shader cache, and controller profiles (`Pro Controller`, `Handheld` — recommended for *Pokémon: Let's Go*, `Dual Joy-Con`, `Right/Left Joy-Con`).

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
- **Launcher:** `D-Pad` / Touch to browse · **`A`** Launch / Open · **`B`** Parent folder · **`X`** Global Settings · **`Y`** Per-Game Settings (Update / DLC / Overrides) · **`-`** PT/EN · **`+`** Exit
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
