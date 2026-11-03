# Application (squelette de Goemon64Recomp adapté à GGA) — étape 6, 2026-10-19 (Sonnet)

## Ce qui a été fait
- **Décompression au lancement** : `src/game/rom_decompression.cpp` (`goemon64::decompress_gga`), port C++ de `tools/ga_rommy.py` (zlib via miniz `tinfl_decompress_mem_to_heap`, LZKN64 maison). **Test : `.\tools\test_rom_decompression.ps1`** → « OK: identical » (27 059 246 octets, identique à `baserom.us.decompressed.z64`). Le test ne compile pas l'application (clang + miniz seuls).
- **Entrée de jeu** (`src/main/main.cpp`, `supported_games`) : `rom_hash = 0xA62841FD58D33B4E` (XXH3-64 de la ROM d'origine, méthode vérifiée : elle redonne `0xDB1BC7EE0E6BEBA1` pour Goemon 1), nom interne `GOEMONS GREAT ADV`, `game_id gga.us`, `mod_game_id gga`, `has_compressed_code = true`, `save_type AllowAll` (**type de sauvegarde de GGA non identifié**).
- **RSP audio** : le microcode audio de GGA est **identique octet pour octet** à celui de Goemon 1 (0xE20 octets, ROM `0x264D0`). `aspMain.toml` (racine) → `.\RSPRecomp.exe aspMain.toml` → `rsp/aspMain.cpp` (75 Ko).
- **Squelette copié de `../Goemon64Recomp`** (sans modifier celui-ci) : `src/main`, `src/ui`, `src/game/{input,controls,config}.cpp`, `include/`, `assets/`, `icons/`, `shaders/`, `recompcontrollerdb.txt`, `CMakeLists.txt` (adapté : cible `GGARecompiled`, plus de `PatchesLib` / `patches.toml`, plus de sources d'API de mods ; `src/game/ga_modules.c` compilé avec `RecompiledFuncs`).
- **Écrits pour GGA** : `src/game/debug.cpp` (table de warps vide, quicksave vide : l'UI et `input.cpp` en ont besoin), `include/goemon_game.h`, `include/ovl_patches.hpp` (sans `register_patches`).
- **`lib/` est une jonction** vers `..\Goemon64Recomp\lib` (RT64, N64ModernRuntime, RmlUi… forks de klorfmorf, ~500 Mo, pas de copie). ⚠ Pour la supprimer : `cmd /c rmdir lib` (PAS `Remove-Item -Recurse`, qui supprimerait le contenu de la cible). Quand le projet aura son propre dépôt, remplacer par de vrais sous-modules (mêmes URL / branches que le `.gitmodules` de Goemon64Recomp, sans `lib/mnsg`).
- **Compilation** : `.\tools\build_app.ps1 [-Target RecompiledFuncs|GGARecompiled] [-Configure]` : Ninja + clang-cl de Visual Studio (`C:\Program Files\Microsoft Visual Studio\18\Community`, comme DK64), dossier `build-vs`.

## Résultat (2026-10-19)
- `.\tools\build_app.ps1` → **`build-vs\GGARecompiled.exe` (20,9 Mo), compile et lie sans erreur** ; lancé 10 s depuis la racine du projet : reste ouvert (338 Mo), rien sur stdout / stderr. **Jamais lancé avec la ROM** : le premier vrai test reste à faire.
- Pièges rencontrés (tous réglés dans `CMakeLists.txt` / `tools/build_app.ps1`) :
  - **`_m_prefetch`** : le clang-cl de VS 18 le traite comme fonction intégrée et casse l'`SDL_endian.h` de SDL2 2.26.3 embarqué dans RT64 (2 fichiers de RT64). Contournement sans toucher `lib/` : `add_compile_definitions(__PRFCHWINTRIN_H)` (le projet DK64 fait pareil).
  - **Ressources (`app.rc`, RC1107)** : il faut le `cmake` + `ninja` de Visual Studio (`...\Common7\IDE\CommonExtensions\Microsoft\CMake\`), PAS ceux de `C:\msys64\mingw64\bin`. `build_app.ps1` charge `vcvars64.bat` (qui a besoin de `vswhere.exe` : `C:\Program Files (x86)\Microsoft Visual Studio\Installer` ajouté au PATH).
  - `src/ui/ui_api_events.cpp` inclut `../patches/ui_funcs.h` : copie de `patch_helpers.h`, `ui_funcs.h`, `recompui_event_structs.h` dans `patches/` (3 en-têtes seulement ; le reste de `patches/` de Goemon 1 n'est pas repris).
  - **17 symboles non résolus à la liaison** (`__osCheckId`, `__osCheckPackId`, `__osContAddressCrc`, `__osContRamRead/Write`, `__osEnqueueThread`, `__osGetId`, `__osPfsGetStatus`, `__osPfsRWInode`, `__osPfsSelectBank`, `__osPopThread`, `__osRepairPackId`, `__osSiGetAccess/RelAccess`, `__osTimerInterrupt`, `__osViGetCurrentContext`, `__osViSwapContext`, tous `_recomp`) : stubs vides dans `src/game/unknown_symbols.cpp` (liste mesurée, pas copiée de Goemon 1). Ce sont des internes de libultra (Controller Pak, file de threads, Vi / Si) : si le jeu en dépend vraiment, un stub vide cassera quelque chose, à vérifier au premier lancement.
- `git` : rien n'a été committé. Fichiers nouveaux / copiés à surveiller : `src/`, `include/`, `assets/`, `icons/`, `shaders/`, `CMakeLists.txt`, `aspMain.toml`, `rsp/aspMain.cpp` (généré, ~75 Ko), `patches/` ; `lib/` (jonction) et `build-vs/` sont ignorés ou à ignorer (`lib` n'est PAS dans `.gitignore` : à ajouter avant tout `git add`).

## Volontairement NON porté (à faire plus tard)
- **Mods** : pas de `patches/` (RECOMP_PATCH), pas d'API pour les mods (`recomp_get_*`, `recomputil_*`, acteurs étendus). Le CMake n'a plus `PatchesLib`. Les hooks du chargeur / des modules passent par `[[patches.hook]]` dans `ga.toml` + `src/game/ga_modules.c`.
- `src/game/scene_table.cpp`, `recomp_api.cpp`, `recomp_actor_api.cpp`, `recomp_data_api.cpp`, `unknown_symbols.cpp` de Goemon 1 : non copiés. `unknown_symbols.cpp` définissait des fonctions `*_recomp` vides pour des fonctions libultra que le runtime ne fournit pas : **à refaire selon les erreurs de liaison** de GGA.
- Noms de threads (`get_game_thread_name` dans `main.cpp`) : ceux de Goemon 1 (ids / priorités), faux pour GGA.
- Le namespace C++ reste `goemon64` (moins de changements dans le code copié).

## Pièges
- `rom_decompression.cpp` : la sortie n'a PAS d'alignement sur 16 octets entre les fichiers (contrairement à Goemon 1), elle copie la ROM d'origine avant le premier fichier et après le marqueur de fin ; les CRC d'en-tête ne sont pas modifiés.
- Un octet de trop après certains flux zlib est normal (on ne vérifie pas que l'entrée est consommée en entier).
