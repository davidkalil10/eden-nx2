# Eden-NX2 — Roadmap de Futuras Features (Nintendo Switch 2)

> Documento vivo de planejamento para o port nativo **Eden-NX2** (`aarch64` / `OUN0` NRO no Nintendo Switch 2).  
> Mantido por **David Kalil Braga ([@davidkalil10](https://github.com/davidkalil10))**.

---

## Já Implementado (até `v0.1.6`)
- [x] **Launcher gráfico 1280×720 bilíngue (PT/EN)** com navegador de pastas SD, ícones de jogos, perfis globais (`[X]`) e por jogo (`[Y]`), toque na tela e testador de controle/giroscópio ao vivo.
- [x] **Auto-Updater via GitHub Releases (`[L]`)** com barra de progresso, validação `NRO0`/`OUN0` e reinicialização automática.
- [x] **Gerenciador de Updates e DLCs por jogo (`[Y]`)** com seletor dedicado, multi-seleção de DLCs (`[X]`) e navegador de arquivos no SD.
- [x] **Menu Rápido Ao Vivo dentro do Jogo (`L + R + +`)** via overlay Vulkan centralizado (`OverlayAnchor::Center`), alterando ao vivo: Speed Limit, VSync, Upscaler, Anti-Aliasing, Docked/Handheld, Precisão GPU, Clock FastLoad, FPS Overlay, Volume, Salvar Perfil e Voltar ao Launcher.
- [x] **Motor de Performance Switch 2:** Dynarmic ARM64 com atômicos inline (`LDAXR`/`STLXR`), `CNTPCT_EL0` nativo sem trap EL0, page-table fundido, `208 MiB` JIT pool + coalescing de `kaiHEN`, `3 GiB` VRAM cache, `EDS3`, Async Presentation, 3 pipeline workers + 2 texture transcode workers, afinidade nos 6 núcleos (`0x3f`) e correção de deadlock no `CoreTiming::Advance()` com `Speed Limit = Off`.
- [x] **Upscaling GPU & Pós-processamento:** `AMD FSR 1.0`, `SGSR`, `ScaleForce`, `MMPX`, `Lanczos`, `Bilinear`, `Nearest`, `FXAA`, `SMAA` e **Lossless Scaling (`LSFG` 2x/3x/4x)**.

---

## Fase 1 — Prioridade Alta (Armazenamento & Controles) ✅ Concluída em `v0.1.7`

### 1.1 Suporte Nativo a ROMs, Updates e DLCs Splitados (FAT32 > 4 GB)
- [x] **Leitura contínua (streaming sob demanda)** via `FileSys::ConcatenatedVfsFile` sem precisar juntar arquivos no cartão SD e sem consumo extra de RAM.
- [x] **Formatos suportados automaticamente** (tanto para jogos base quanto para Updates e DLCs):
  - Pasta splitada padrão Switch/FAT32 (`SAK` / `splitNSP`): `NomeDoJogo.nsp/` (ou `.xci/`, `.nsz/`, `.xcz/`, ou pasta sem extensão) contendo `00`, `01`, `02`, ...
  - Arquivos numerados soltos com base 0: `NomeDoJogo.nsp.00`, `.01`, `.02`, ... e `NomeDoJogo.xc0`, `.xc1` / `.ns0`, `.ns1`.
  - Arquivos divididos pelo **7-Zip** (base 1): `NomeDoJogo.nsp.001`, `.002`, `.003`, ...
- [x] **Integração no Launcher (`switch2_launcher.cpp`):** exibir jogos splitados como uma única entrada limpa (com tamanho total somado das partes e badge `SPLIT`), sem abrir a pasta de pedaços como subdiretório comum.

### 1.2 Multiplayer Local (Player 1 a Player 4) + Joy-Cons Individuais
- [x] Expandido `switch2_runtime.cpp` de `padConfigureInput(1, ...)` para **`padConfigureInput(4, ...)`**.
- [x] **Auto-detecção do estilo físico no Switch 2 (`Auto`):** lê `padGetStyleSet` de cada controle pareado no Horizon OS (`Pro Controller`, `Dual Joy-Con`, `Joy-Con Esquerdo`, `Joy-Con Direito`, `Handheld`) com hot-plug em tempo real.
- [x] **Giroscópio, acelerômetro e HD Rumble independentes** por jogador (`NpadIdType::Player1..Player4`).
- [x] **Configuração de P1–P4** por jogo (`[Y]`) e **P1/P2 ao vivo no Menu Rápido (`L + R + +`)**, mais indicadores `P1..P4` ao vivo no launcher.
- [ ] **Controller Applet HLE/Overlay:** melhoria futura para jogos que exigem tela interativa do Controller Applet.

---

## Fase 2 — Qualidade Visual, Performance Extra & Ajustes Avançados do Eden

### 2.1 Recursos Gráficos já existentes no Eden (`settings.h`)
- [ ] **Filtragem Anisotrópica (`max_anisotropy`):** `Auto / 2x / 4x / 8x / 16x` (muito mais nitidez em pisos/texturas distantes praticamente sem perda de FPS no Switch 2).
- [ ] **Nitidez do FSR (`fsr_sharpening_slider`):** controle ao vivo (`0%` a `100%`) no Menu Rápido quando `AMD FSR 1.0` estiver ativo.
- [ ] **NVDEC na GPU (`nvdec_emulation`):** alternar entre `GPU` e `CPU` (cutscenes/vídeos mais fluidos).
- [ ] **GPU Unswizzle (`gpu_unswizzle_enabled`):** mover unswizzle de texturas da CPU para a GPU (testar ganho de FPS em títulos pesados).
- [ ] **ASTC Decode & Recompression (`accelerate_astc`, `astc_recompression`):** opções `GPU / CPU` e `Uncompressed / BC1 / BC3` para controle de VRAM e compatibilidade.
- [ ] **Aspect Ratio (`aspect_ratio`):** `16:9`, `4:3`, `Stretch`, `21:9` (útil para coletâneas retrô e mods widescreen).
- [ ] **Shaders de Pós-Processamento (`post_shader_enabled` / `post_shader_preset`):** presets extras (sharpen, cores vibrantes, CRT/scanlines para 2D).

### 2.2 Tuning Avançado de CPU / Timing
- [ ] **Clock Virtual da CPU (`cpu_clock`):** `Normal (1020 MHz)` / `Boost (1734 MHz)` / `Overclock (2040 MHz)` — ajuda jogos com resolução dinâmica baseada no clock virtual.
- [ ] **Frame Pacing Mode (`frame_pacing_mode`):** `Auto / 30 / 60 / 120`.
- [ ] **LSFG Flow Scale & Target Rate (`frame_gen_flow_scale`, `frame_gen_target_rate`):** ajustes finos de qualidade vs latência do Lossless Scaling.

---

## Fase 3 — Ecossistema Switch 1 (Mods, Cheats, Amiibo, Saves & Teclado)

### 3.1 Gerenciador de Mods & Cheats (LayeredFS / ExeFS / IPS / `.txt`)
- [ ] Tela dedicada por jogo (em `[Y]` e/ou Menu Rápido) para listar e ativar/desativar **mods** (patches 60 FPS, traduções PT-BR, texturas HD) e **cheats** na pasta `load/<TitleID>/` usando `disabled_addons`.
- [ ] Toggle rápido de **Cheats** ao vivo no Menu Rápido (`L + R + +`).

### 3.2 Emulação de Amiibo (`.bin` no SD)
- [ ] Opção **`[ Escanear Amiibo... ]`** no Menu Rápido (`L + R + +`) lendo arquivos `.bin` de `sdmc:/switch/eden-nx2/amiibo/` e enviando direto ao serviço NFC (`Cabinet`) do jogo ativo.

### 3.3 Teclado Nativo do Switch 2 (`swkbd`), Perfil de Usuário & Gerenciador de Firmware ✅ Concluída em `v0.1.11`–`v0.1.16`
- [x] Conectado o `SoftwareKeyboardApplet` (`swkbd`) do Eden ao **teclado nativo do Horizon OS (`libnx swkbdShow`)**, com **fallback automático para Teclado Virtual QWERTY integrado** (controle + toque na tela) e opção **`Teclado no Jogo`** (`Auto` / `Teclado Integrado`) em `[X]`/`[Y]`.
- [x] Editor de **Nome do Perfil** (gravado no `ProfileManager` da conta Switch 1) nas Configurações Globais (`[X]`) do launcher.
- [x] **Gerenciador e Instalador de Firmware Switch 1** (`v0.1.16`) em `[X]` (`Firmware Switch 1`), instalando direto de `.zip` ou pastas com `.nca` em `sdmc:/switch/eden-nx2/firmware/` (ou via navegador SD) com barra de progresso ao vivo e opção de remover/reverter para sintetizado.
- [x] **Modos de Compatibilidade NVK** (`v0.1.12`–`v0.1.15`): `Safe (Compat. NVK)` (destravou *Super Mario Party Jamboree*), `Ultra-Safe (NAK Serial)`, `VK_EXT_robustness2` ativo no Switch 2 e diagnóstico completo de `VK_ERROR_DEVICE_LOST` / `crash_gpu.txt`.

### 3.4 Gerenciador de Saves (Backup / Importar / Exportar)
- [ ] Opção no `[Y]` para **exportar** o save do jogo selecionado para `sdmc:/switch/eden-nx2/saves_backup/<Jogo>/` e **importar** saves vindos de um Switch 1 (JKSV / Checkpoint) ou do Eden no PC.

---

## Fase 4 — Recursos Avançados & Experimentais

### 4.1 Multiplayer Local Wireless / Rede (LDN / Salas Eden)
- [ ] Habilitar emulação de **LDN** via Wi-Fi do Switch 2 para multiplayer local com outros consoles ou PCs rodando Eden (*Mario Kart 8*, *Pokémon*, *Monster Hunter*, etc.).

### 4.2 Diagnóstico & Qualidade de Vida no Launcher
- [ ] **Busca / Filtro rápido** na lista de jogos e aba de **Favoritos / Recentes**.
- [ ] **Captura de tela (Screenshot)** direto pelo Menu Rápido salvando PNG em `sdmc:/switch/eden-nx2/screenshots/`.
- [ ] **Exibição de temperatura/bateria/horário** na barra superior do launcher e no Menu Rápido.
- [ ] **Suporte aprimorado a `.nup`** (investigação de compatibilidade futura).
