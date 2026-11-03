# Goemon's Great Adventure (US) — recompilation statique N64

Projet : recompiler statiquement « Ganbare Goemon: Derodero Douchuu Obake Tenkomori » / Goemon's Great Adventure (US, `NGME`) avec N64Recomp + RT64,
en prenant le dépôt `../Goemon64Recomp` (Mystical Ninja 1, klorfmorf) comme squelette d'application (UI, runtime, RT64, mods).
La partie propre à Goemon 1 (`mnsg.toml`, symboles, `patches/`, `src/game`) doit être remplacée par celle de GGA.

## Règles de travail
- Parler en français, tutoyer l'utilisateur. L'utilisateur débute en recompilation : expliquer le « pourquoi » de chaque étape.
- **Ne jamais lancer de commande git** (commit, push, submodule, etc.). L'utilisateur fait tout le git lui-même.
- Ne pas modifier `../Goemon64Recomp` ni `../dk64recomp` (références en lecture seule).
- Ne jamais réécrire un fichier entier par script quand `Edit` suffit.
- Noter tout fait établi dans `notes/` (mesuré vs supposé, avec la date). **Lire `notes/REPRISE.md` en premier à chaque session** (état actuel, prochaine tâche, comment lancer / régénérer), puis au besoin `notes/OVERLAYS.md`, `notes/APPLICATION.md`, `notes/HANDOFF.md` (historique), `notes/ROM_NOTES.md`.
- Mettre à jour `notes/HANDOFF.md` en fin de session (fait / à faire / pièges).
- Modèle : dire à l'utilisateur quand changer. **Opus** : reverse engineering, diagnostic d'un bug inconnu, choix d'architecture (overlays, relocations, décompression à l'exécution).
  **Sonnet** : tâches mécaniques au plan déjà écrit (scripts d'extraction, remplir un tableau, renommer, adapter un fichier copié de Goemon 1, mettre à jour les notes). Marquer le modèle conseillé sur chaque tâche de `HANDOFF.md`.

## Environnement (Windows 11)
- PowerShell. Python 3.14 (stdlib seule pour nos outils), CMake 3.31, clang 22.1.8 (`C:\msys64\mingw64\bin`, cible `mips` OK), `ld.lld` (`C:\Program Files\LLVM\bin`), VS 2022 + x64 Native Tools.
- `N64Recomp.exe` / `RSPRecomp.exe` : copiés à la racine du projet (ceux de `../dk64recomp`, compatibles : chaîne complète OK le 2026-10-19).
- `ref/mnsg.us.z64` : ROM de Goemon 1 (US) fournie par l'utilisateur, référence pour `tools/match_libultra.py` (jamais committée, `ref/` est dans `.gitignore`).
- `git` n'est pas dans le PATH de la session de l'assistant.
- Ghidra : pas installé (à ce jour).
- Python 3.12.10 installé (le 2026-10-19, `py -3.12`) + venv `.venv` (racine du projet) avec splat : `.\.venv\Scripts\python.exe -m splat ...`. Python 3.14 reste celui par défaut pour nos scripts stdlib (`ga_rommy.py`).
- Désassembleur sans installation : `llvm-objdump` / `llvm-objcopy` (`C:\Program Files\LLVM\bin`, aussi dans `C:\msys64\mingw64\bin`, cible mips) : présent (constaté le 2026-10-19), à préférer à capstone ; désassemblage de binaire brut pas encore essayé.

## Contenu
- `baserom.us.z64` : ROM d'origine (SHA1 9F9E860816F9E2A68BB5F4F56086E0C7450C64D7). Ne jamais la modifier ni la committer.
- `baserom.us.decompressed.z64` : sortie de `tools/ga_rommy.py` (voir `notes/ROM_NOTES.md`).
- `config/rommy.json` : manifeste de la décompression.

Controller Pak / sauvegarde : fait et testé le 2026-10-20 (voir REPRISE.md, section « Controller Pak »).

Chantier en cours : mode 3P / 4P « absolu »
Objectif : les joueurs 3 et 4 doivent être de vrais joueurs, créés comme J1 et J2 au lancement d'un niveau, sans la manip C-droite + START, et sans disparaître à un changement de zone ou de niveau (vies, pièces, armure, arme conservées comme pour J1/J2). Bonus plus tard : un HUD pour J3 et J4 avec les mêmes données que J1/J2.
Déclencheur choisi (provisoire) : automatique, d'après le nombre de manettes physiques branchées (on ne crée que des joueurs qui ont une manette). Un choix « 3P / 4P » dans le menu 1P/2P viendra après.
État détaillé, faits mesurés et prochaines étapes : notes/REPRISE.md, section « Mode 3 et 4 joueurs ».
ÉTAT AU 2026-10-21 : build OK. Fait et testé : sauvegarde Controller Pak, J3/J4 rejoignent comme J2, écran large 16:9 (3D, voile de pause, panorama), HUD 3P/4P aux 4 coins (blocs d'origine 1:1, J2/J4 en miroir, carrés d'objets) validé, mur entre les joueurs élargi en 16:9 validé, apparition des objets en 16:9 (niveau 1) et effet d'eau (copie GPU) validés. L'utilisateur ouvre une conversation par sujet : voir la liste des sujets ouverts en tête de notes/REPRISE.md. Résumé et suite : notes/HANDOFF.md, « Session du 2026-10-21 (HUD) » et « (mur entre les joueurs, 16:9) » ; détails : notes/REPRISE.md.
Règles propres au chantier : mesurer avant de corriger quand la cause n'est pas sûre ; ne rien modifier dans lib/ (jonction vers ../Goemon64Recomp/lib) — tout passe par ga.toml et src/ ; pas de git.
Modèle conseillé : Opus pour retrouver le chemin de création de J1/J2 et étendre le nombre de joueurs (reverse engineering) ; Sonnet pour compiler, tester, nettoyer les traces et mettre les notes à jour.