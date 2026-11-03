# Goemon's Great Adventure (US) — notes ROM

Fichier de référence pour retrouver les infos établies. À compléter au fil de l'eau (mettre la date et « mesuré » vs « supposé »).

## Identité de la ROM (mesuré)
- Fichier : `baserom.us.z64` (copie de `Goemon64Recomp/mnsg2.z64`), 16 777 216 octets, déjà big-endian (z64).
- Titre interne : `GOEMONS GREAT ADV`, code `NGME`, version 0.
- SHA1 : `9F9E860816F9E2A68BB5F4F56086E0C7450C64D7`
- Point d'entrée : `0x80000400` ; CRC en-tête : `4252A5AD AE6FBF4E`.
- Code de boot lisible en clair à ROM `0x1000` (efface la BSS, règle la pile, saute dans le jeu). Le début de la ROM (jusqu'à ~`0x41000`) ressemble à du code MIPS ; la suite est en grande partie non lisible (compressée ou données).

## Code principal et boot (mesuré le 2026-10-19, désassemblage `build/main.dis.txt`)
- IPL3 copie la ROM depuis `0x1000` vers RAM `0x80000400` (1 Mo). Le code de boot (`0x80000400`) efface la **BSS à partir de `0x8003ED10`, longueur `0x88600`** (et non `0x8004ED10` comme écrit par erreur dans une version précédente du HANDOFF), puis met `sp = 0x80048D10` et saute à `0x80000DE0`.
- Donc le code + données en clair = RAM `0x80000400..0x8003ED10` = ROM `0x1000..0x3F910` (0x3E910 octets).
- `0x80000DE0` (point d'entrée C) : appelle `0x800121D0` (init), `0x800113D0` (probablement `osCreateThread`, thread à `0x80048D10`, fonction `0x80000E3C`, priorité 9), puis `0x80020630` (probablement `osStartThread`). Noms supposés, non vérifiés.
- La table Nisitenma-Ichigo est dans la RAM : signature à `0x8002BB84` (ROM `0x2C784`), entrées à partir de `0x8002BB94`. Premières références trouvées dans le code : `0x80003E88` (`addiu -0x447c`) et `0x800044B0` (`lw -0x446c`) : pistes pour trouver la routine de lecture de la table / de chargement.
- 63 187 lignes de désassemblage, 1530 `jal`. La fin de la zone ressemble à des données mal désassemblées : le code réel s'arrête avant (à déterminer).
- Régénérer : `.\tools\disasm_main.ps1`. Piège : l'`llvm-objdump` de `C:\Program Files\LLVM` n'a pas la cible mips, utiliser celui de `C:\msys64\mingw64\bin` avec `--mcpu=mips3`. Les cibles de `jal` sont affichées sans le préfixe `0x80000000`.

## Table de fichiers Konami « Nisitenma-Ichigo » (mesuré)
- Signature à ROM `0x2C784`, entrées de 4 octets juste après (début à `0x2C794`), fin de table à `0x2EFD8`.
- 2577 entrées + terminateur 0. La dernière entrée est un repère de fin (`0xFD7442`), pas un fichier. Donc 2576 fichiers ; le premier est à `0x582530`.
- Bit 31 de l'entrée = fichier compressé. Le reste = offset ROM. Fichiers contigus, taille = différence avec l'entrée suivante.
- Répartition : 1942 compressés zlib, 555 vides (même offset que le suivant), 78 bruts (bit 31 = 0, pas d'en-tête), 1 LZKN64 (fichier 8).
- **Format de compression : zlib standard, PAS LZKN64** (le `rommy.py` de Goemon 1 ne marche pas). Chaque fichier compressé = en-tête de 4 octets (`80 00 xx xx` : bit 31 + valeur ≈ taille de la plage, parfois −1) puis flux zlib (`78 DA`). Un octet en trop après le flux est normal. Tailles décompressées : toutes multiples de 16.
- **Fichier 8** (`0x5A92D6`, 178 188 octets) : en-tête `00 02 B8 0C` sans bit 31 = **compressé en LZKN64** (mesuré le 2026-10-19 en lisant le décompresseur `0x80002F40` ; `9F` = commande « copier 31 octets bruts »). Décompressé : 0x44400 octets de code (overlay, VRAM `0x801738A0`, voir `notes/OVERLAYS.md`). **Corrigé le 2026-10-19 (Sonnet)** : `ga_rommy.py` contient maintenant un décompresseur LZKN64 en Python pur (`lzkn64_decompress`), résultat identique octet pour octet à la bibliothèque `lzkn64` (celle de `rommy.py`) sur ce fichier. Commandes (déduites en comparant avec la bibliothèque, seul le fichier 8 les a validées) : `00-7F` copie de fenêtre (long. `(c>>2)+2`, offset 10 bits), `80-9F` octets bruts (`c&0x1F`), `C0-DF` remplissage par l'octet suivant (`(c&0x1F)+2`), `E0-FE` zéros (`(c&0x1F)+2`), `FF` zéros (octet suivant + 2) ; `A0-BF` jamais vu (lève une erreur).
- Numérotation : le jeu compte les fichiers à partir de 1 ; index du jeu = index de `rommy.json` + 1.
- Zone après le dernier fichier (`0xFD7442` → fin ROM) : conservée telle quelle ; de `0xFC1000` à la fin, beaucoup de code MIPS apparent (80 %), à vérifier.

## Outil et sortie
- `tools/ga_rommy.py decompress -i baserom.us.z64 -o baserom.us.decompressed.z64 -m config/rommy.json` (Python seul, pas de dépendance).
- Structure de sortie = celle de `rommy.py` de Goemon 1 : copie de ROM[0 : premier fichier], fichiers décompressés bout à bout, table réécrite (bit 31 effacé, nouveaux offsets), puis la fin de la ROM d'origine.
- Résultat (2026-10-19, après correction du fichier 8) : 27 059 246 octets (l'ancienne sortie, 26 957 878 octets, gardait le fichier 8 compressé). Calculer le SHA1 avec `Get-FileHash` si besoin. Manifeste : `config/rommy.json` (par fichier : type, offset et taille d'origine, nouvel offset, taille, CRC32).
- Limites : CRC d'en-tête de la ROM non recalculés ; pas de recompression (inutile pour la recomp seule).

## À faire / questions ouvertes
- Chercher les overlays : quels fichiers sont du code, à quelle adresse RAM ils sont chargés, comment le jeu les charge (appel de décompression dans le code principal).
- Zone de fin de ROM (`0xFC1000` → fin) : voir `notes/OVERLAYS.md` (non vérifié). Fichier 8 : résolu.
- À l'exécution, la recomp ne doit PAS lire la ROM décompressée (le joueur fournit la ROM d'origine) : voir comment Goemon 1 gère ça (`mnsg.overlays.txt`, `patches/overlay_reloc.c`).
- Demander sur le Discord Ganbare Goemon si quelqu'un a déjà des notes sur GGA (klorfmorf annonce la recomp de GGA).
