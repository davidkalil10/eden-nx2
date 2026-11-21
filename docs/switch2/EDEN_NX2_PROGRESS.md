# Eden-NX2 — Nintendo Switch 2 Homebrew Porting Tracker & Engineering Log

> **For any AI or developer resuming this project:**
> 1. Read **[SWITCH2_PORTING_BIBLE.md](./SWITCH2_PORTING_BIBLE.md)** first — it contains every hard-won rule for Nintendo Switch 2 (`ounce-devkit`, `libounce`, `OunceVK`, `kaiHEN`, `OUN0`, CI Docker recipes, and pitfalls).
> 2. Read this file (**`EDEN_NX2_PROGRESS.md`**) to see the current phase, architectural decisions, exact file map, and next actionable step.
> 3. **Always update this file** after completing a phase, fixing a build/runtime issue, or making a new architectural decision.

---

## 1. Project Identity

| Field | Value |
| :--- | :--- |
| **Project** | `eden-nx2` (Nintendo Switch 2 homebrew port of [Eden Emulator](https://git.eden-emu.dev/eden-emu/eden)) |
| **GitHub Repo** | `https://github.com/davidkalil10/eden-nx2` (clean single branch: `master`, 0 upstream tags) |
| **Local Workspace** | `D:\Outros projetos\eden_nx2` |
| **Upstream Baseline Commit** | `56e628ad1d82ade16f2c5ac8797749fd2be30467` (`master`) |
| **Port Maintainer** | **David Kalil Braga (`@davidkalil10`)** — Ko-fi: `https://ko-fi.com/davidkalil10` |
| **Target Platform** | Nintendo Switch 2 (`aarch64-none-elf`, `-mcpu=cortex-a78c+crypto`, `OUN0` NRO, `ounce-devkit` `20261115b`) |
| **Graphics / JIT Stack** | **OunceVK** (static Vulkan 1.4 + `VK_NN_vi_surface`) + **kaiHEN** (`jitCreate` / `kaihenJitPublishRanges`) |
| **Current Phase** | **Phase 5 In Progress (Real Switch 2 Hardware Bring-Up & Memory/OunceVK Tuning)** |

---

## 2. Phase 0 Audit Findings (Verified in Source Code)

Every major Switch 2 architectural requirement was audited directly in the `eden-nx2` codebase:

1. **In-Tree Dynarmic (`src/dynarmic/`) + Dual-Pointer Codegen Ready for `kaiHEN`:**
   - `src/dynarmic/src/dynarmic/backend/arm64/address_space.cpp` uses `oaknut::CodeGenerator code(rw_ptr, rx_ptr)` (line 32) and calls `mem.invalidate(reinterpret_cast<u32*>(block_info.entry_point), block_info.size)` after every emitted block (line 134).
   - **Implication:** Adapting `oaknut::CodeBlock` (`mem`) to a single shared `kaiHEN` JIT pool (`jitCreate` → separate `rw_addr` and `rx_addr`) and publishing exact `(offset, size)` ranges via `kaihenJitPublishRanges` inside `mem.invalidate(...)` is clean and surgical.
   - **Critical Rule (from `ppsspp-nx2` & `Vita3K-nx2`):** Never map/publish multi-megabyte ranges unnecessarily, and either disable dynamic block relinking (`RelinkForDescriptor`) or publish the exact patched 4-byte branch instruction on `kaiHEN`.

2. **Software Page-Table Fallback Already Exists (`src/common/host_memory.cpp`):**
   - `src/common/host_memory.cpp:711` already has a `#if defined(__OPENORBIS__) || defined(__managarm__)` non-fastmem path (`virtual_base = nullptr`), where `Map`, `Unmap`, `Protect`, and `EnableDirectMappedAddress` become clean no-ops!
   - **Implication:** On `__SWITCH2__`, we allocate `backing_base` via `memalign(0x1000, backing_size)` (no `mmap`/`mprotect`/`SIGSEGV`), and Eden's `Core::Memory::Memory` software page table works out of the box!

3. **Static Vulkan 1.4 (`OunceVK`) Integration is Surgical:**
   - `src/video_core/vulkan_common/vulkan_instance.cpp:123` resolves only `vkGetInstanceProcAddr` from `DynamicLibrary`. On `__SWITCH2__`, assign `dld.vkGetInstanceProcAddr = &::vkGetInstanceProcAddr;` directly (statically linked to `libouncevk.a`, zero `dlopen`).
   - `src/video_core/vulkan_common/vulkan_surface.cpp` creates the `VkSurfaceKHR`. On `__SWITCH2__`, add a `WindowSystemType::Switch2` (or `VI`) branch calling `vkCreateViSurfaceNN` with `nwindowGetDefault()`.

4. **Non-Qt Frontend + Modern Build System:**
   - `src/yuzu_cmd/` provides a standalone non-Qt SDL3 frontend (`ENABLE_QT=OFF`, `YUZU_CMD=ON`).
   - `CMakeLists.txt` already supports disabling desktop-only features cleanly:
     - `ENABLE_QT=OFF`
     - `ENABLE_CUBEB=OFF` (use SDL3 audio sink!)
     - `ENABLE_LIBUSB=OFF`
     - `ENABLE_WEB_SERVICE=OFF`
     - `USE_DISCORD_PRESENCE=OFF`
     - `ENABLE_RESHADE=OFF`
     - `ENABLE_OPENSSL=OFF` / `YUZU_STATIC_BUILD=ON`
     - `BUILD_TESTING=OFF`
     - `ENABLE_WERROR=OFF` (for cross-compile bring-up)
   - Dependencies are managed via **CPM** (`cpmfile.json`) rather than fragile nested git submodules.
   - **CI Note:** `CMakeLists.txt` requires `cmake_minimum_required(VERSION 3.31)` — our CI workflow must install CMake 3.31+ inside the `devkitpro/devkita64` container.

5. **Real Switch 1 vs. Switch 2 Hardware Scope:**
   - Target is **Switch 1 emulation** (`prod.keys` + Switch 1 firmware + NSP/XCI/NRO dumps) running natively on Switch 2 hardware (~9 GB usable RAM comfortably fits Switch 1's 4 GB guest RAM + Vulkan caches + Dynarmic JIT pool).

---

## 3. Architecture Decision Records (ADRs)

| ID | Decision | Rationale |
| :--- | :--- | :--- |
| **ADR-001** | **CPU:** `Dynarmic` ARM64 JIT over `kaiHEN` (no NCE, no fastmem/SIGSEGV) | Switch 2 userland (`kaiHEN`) has no `mmap`/`mprotect`/`SIGSEGV` or custom SVC trap; Dynarmic + software page table is proven and safe. |
| **ADR-002** | **Memory:** `HostMemory` non-fastmem path using `memalign(0x1000, backing_size)` | Avoids `mmap` (`errno=ENOSYS`/crash on libnx) and reuses Eden's existing `virtual_base == nullptr` software page table path. |
| **ADR-003** | **Graphics:** `OunceVK` static Vulkan 1.4 + `VK_NN_vi_surface` | Native Vulkan 1.4 on Switch 2 GPU (`libouncevk.a`), zero Mesa/GL overhead, proven in `Vita3K-nx2` and `ppsspp-nx2`. |
| **ADR-004** | **Frontend:** Start with `yuzu_cmd` (SDL3) → add minimal ImGui ROM/Keys/Firmware selector | Qt6 does not exist in Switch 2 portlibs. `yuzu_cmd` gets us to first boot fastest; ImGui overlay on SDL3/Vulkan gives a clean controller-driven UI. |
| **ADR-005** | **Audio:** SDL3 audio backend (`ENABLE_CUBEB=OFF`, `ENABLE_SDL3=ON`) | `libnx` + Switch 2 SDL3 audio works cleanly without porting `cubeb`/`oboe`. |
| **ADR-006** | **Build & CI:** GitHub Actions (`devkitpro/devkita64` + `ounce-devkit` `20261115b` + CMake 3.31+) | Keeps local disk clean, reproducible builds, automated `OUN0` + `kaiHEN` + Switch 1 symbol verification (`verify_switch2_package.py`). |

---

## 4. Phased Porting Roadmap & Status

### Phase 0 — Repository Setup & Architecture Audit
- [x] Import `eden-emu/eden` into `davidkalil10/eden-nx2`
- [x] Fix default branch to `master` (`56e628ad`), delete 155 extraneous branches and 26 upstream tags
- [x] Clone clean single-branch repo to `D:\Outros projetos\eden_nx2`
- [x] Copy [SWITCH2_PORTING_BIBLE.md](./SWITCH2_PORTING_BIBLE.md) into `docs/switch2/`
- [x] Audit `CMakeLists.txt`, `cpmfile.json`, `host_memory.cpp`, `dynarmic/backend/arm64/address_space.cpp`, `vulkan_library.cpp`, `vulkan_surface.cpp`
- [x] Create living tracker `docs/switch2/EDEN_NX2_PROGRESS.md`

### Phase 1 — Switch 2 Toolchain, CMake Platform Support & First CI Build (`common` + `core` + `dynarmic`)
- [x] Add `SWITCH2` / `__SWITCH2__` detection to CMake platform/architecture modules (`CMakeLists.txt`, `externals/cmake-modules/DetectPlatform.cmake`, `CompilerChecks.cmake`)
- [x] Create `scripts/build-switch2.sh`, `tools/verify_switch2_package.py`, and `.github/workflows/build-switch2.yml` (CMake 3.31.6 + `ounce-devkit` `20261115b` + Switch portlibs + CPM cache)
- [x] Port `src/common/host_memory.cpp` to Switch 2 non-fastmem `memalign` backing buffer
- [x] Port POSIX/OS shims in `src/common/` for `libnx`/`newlib` (`host_memory.cpp`, `dynamic_library.cpp`, `fs/file.cpp`, `fs/path_util.cpp`, `sparse_large_vector`, `multi_level_page_table.inc`, `signal_chain` skip, `thread.cpp`, `sys/resource.h`) — **`libcommon.a` compiled 100% green!**
- [x] Get `dynarmic` + `shader_recompiler` + `core` compiling cleanly in GitHub Actions

### Phase 2 — `kaiHEN` ARM64 JIT Backend in `Dynarmic`
- [x] Implement `Switch2JitPool` (`jitCreate` single shared 96 MiB pool, `Alloc`/`Free`/`Publish`/`Shutdown`) in `src/dynarmic/src/dynarmic/backend/arm64/switch2_code_block.h/.cpp`
- [x] Wire `Switch2CodeBlock` + `Dynarmic::Backend::Arm64::AddressSpace` to separate `(rw_ptr, rx_ptr)` and `kaihenJitPublishRanges` in `mem.invalidate(...)` (plus prelude-only invalidation in `a64_address_space.cpp` and `a32_address_space.cpp`)
- [x] Guard `exception_handler` to `backend/exception_handler_generic.cpp` on `SWITCH2`
- [ ] Verify `RelinkForDescriptor` behavior under `kaiHEN` on real Switch 2 hardware

### Phase 3 — `OunceVK` Static Vulkan 1.4 & `VK_NN_vi_surface`
- [x] Define `VK_USE_PLATFORM_VI_NN` instead of X11/Wayland in `src/video_core/vulkan_common/vulkan.h`
- [x] Wire `vulkan_instance.cpp` to static `&::vkGetInstanceProcAddr` on `__SWITCH2__`
- [x] Add `WindowSystemType::Switch2` (`VK_NN_vi_surface` / `vkCreateViSurfaceNN`) in `emu_window.h`, `vulkan_instance.cpp`, and `vulkan_surface.cpp`
- [ ] Verify full `video_core` + `yuzu-cmd` link against `libvulkan.a` (`OunceVK`) without duplicate symbols

### Phase 4 — Minimal Switch 2 Frontend (`eden-switch2.nro`)
- [x] Adapt `src/yuzu_cmd/` (`emu_window_sdl3_vk.cpp` `nwindowGetDefault()`, `yuzu.cpp` Vulkan + SD `roms/` scan + `Switch2JitPool::Shutdown()`, `CMakeLists.txt` `WIN32` guard on `yuzu.rc`)
- [ ] Add minimal controller-friendly ROM picker + status/log screen (so it boots cleanly even before a game is selected)
- [x] Wire `eden-switch2.nro` packaging (`nacptool` + `elf2nro` + `verify_switch2_package.py`) and CI artifact upload in `scripts/build-switch2.sh` & `.github/workflows/build-switch2.yml`

### Phase 5 — Real Switch 2 Hardware Bring-Up
- [ ] Boot `eden-switch2.nro` on real Switch 2 (verify launcher + `OunceVK` device enumeration + clean exit to `hbmenu`)
- [ ] Boot a lightweight Switch 1 homebrew `.nro` (e.g., simple 2D/3D test) to validate `Dynarmic` (`kaiHEN`) + `HostMemory` + `OunceVK`
- [ ] Boot a commercial Switch 1 title with `prod.keys` + firmware and tune RAM budget / shader cache / performance

---

## 5. Session & Progress Log

| Date | Milestone | Commit / CI | Notes |
| :--- | :--- | :--- | :--- |
| **2026-11-20** | **Phase 0 Complete** | `6bb33b3d` | Repo cleaned (1 branch `master`, 0 tags), Switch 2 Bible copied to `docs/switch2/`, full audit of `HostMemory`, `Dynarmic/oaknut`, `Vulkan`, and `CMakeLists.txt` completed. |
| **2026-11-20** | **Phase 1 Started (CI Bring-Up #1–#11)** | `ff64743c`..`43a2cb2c` | Full CMake configure green (`devkitPro/SDL3` + `glslang-tools`), **OpenSSL 3.6.2** (`libcrypto.a`/`libssl.a`), **Boost 1.92** (`boost_container` `HAVE_MMAP=0`, `BOOST_HAS_PTHREADS=1`, `sys/resource.h` shim for `boost_context`), `jacinth`, and `hydrocarbon` all compiled green for Switch 2. |
| **2026-11-20** | **Phase 1 `src/common/` Complete!** | `32f6f4e4` ([Run #44736441818](https://github.com/davidkalil10/eden-nx2/actions/runs/44736441818)) | **`libcommon.a` compiled 100% green** for Switch 2 (`file.cpp` `fopen`, `signal_chain` skipped, `thread.cpp` priority/name guarded). Advancing CI target to `dynarmic shader_recompiler core`. |
| **2026-11-20** | **Phase 2 `kaiHEN` JIT Wired in `Dynarmic`** | `f33bf253` | Disabled `boost_process` (`debugger.cpp` stubbed on `__SWITCH2__`), implemented `Switch2CodeBlock` + `Switch2JitPool` (96 MiB shared `kaiHEN` pool + `kaihenJitPublishRanges`) replacing `oaknut::CodeBlock` (`sys/mman.h`) across `AddressSpace` / `A64AddressSpace` / `A32AddressSpace` / `spin_lock_arm64.cpp`. |
| **2026-11-20** | **`core` / `audio_core` / `hid_core` / `network` Passed! Phase 3 `OunceVK` Wired** | `d4ef421a` ([Run #44759350095](https://github.com/davidkalil10/eden-nx2/actions/runs/44759350095)) | All core emulator libraries (`common`, `dynarmic`, `shader_recompiler`, `video_core`, `audio_core`, `hid_core`, `network`, `core`) compiled **100% green** in ~33m! |
| **2026-11-21** | **First `eden-switch2.nro` Built & Verified Green!** | `2fb0d19f` ([Run #44899545405](https://github.com/davidkalil10/eden-nx2/actions/runs/44899545405)) | Full `yuzu-cmd` -> `eden-cli.elf` -> `dist/eden-switch2.nro` (`OUN0` + `kaiHEN`) built & verified green with `ccache` + SDL3 dummy video! |
| **2026-11-21** | **Phase 5: Boot Trace + Demand-Paged `MultiLevelPageTable` + 38-bit AS + OunceVK Fixes** | `d36ba550` | Fixed `MultiLevelPageTable` (demand-paged `AllocateLevel`), compacted 39-bit process AS to 38-bit in `KPageTableBase`, capped `OunceVK` VMA blocks (32 MiB) & VRAM budget (2 GiB), added `VK_FORMAT_R8G8B8A8` swapchain view support, and added `boot_trace.txt` breadcrumbs. |
| **2026-11-21** | **Phase 5: Native Switch 2 `main()` + `EmuWindowSwitch2` + NRO Icon** | pending push | Hardware boot trace stopped at `02: Log started` inside desktop `ParseLaunchParams`/`SdlConfig`/`SDL_MAIN_USE_CALLBACKS` flow. Replaced Switch 2 entrypoint in `yuzu.cpp` with native `int main()` + `EmuWindowSwitch2` (`nwindowGetDefault()`), moved Horizon runtime helpers to `switch2_runtime.cpp`, and wired 256×256 `dist/eden_profile.jpg` icon into `elf2nro`. |
