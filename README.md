# Savage Wind Recomp — Android

[![Build Android APK](https://github.com/deivid22srk/SavageWindRecomp-Android/actions/workflows/build.yml/badge.svg)](https://github.com/deivid22srk/SavageWindRecomp-Android/actions/workflows/build.yml)

Port Android (arm64-v8a) do [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp) —
a recompilação estática de *The Legend of Zelda: Breath of the Wild* (Nintendo Switch, versão
1.8.2) para apps nativos. Este repositório mantém praticamente todo o código original e o
compila para Android via **GitHub Actions** — o APK é gerado na nuvem, nunca localmente.

> [!WARNING]
> Port experimental de um projeto experimental. O jogo original roda a abertura e o Great
> Plateau no PC; no Android este port está no início do caminho de testes. Bugs e travamentos
> são esperados — e relatórios de teste valem ouro (abra uma issue!).

> [!IMPORTANT]
> **Nenhum arquivo do jogo é incluído.** Você precisa da sua própria cópia de BOTW atualizada à
> versão 1.8.2, extraída do seu próprio console (ExeFS + RomFS já descriptografados). O app
> nunca descriptografa nada, não precisa de `prod.keys`/`title.keys` e lê apenas os arquivos
> que você já extraiu. A recompilação do código do jogo acontece **no seu próprio dispositivo**,
> a partir dos seus arquivos.

## Estado do port

| Parte | Estado |
|---|---|
| Runtime C (kernel, dispatch, HLE, áudio, saves) | Portado (compila para Android arm64-v8a) |
| Renderizador Vulkan (NVN → Vulkan) | Portado (exige Vulkan 1.3 no aparelho) |
| Janela/entrada (SDL3) | Touch mapeado como mouse; controles Bluetooth/USB via SDL3 |
| Interface (ImGui, setup, configuração) | Portada, com seletor de pastas via SAF |
| Geração do código do jogo (tools/*.py via Chaquopy) | Integrada; depende dos wheels unicorn/capstone (veja abaixo) |
| Compilador no dispositivo (clang + lld no APK) | Integrado (experimental) |
| Player de filmes (FFmpeg) | Portado (H.264/AAC, VP8/9, Opus, Vorbis) |

## Requisitos do dispositivo

- Android 9 (API 28) ou mais novo, **arm64-v8a**
- GPU com **Vulkan 1.3** (a maioria dos SoCs flagship de 2022+: Snapdragon 8 Gen 1+, Tensor G1+,
  Dimensity 9000+)
- **Espaço de endereçamento de 48 bits** no kernel (a janela de memória do guest tem 12 GB fixos
  em 0x7000000000; aparelhos com VA de 39 bits não conseguem alocá-la)
- ~35 GB de espaço livre (cópia dos arquivos do jogo + imagem de dados + caches)
- Muita paciência na primeira execução: a geração do código do jogo demora bastante

## Como usar

1. Baixe o APK em [Releases](../../releases) (ou no artifact do Actions) e instale
   (permitir "instalar apps de fontes desconhecidas").
2. Na primeira abertura, conceda **"Todos os arquivos"** ao app (usamos um caminho real do
   sistema de arquivos para ler os arquivos que você escolher — nada é enviado para a internet).
3. Coloque seus arquivos do jogo no armazenamento do aparelho, por exemplo
   `/storage/emulated/0/SavageWind/` contendo:
   ```
   SavageWind/
     exefs/   main, main.npdm, rtld, sdk, subsdk0
     romfs/   o RomFS completo (jogo base + 1.8.2 mesclados)
   ```
4. Abra o app → **"Game files folder"** → escolha a pasta com `exefs/` e `romfs/` →
   **"Set up game files"**. O app confere a versão (1.8.2), copia os arquivos, monta a imagem de
   dados e recompila o código do jogo no aparelho. Quando terminar, o jogo inicia.
5. Nas próximas vezes o jogo abre direto. **"Quit to Start Screen"** volta à tela de
   configuração (Play, opções, apagar arquivos do jogo).

Sem `prod.keys`: o app não descriptografa nada. Todos os arquivos que ele lê já devem estar
extraídos e descriptografados (o `main` do ExeFS é um NSO já aberto).

## Limitações conhecidas

- **Geração do código no dispositivo**: o gerador (`tools/gen.py`) usa `unicorn` e `capstone`
  (emulação/desassembly ARM64). Não existem wheels Android prontos para eles; o fluxo completo
  no aparelho depende de compilá-los (scripts/experimento em `scripts/build_wheels_android.sh`,
  em preparo). Sem os wheels, o setup copia e prepara tudo, mas a etapa de recompilação falha
  com a mensagem do log. Alternativa enquanto isso: acompanhe as issues.
- **Aparelhos com VA de 39 bits** não alocam a janela do guest (mensagem clara no app).
- **Vulkan 1.3** é obrigatório (sem fallback para GLES; o backend original é Vulkan-only).
- Emulação x86 (ex.: BlueStacks) não é suportada — aparelhos físicos arm64 apenas.
- Touch controla a UI; jogar com touch dentro do jogo é bruto — use um controle.

## O que mudou do original

O código original é vendored **intacto** sempre que possível; as mudanças estão isoladas e
comentadas com `__ANDROID__`:

- `runtime/platform/android.c` (novo): camada de plataforma baseada em `linux.c` (mesma janela
  de memória guest, mesmo crash handler), com log no arquivo do app, sem re-exec (o app é
  reiniciado pelo usuário), e spawn de processos mantendo o modelo original.
- `runtime/gamebuild.c`: apenas `find_tools` ganhou ramo Android — o gerador roda via Chaquopy
  (JNI) e o compilador é um clang real embutido no APK (`SAVAGEWIND_CC`), todo o resto do fluxo
  de compilação/link (response files, jobs paralelos, logs) é o código original.
- `runtime/main.c`: a reserva do espaço guest no Android falha com mensagem clara (não há
  re-exec dentro de um app); `main` vira `sw_main` (chamado por `SDL_main`).
- `runtime/platform.h`: include do `android_compat.h` + declarações dos seletores de pasta.
- `runtime/ui/launcher.mm`: os dois pontos de diálogo (`SDL_ShowOpenFolderDialog` /
  `SDL_ShowOpenFileDialog`) usam a ponte SAF via JNI no Android.
- `app/src/main/cpp/android/`: `android_main.c` (SDL_main), `android_bridge.c` (JNI: seletores,
  dispatch do gerador), CMakeLists.txt (build do `libmain.so`).
- `app/src/main/java/`: `MainActivity` (SAF, permissões, ambiente, unpack de assets) e `Swgen`
  (Chaquopy), com as classes Java do SDL3 3.4.16.
- `scripts/build_deps_android.sh`: compila SDL3/FFmpeg/shaderc/VMA para Android e empacota o
  clang+lld do Termux como `lib*.so` (executáveis do APK no `nativeLibraryDir`).
- `app/src/main/python/`: `gen.py` e companhia **inalterados** + `gen_runner.py` (roda o gerador
  no app, escrevendo os mesmos logs de progresso que o desktop).

## Compilando

Tudo é compilado pelo [.github/workflows/build.yml](.github/workflows/build.yml) (push, PR e
`workflow_dispatch`): JDK 17, NDK r27 fixo, dependências nativas com cache, e `gradlew
assembleRelease`. Sem secrets, o APK sai assinado com a keystore de debug; com
`KEYSTORE_BASE64`/`KEYSTORE_PASSWORD`/`KEY_ALIAS`/`KEY_PASSWORD`, sai assinado com a sua chave.
Tags `v*` criam uma Release automática com o APK.

Build local completo não é suportado de propósito — use o Actions.

## Licenças e créditos

- [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp) e
  [LAS Recomp](https://github.com/peiikai/LASRecomp) — MIT (runtime, gerador, arquitetura)
- SDL3 — zlib · FFmpeg — LGPL-2.1+ (build mínimo, sem codecs GPL) ·
  shaderc/glslang/SPIRV-Tools — Apache-2.0/BSD · Dear ImGui — MIT ·
  Vulkan Memory Allocator — MIT · Fira Sans — OFL (menus) · clang/lld (Termux) — Apache-2.0/LLVM
- Nenhuma chave, firmware, arquivo de jogo ou ferramenta de descriptografia é incluído ou
  distribuído por este projeto.

---

**English summary**: Android (arm64-v8a) port of SavageWindRecomp — the static recompilation of
Breath of the Wild 1.8.2 — built entirely by GitHub Actions. The original sources are vendored
with surgical `#ifdef __ANDROID__` changes; game code is generated and compiled on-device from
your own extracted game files (no keys, nothing decrypted, nothing bundled). See the sections
above (Portuguese) for status, device requirements and usage; the workflow produces the APK as
an artifact.
