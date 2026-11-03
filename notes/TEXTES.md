# Textes du jeu (dialogues, menus) — pour un futur mod de traduction

Écrit le 2026-10-24 (Opus), mesures statiques sur `baserom.us.decompressed.z64` + `config/rommy.json`.
« rommy N » = index de `config/rommy.json` ; numéro du jeu = rommy + 1 (voir `notes/OVERLAYS.md`).

## Modèle choisi par DK64Recomp (rappel)
Le mod `translation_french.nrm` de DK64 est un mod de code : il s'abonne à l'événement `recomp_on_asset_file_load_override(table, fichier, &données, &taille)`,
déclaré par DK64Recomp au début de son chargeur de fichiers (`patches/patches_draw.c`), et renvoie une version traduite du fichier entier.
Pour GGA, l'équivalent naturel est le chargeur `file_load` (`0x80003E50`), déjà accroché (`ga_file_load_begin` / `ga_file_load_end`, `src/game/ga_modules.c`).

## Où sont les dialogues (MESURÉ)
- **40 fichiers, rommy 328..367 (jeu 329..368)**, tous en zlib, 0,4 à 51 Ko décompressés.
  - Anglais : rommy 329..342, 345..367.
  - **Restes japonais** (codes de glyphes > 0x5A, kanji) : rommy 328, 343, 344.
- Aucun autre fichier ne contient de dialogue dans ce codage (recherche de « Goemon », « Ebisumaru », « Sasuke », « Omitsu », « Impact », « Yes », « Continue »).
- Un fichier par lieu / scène, probablement (non vérifié) : 333 = début de l'histoire (Sasuke, Impact), 362..367 = boutiques (« Store owner », « Rice balls 20 »…).
- Table des adresses `0x80026B00` : ces fichiers sont **liés à `0x08000000`** (début `0x08000000`, fin `0x08000000 + taille`), comme les modules TLB.

## Format d'un fichier de dialogue (MESURÉ)
Deux parties à la suite :
1. **Messages** : mots de 16 bits (big-endian).
2. **Script d'événement** à la fin (ex. rommy 362 : à partir de `0x3444` environ, fichier de 0x7D40 octets) : instructions de 8 octets `{op u16, 0, valeur/pointeur u32}`.
   Exemples : `0400 0000 0800xxxx`, `0900 0000 000000nn`, `1000 0000 0800xxxx` (souvent vers un message), `0A00 0000 0800xxxx` (saut ?), `2200`, `1100`, `1200`.
   Les pointeurs `0x08xxxxxx` = `0x08000000 + offset dans CE fichier` (mesuré : rommy 362 contient 367 pointeurs vers ses propres débuts de messages).
   Sens exact des opcodes : SUPPOSÉ, pas encore lu dans le code.
   → **Conséquence pour une traduction** : si un message change de longueur, tous les messages suivants bougent, donc il faut **reconstruire le fichier entier** et recalculer les pointeurs du script. Un remplacement « fichier entier » au chargement (comme DK64) convient.

### Codage des caractères (MESURÉ)
- **code = ASCII − 0x20** pour les caractères affichables `0x20..0x7A` :
  `0x0000` espace, `0x0001` `!`, `0x0007` `'`, `0x0008`/`0x0009` `(` `)`, `0x000C` `,`, `0x000D` `-`, `0x000E` `.`, `0x000F` `/`,
  `0x0010..0x0019` chiffres, `0x001A` `:`, `0x001F` `?`, `0x0021..0x003A` `A..Z`, `0x0041..0x005A` `a..z`.
  (`0x001C`, `0x001E`, `0x0002`, `0x0004` existent aussi : `<`, `>`, `"`, `$` si la règle tient ; non vérifié à l'écran.)
- Codes > `0x5A` : seulement dans les fichiers japonais (jusqu'à `0x314` environ) → la police contient au moins ~800 glyphes ; **lettres accentuées : inconnu** (police pas encore trouvée).
- **Codes de contrôle** `0xFFxx` (comptes sur les 37 fichiers anglais) :
  - `FFFF` (5045) : fin de message / de bulle ; suivi de `0000` de bourrage.
  - `FFFE` (2996) : retour à la ligne.
  - `FFF9` (1185) / `FFFB` (1357) : début / fin de mot mis en couleur (« <F9>Impact<FB> »).
  - `FFF8` (368), `FFF7`, `FFF6`, `FFF5` : SUPPOSÉ attente / choix (vu avant les réponses « Yes / No » et les lignes de boutique).
  - `FFEC`, `FFE8`, `FFE7`, `FFE6`, `FFE9` (808), `FFEA`, `FFEB` (378), `FFED`..`FFF0` : en tête de message, SUPPOSÉ réglages de bulle (position, portrait, vitesse…).
- Les noms en tête de réplique (« Sasuke: ») font partie du texte ; l'alignement se fait avec des espaces.

## Autres textes (MESURÉ)
- **Module de l'écran titre (rommy 17, `0x65EEA0`)**, en ASCII : messages du Controller Pak / Rumble Pak **en anglais, français et allemand**
  (« Ins<erez le Rumble Pak », « R<eparez », « besch~adigt » : accent = caractère de préfixe `<` aigu, `~` tréma, etc. avant la lettre),
  et noms des éléments du menu titre : `Start`, `Option`, `Stereo`, `Monaural`, **`Language`**, `Sound`, **`English`, `French`, `German`** (chacun en 3 casses).
  → Restes de la version européenne (Mystical Ninja 2, anglais / français / allemand). Dans la ROM US, **aucun dialogue français ni allemand** (recherche de « Merci », « Bonjour », « Danke », « Nein »… : 0).
  SUPPOSÉ : ces mots sont des noms d'images (le menu titre est dessiné avec des images de texte), pas du texte affiché.
- Gestionnaire du Controller Pak (fichier 6 / code permanent, vers ROM `0x5D6089`) : ASCII (« PRESS START TO EXIT »…).
- Menus en jeu (pause, options, game over) : **pas trouvés en texte** → probablement des images (à vérifier avec un dump de textures RT64, `GGA_DUMP_TEXTURES`).

## Qui charge / affiche (état)
- Chargement : SUPPOSÉ par `file_load` (`0x80003E50`), comme tous les fichiers de la table (rien d'autre ne lit la table Nisitenma, d'après `notes/OVERLAYS.md`).
  Liés à `0x08000000` : probablement mappés par la TLB (`module_map`) ou convertis `base + (ptr & 0xFFFFFF)` ; non vérifié.
- Routine qui lit les codes `FFxx` et dessine les glyphes : **PAS TROUVÉE** en statique (les constantes `0xFFF9` / `0xFFFB` du code sont des masques `andi`, pas des comparaisons).
- Méthode conseillée (Opus) : trace temporaire dans `ga_file_load_begin` quand `idx` est entre 329 et 368 (numéro du jeu, adresse de destination),
  puis trace d'accès lecture à cette zone (macro `MEM_H` / `MEM_HU` de `include/gga_recomp/recomp.h` avec `__func__`, comme pour le mur entre les joueurs) pendant un dialogue.
  On trouvera l'interpréteur de script et la routine d'affichage, puis la police (pour savoir si les accents existent).

## Version européenne (Mystical Ninja 2 Starring Goemon, `NGMP`) — 2026-10-24 (Opus), statique
ROM fournie par l'utilisateur : `ref/goemon2.eu.z64` (SHA1 `aadd1d7e943cbfa5496a370158b5f8fe17b06415`, jamais committée).
Décompressée par le même outil : `py -I tools\ga_rommy.py decompress -i ref\goemon2.eu.z64 -o ref\goemon2.eu.decompressed.z64 -m ref\rommy.eu.json`
(27 938 906 octets, 2714 fichiers contre 2576 en US). Le code EU n'est PAS à la même adresse : fichier 6 EU chargé à `0x800C8F70` (US `0x800C7B10`).

### Dialogues EU (MESURÉ)
- 3 jeux de 39 fichiers, **même codage que l'US** (ASCII − 0x20, mêmes codes `FFxx`, même structure messages + script) :
  - anglais : rommy EU 328..366 ; allemand : 396..434 (+68) ; français : 464..502 (+136).
  - Correspondance avec l'US : **fichier US rommy `u` = EU anglais `u − 1`** pour u = 329..341 et 345..367 (l'EU n'a plus le japonais US 328 ; US 342..344 ↔ EU 341..343 sont des petits fichiers différents, non regardés).
    Donc FR de `u` = EU `u + 135`, DE de `u` = EU `u + 67`.
- **Accents** (dans le texte EU, codes > 0x5A) : `0x61` à, `0x62` ä, `0x64` é, `0x66` ê, `0x6A` ô, `0x6B` ö, `0x6D` ü, `0x6F` ç, `0x71` ß
  (relevés sur 2 fichiers ; liste complète à faire en comptant tous les codes `0x5B..0xFF` des fichiers FR / DE). En US, ces codes sont des glyphes japonais (SUPPOSÉ) → il faudra la police EU.
- **Les scripts contiennent des adresses du code du jeu** : on ne peut PAS copier un fichier EU tel quel dans l'US.
  Comparaison US / EU anglais (36 fichiers de même taille) : seules différences = valeurs de 3 opcodes :
  - op `0x0B` = **appel d'une fonction du jeu** (fichier 6) : adresse EU → US = **EU − `0x15B0`** pour les 41 adresses utilisées par les fichiers EN / FR / DE
    (`0x800EE520`..`0x800EFA94` → `0x800ECF70`..`0x800EE4E4`), vérifié par signature du code (35 uniques) et par la comparaison EN (6 autres) : toutes d'accord.
  - op `0x09` = **attente en images** : EU (50 Hz) × 1,2 ≈ US (60 Hz) (6→8, 8→10, 14→18, 24→30, 49→60, 74→90, 99/100→120, 150→180, 174→210, 305→368).
  - op `0x10` / `0x0A` = pointeurs dans le fichier lui-même (`0x08xxxxxx`), à garder.
  - Les scripts FR / DE n'ont pas toujours le même nombre de messages que l'anglais (bulles découpées autrement) : on convertit chaque fichier EU **tel quel** (ses propres pointeurs), on ne fusionne pas avec le script US.
  → **Conversion possible** d'un fichier FR / DE EU en fichier US : copier, puis sur chaque instruction `{op, 0, valeur}` : op `0x0B` : valeur − `0x15B0` ; op `0x09` : valeur × 1,2 arrondie. À valider en jeu.

### Langue dans le code (MESURÉ)
- Variable de langue EU : **octet `0x80088E50`** (0 = anglais, 1 = français, 2 = allemand). Écrite par le menu titre (module rommy 17, `sb` à `+0x5170`)
  et par l'overlay rommy 8 (`+0x302D8..0x30324`, test des boutons `0x1000` / `0x8000` / `0x4000` = START / A / B de la manette : raccourci au démarrage, SUPPOSÉ).
  Lue par : module titre 17 (21 lectures), modules 33 (images d'un écran, table `0x08003B8C` de 5 images par langue), 35, 39, 106 (images / positions par langue, listes `0x03xxxxxx`), fichier 6 (`0x800E79CC`, `0x800E7A74` : identifiant + `0x332` en DE, + `0x664` en FR, SUPPOSÉ des sons / voix), overlay 9 (`+0x192EC`).
- Menu titre EU : table de chaînes `0x0800C900` (module 17), **3 chaînes par élément** (EN, FR, DE) : `chaîne[élément * 3 + langue]` (code `0x08007A00`).
  Éléments : Start / Commencez, Option / Optionen, Stereo, Monaural / Mono, **Language / Langue / Sprache**, Sound / Son, English, Français, Deutsch.
- **Version US : le même mécanisme est encore là.** Le module titre US 17 lit une variable de langue **`0x80088200`** (9 lectures, `chaîne[élément * 3 + langue]`, table `0x0800BF30`) et l'écrit une fois (`+0x5030`) ;
  mais toutes ses chaînes sont en anglais (« Start, START, start »…), et aucun autre code US ne la lit. L'élément « Language » n'est pas affiché en US (code du menu pas encore comparé).
- **Pas trouvé** : comment l'EU choisit le fichier de dialogue de la langue (aucune lecture directe de `0x80088E50` ne calcule un `+68`, pas de table `{n, n+68, n+136}`).
  SUPPOSÉ : par une structure globale (base + décalage) ou dans le chargeur. Inutile pour notre intégration (on redirige nous-mêmes les fichiers, voir le plan).

### Autres données traduites en EU (à voir)
- Images : les modules 33, 35, 39, 106 choisissent des images selon la langue (écrans avec du texte dessiné).
- Police : pas encore trouvée. Candidats (fichiers « PIC » bruts qui grossissent en EU, format d'image compressé inconnu, en-tête `PIC\x1a` / `PIC000\x1a`) :
  US 710 (95 856) → EU 848 (104 992), US 796 (142 656) → EU 934 (161 120), US 761 → EU 899, US 533 / 534 → EU 671 / 672, US 500 → EU 635.

## Menu Language dans le menu titre US (2026-10-24, Opus) — compilé, À TESTER
Arbre EU (photos de l'utilisateur) : titre [Start][Option] → Option [Language][Sound] → Language [English][Français][Deutsch]. Titre US : [Start][Sound].
MESURÉ (module titre US, `build/splat/asm/65EEA0.s`) : les écrans EU sont encore dans le module US, seulement plus reliés.
- Boutons : `func_08007C4C(a0, type, x, y)`, type 1 Start, 2 Option, 3 Stereo, 4 Monaural, 5 Language, 6 Sound, 7 English, 8 Français, 9 Deutsch, 0xA / 0xB (écran de Start).
  Dessin : `table[(type − 1) * 3 + langue]` (table `0x0800BF30`, chaînes `0x0800D150..0x0800D23F`), langue = **`D_80088200`** (= `D_800881F0 + 0x10`).
- Écrans (entrée → mise à jour) : titre `func_08004564` (1re fois) / `func_080049FC` (retour) → `func_08004790` ; Option `func_08004D44` → `func_08004AD8` (Language → `func_0800507C`, Sound → `func_0800545C`, B → titre) ;
  Language `func_0800507C` → `func_08004E20` (A : `D_80088200 = curseur`, puis Option ; B : Option) ; Sound `func_0800545C` → `func_0800517C` (sortie `0x08005430` → titre).
- FAIT (`ga.toml`, 4 `[[patches.instruction]]`) : boutons du titre type 6 → 2 (`0x08004728`, `0x08004A94`) ; titre 2e bouton → `func_08004D44` (`0x080049B0`) ; sortie de Sound → Option (`0x08005430`).
- FAIT (`src/game/ga_lang.c`, appelé par `ga_module_mapped`) : à chaque mappage du module titre (fichier du jeu 18), table `0x0800BF30` et chaînes réécrites avec les libellés EU (EN / FR / DE, 205 octets sur 240).
  Le texte ASCII du jeu (`func_800EB1DC`, fichier 6) gère déjà les préfixes d'accent EU (`<` aigu, `^`, `{` cédille, `~` tréma).
- Limites : la langue n'est pas enregistrée (repart en anglais à chaque lancement, à voir) ; elle ne change encore que les libellés du menu (pas les dialogues, voir le plan).
- Test du 2026-10-24 (utilisateur) : Option / Language / Sound et libellés FR / DE OK ; au 1er affichage le titre montrait encore [Start][Sound] :
  3e création des boutons du titre dans `func_080043E0` (`0x08004480`, type 6 → 2) ajoutée, recompilé, à retester.
  Après l'écran de sauvegarde « tout redevient anglais » : normal pour les textes (rien d'autre n'est traduit en US) ; à vérifier si `D_80088200` est remis à 0 (retour au titre en FR ?).
- À tester : titre [Start][Option] ; Option → [Language][Sound] ; Language → 3 langues, A revient à Option, libellés en français / allemand ; Sound → Stereo / Monaural puis retour à Option ; B partout ; Start inchangé.

- Test du 2026-10-24 (suite) : VALIDÉ par l'utilisateur ([Start][Option] dès le lancement, menus FR / DE). La langue est
  conservée d'un lancement à l'autre : le jeu l'enregistre déjà (`D_80088200` fait partie de `D_800881F0`, sauvegardé ; mécanisme non lu).

- 2026-10-24 (suite) : menu CONDITIONNEL, compilé, À TESTER. Sans ROM EU valide (`ga_lang_eu_ready()` = table de scripts EU vérifiée) : menu US d'origine
  [Start][Sound] et langue remise à 0 au mappage du titre. Avec la ROM EU : [Start][Option] → Language comme avant. Les 3 `addiu $a1, 6 → 2` sont devenus
  des hooks après l'instruction ; les 2 `jal` sont devenus `jalr $at` (instruction `0x0020F809`) avec un hook qui met la cible dans `$at`
  (`LOOKUP_FUNC`, comme les appels de modules du jeu).

## Logo du titre EU (2026-10-24, Opus) — VALIDÉ par l'utilisateur (logo EU avec la ROM EU, logo US sans)
MESURÉ (vidage RT64 du titre `build\texdump_title` + trace des lectures brutes `func_80001CF8`) :
- Le titre lit ses images comme des morceaux PIC de l'archive **fichier du jeu 762** (rommy 761, 576 Ko), position / taille d'après une table du fichier 11
  (lecture de 8 octets à fichier 11 + 0x848), et les décode dans le segment 3 (= place réservée au fichier 739, `0x80417840..0x80490060`).
- Logo = morceau `0x35720` (`0x9F60` octets), décodé au segment 3 + `0x595C0` ; dessiné par `func_08007014` (module titre) : `D_030595C0`, 269 × 102, position -0x86 / -0x35.
  En-tête d'un morceau PIC : « PIC\x1a », 0, puis 0x10, largeur 16 bits, hauteur 8 bits (US `10 010D 00 66`).
- EU : archive rommy 899 ; le seul morceau du titre qui change est le logo, `0x35720..0x3A2D0` (`0x4BB0`, en-tête `10 010E 00 62` = 270 × 98) ;
  les morceaux suivants sont décalés de -0x53B0. Le module titre EU le dessine en 270 × 98, position -0x87 / -0x33.
FAIT (`ga_lang_eu.cpp`, `ga.toml`) : avec la ROM EU, le morceau EU remplace le morceau US juste après la copie DMA (hooks entrée / `0x80001D28` de `func_80001CF8`),
et 4 hooks donnent au dessin la taille / position EU. Sans ROM EU : logo US.

## Police et dialogues FR / DE (2026-10-24, Opus) — VALIDÉ en français (village du monde 1 : hôtel, maison de Sasuke, villageois) et en allemand (utilisateur) ; suite du jeu non testée
MESURÉ (fichier 6 US, `build/splat/asm/582D30.s`) :
- **Une seule police** pour les dialogues et le texte ASCII des menus : glyphes 8 × 12 en I4 (48 octets), table `D_80109880`, largeurs (octets) `D_8010AA80`.
  US : 96 glyphes (`0x00..0x5F`) ; EU : 132 (`0x00..0x83`, les 96 premiers identiques), table EU `0x8010AE80`, largeurs `0x8010C740` (fichier 6 EU chargé à `0x800C8F70`).
  En US, un code ≥ `0x60` lit les données qui suivent la table (n'importe quoi).
- Lecteurs : `func_800E9B18(objet, code)` = un caractère de dialogue (codes négatifs `0xFFED..0xFFFE` = contrôles, table de saut `0x8010E2A8`),
  dessin par `func_800E9A44` (glyphe = `D_80109880 + code * 48`) ; `func_800EB1DC` = chaîne ASCII (`c − 0x20` ; préfixes `<` `^` `{` `~` `>` + lettre
  → codes `0x60..0x82`, les mêmes que les accents des dialogues EU) ; `func_800EB998` = largeur d'une chaîne ASCII (ne gère pas les préfixes, comme en EU supposé).
- Mémoire : RT64 lit les adresses jusqu'à 16 Mo (`RDP_ADDRESS_MASK 0xFFFFFF`) ; la RDRAM du runtime fait 512 Mo ; `0x80800000..0x80800FFF` = poignées PI,
  `0x81000000+` = mods ; **`0x80E00000` libre** → copie de la police EU (glyphes `0x80E00000`, largeurs `0x80E01900`).
- Chargement des fichiers : la place réservée vient de la table `0x80026B00` (`func_80003D2C` : taille = fin − début), le nombre de pages TLB aussi.
FAIT (`src/game/ga_lang_eu.cpp`, 5 `[[patches.hook]]` dans `ga.toml`, `goemon64::decompress_rom_file` dans `rom_decompression.cpp`) :
- ROM EU lue au 1er `file_load` depuis **`%LOCALAPPDATA%\Goemon64Recompiled\goemon2.eu.z64`** (z64, 16 Mo, `NGMP`) ; sans elle rien ne change.
- Police : copiée en `0x80E00000` dès que les 96 glyphes US sont en mémoire et identiques aux EU (contrôle des adresses) ; accroches :
  `0x800E9AA4` (`$a2` code → `$a1`), `0x800E9BC8` (`$a1` code → `$t2` largeur), `0x800EB2BC` (`$t0` → `$t1`), `0x800EB2EC` (`$t0` → `$t2`), seulement pour `0x60..0x83`.
- Dialogues : fichiers du jeu 330..342 et 346..368 ; EU FR = rommy US + 135, DE = + 67 ; conversion op `0x0B` − `0x15B0`, op `0x09` (table mesurée, sinon × 1,2) ;
  table `0x80026B00` agrandie à la plus grande des 3 versions ; à la fin de `file_load` (`0x80003FD8`, `$s0` destination, `$a3` fin), si `D_80088200` = 1 / 2, contenu remplacé.
- Effets de bord attendus : les accents du menu titre (« Français », « Stéréo ») s'affichent maintenant correctement (avant : glyphes faux).
- À tester : (1) journal `[GGA LANG] European ROM loaded: font + 36 FR / 36 DE` (lancer avec `--log`) ; (2) nouvelle partie en FR : dialogue de Sasuke au début, accents ;
  (3) idem DE ; (4) EN inchangé ; (5) boutiques (scripts avec appels de fonctions) ; (6) changer de langue en cours de jeu = effet au prochain chargement de dialogue.
- Test 2026-10-24 : police VALIDÉE (« Français », et « Insérez » : le message Controller Pak du module titre US s'affiche déjà en français
  quand la langue = FR, le module US garde les 3 langues pour ces messages). Piège : la ROM EU va dans le dossier de l'APPLICATION
  (`%LOCALAPPDATA%\Goemon64Recompiled`, nom hérité de Goemon 1), pas dans le dossier du projet. Dialogues : pas encore testés.
- Test 2026-10-24 : FREEZE en entrant dans la maison où l'on libère Sasuke (FR). Pas de `gga_crash.txt` (blocage, pas plantage).
  MESURÉ ensuite : l'op `0x09` n'est pas « attendre » mais une valeur générique (`04 <ptr> ; 09 <valeur> ; 08 / 10 ...`), qui sert aussi d'argument ;
  sur les fichiers anglais, aucune règle ne sépare les 29 attentes modifiées des valeurs identiques (règle « forme + valeur connue » : 45 vraies, 88 fausses).
  La conversion × 1,2 corrompait donc des arguments (cause probable). RETIRÉE : seules les adresses op `0x0B` sont converties ; les bulles FR / DE à durée fixe vont ~17 % trop vite.
  Recompilé, à retester au même endroit.
- Test 2026-10-24 (après retrait du × 1,2) : freeze toujours, à l'hôtel du village en parlant au PNJ de sauvegarde (FR) ; EN OK.
  MESURÉ (trace `gga_lang.txt`) : dernier fichier chargé = jeu 363 (US rommy 362, boutiques / hôtel) REMPLACÉ par EU 497 ; en EN, le jeu charge ensuite 2554.
  Le fichier 333 remplacé plus tôt n'a pas bloqué. `build\gga_hang.txt` (chien de garde existant de main.cpp, `crash_where.py --log`) :
  tous les threads du jeu attendent dans `osRecvMesg` / `pause_self` → interblocage, pas de boucle infinie.
  Vérifié sans résultat : aucune adresse 0x80 hors des 41 appels, aucune pointeur hors fichier, mêmes opcodes US / EU, mêmes cibles d'appel (décalage de 4 octets seulement).
  Script US 362 : début à 0x343C ; EU EN 361 : 0x3440 ; EU FR 497 : 0x32A8 (même début `04 08000088 ; 09 0 ; 04 08000008...`).
  ESSAI `GGA_LANG_TEST=en` (« Français » charge l'anglais EU converti) : FREEZE aussi → pas le texte français, mais l'usage d'un fichier EU.
  MESURÉ : scripts EN US / EU alignés instruction par instruction (difflib) : seules différences = les 29 attentes. Texte : EU corrige « swith » → « switch »
  (+2 octets à 0x2F74, début du script +4). Donc un fichier presque identique bloque : piste = quelque chose hors du fichier qui vise une position fixe dedans (non trouvé en statique).
  Interpréteur des scripts : `func_800E7B34` (objet + 0x20 ; pointeur de script = `*(objet + 0x28)`, `lbu` opcode, ops 0x04..0x23, table de saut `0x8010DD7C`), lu via la TLB.
  TRACE (hook `0x800E7B68`, 256 dernières instructions → `build\gga_script.txt` par le chien de garde) : CAUSE TROUVÉE.
  Boucle infinie sur une seule instruction à `0x8047CEA4` = fichier 363 + `0x5EA4` = début d'un sous-script US ; dans le fichier EU il y a là une valeur (`00000001`).
  MESURÉ : le jeu démarre un script par **table** : `fichier = D_8010AED0[id]` (u16), `script = D_8010B500[id]` (pointeur `0x08xxxxxx` dans ce fichier),
  `func_8000458C(fichier, script)` → adresse réelle (code `0x800E7428`). US : ids 0..790 (id 1 = fichier japonais 329). EU : `0x8010DEF0`, 3 blocs de 818 ids
  (EN, DE, FR ; c'est le `+0x332` / `+0x664` de la fonction EU `0x800E79A8`) ; **EU id = US id − 1**. 663 des 789 cases EN = US (les autres : fichiers EU décalés).
  CORRECTIF (compilé, à tester) : en FR / DE, D_8010B500 ids 2..790 réécrit avec le bloc EU de la langue (copie US gardée pour l'anglais), à chaque fin de
  `file_load` ; les 39 fichiers (US 329..367, y compris 342..344) remplacés pour que chaque case corresponde à son fichier. Vérifié hors jeu : les 789 cases FR
  tombent sur une instruction du fichier FR correspondant (id 736 = `0x080032A8`, 1er script FR de l'hôtel). Sans table valide, aucun fichier n'est remplacé.
  VALIDÉ par l'utilisateur (hôtel, maison de Sasuke, villageois du monde 1 en français, plus de freeze). Traces temporaires retirées
  (`gga_lang.txt`, hook de `func_800E7B34`, `build\gga_script.txt`) ; le chien de garde de `main.cpp` (`build\gga_hang.txt`, avec `--log`) reste.
  Méthode qui a marché : trace des dernières instructions de script + chien de garde (pile d'appels) au moment du blocage.
  Reste : allemand à tester ; durées 50 Hz des bulles à défilement automatique (~17 % trop rapides) ; textes hors dialogues ; images traduites ; sons par langue.
- Non couvert : textes hors dialogues (écran de sauvegarde, pause, noms d'objets ?) ; images traduites (modules 33 / 35 / 39 / 106) ; sons selon la langue ; US 342..344.

## Reste à traduire (2026-10-24, Opus)
MESURÉ (alignement des listes de fichiers US / EU) : en dehors des dialogues, l'EU diffère par le code (5..11, modules 13..326 : adresses),
29 petits fichiers par langue sans intérêt (EU 367..395 / 435..463 / 503..531 : scripts de débogage « data/txt/m_boss.sc START » ; vides en US 368..396),
et des fichiers d'images « PIC » changés : ajout à la fin (US 534 → EU 672) ou au milieu, donc décalés (761 → 899, 796 → 934, 500 → 635, 533 → 671, 710 → 848) ;
retouches de quelques octets (491, 531, 709, 738, 741, 783 : positions / 50 Hz probables). Après US 410, numéro EU ≈ US + 135..138.
Écrans signalés en anglais par l'utilisateur (captures) : journal (sauvegarde), choix du joueur, carte du monde. Ce sont des chaînes ASCII :
- Module titre (17) : 8 chaînes passées en dur (lui / addiu) au 6e argument de `func_08001070` ; l'EU les prend dans une table de 3 (`0x0800C5C0`).
  FAIT : hook à l'entrée de `func_08001070` (`ga_lang_title_text`, `src/game/ga_lang.c`) qui remplace la chaîne anglaise par la traduction.
- Carte du monde (module 15 = fichier du jeu 16) : lit DÉJÀ `D_80088200` (`func_08001654`) pour une table de 3 (`0x08007F78` : Move / Enter stage) ;
  la table US contient « Move / MOVE / move » (d'où « MOVE » en français). FAIT : table réécrite au mappage (`ga_lang_map_mapped`).
- Les chaînes qui ne tiennent pas dans les modules sont écrites en `0x80E02000+` (RAM libre après la police) ; un objet texte accepte une adresse KSEG0 directe.
- Non traduits même en EU : noms de lieux (« Lost'n Town:INN », « Edo »…), noms des personnages.
- Seuls les modules 15 et 17 lisent la langue en US (recherche de `D_800881F0 + ...` dans tout l'asm) ; le fichier 6 en garde une copie (struct `D_800881F8`, sauvegardée).
- VALIDÉ en français (2026-10-24, utilisateur) : écran journal, choix du joueur, carte du monde. Allemand non testé.
- PIÈGE CORRIGÉ (2026-10-25) : la réécriture de table au mappage (carte, titre) se déclenchait aussi sur un mappage d'adresse brute dont
  la mémoire avait été réutilisée (combat de robot) → pointeurs écrits dans une réserve d'objets → crash (func_800D4CEC). Les deux
  fonctions vérifient maintenant que la table contient les pointeurs attendus avant d'écrire.

## Cartons d'entrée dans un monde (2026-10-24, Opus) — VALIDÉ par l'utilisateur (carton du monde 1 en FR et en DE ; autres mondes non vus)
Écran en lançant une partie : panneaux rouge / jaune, estampe (grues), Goemon, titre jaune « The Stolen Summoning Machine ».
MESURÉ :
- Le titre n'est pas du texte (absent des ROM US et EU en ASCII et en codage dialogue) : c'est une **texture 4 bits brute** (pas PIC).
- Trace des lectures brutes (`func_80001CF8`) en lançant une partie : fichier du jeu 742 (rommy 741, listes d'affichage), puis morceaux
  de l'archive 762 (celle du logo) : estampe `0x4E490` (PIC 272 × 128), portraits `0x54540..0x73F20`, titres `0x73F20..0x775A0`, etc.
- Images d'id ≥ `0x8000` : position dans l'archive = table `D_8002E49C` (4 octets par id, 24 bits bas = position, longueur = suivante − celle-ci),
  destination dans le segment 3 = table `0x800331xx` (paires id / adresse segmentée). Titres US = ids `0x851E..0x8522`, seg. 3 `0x6C550..0x6FBD0`.
- Choix et taille dans le code : `func_0800ABC0` (module du fichier 18, asm `65EEA0.s`), index 0..4 à objet + 0x10 :
  256×19, 272×20, 208×16, 272×36, 224×20 (largeur / hauteur à +0x1C / +0x20), puis `func_800CDF44(adresse seg., 0x10C, 0)` → adresse réelle à +0x8.
- EU (rommy 899) : 15 titres à la suite (ids EU `0x8533..` EN, `0x8538..` DE, `0x853D..` FR, positions FR `0x75360` …, DE `0x721F0` …),
  et le code EU (`0x80088E50` = 1 → bloc FR, 2 → bloc DE) donne ses tailles : FR 256×19, 272×20, 208×16, 272×36, **256×20** ;
  DE 256×19, 272×20, **240×16**, **176×38**, 224×20. Vérifié en image : « The Evil Dochuki's Back!? » / « Der Rücken des bösen Dochuki!? » / « Le retour du Dochuki!? ».
FAIT (`ga_lang_eu.cpp`, `ga_lang_card` ; hook `ga.toml` à `0x0800AD1C`) : en FR / DE avec la ROM EU, le titre EU de l'index est copié en
`0x80E10000` (RAM libre) et remplace l'adresse rendue par `func_800CDF44` et la taille. Sans ROM EU / en anglais : rien ne change.
Non traité : panneaux rouge / jaune (vides en US, pas de texte vu). À tester : nouvelle partie en FR puis en DE (carton du monde 1), autres mondes.

## « STAGE CLEAR » (fin de stage, robots des châteaux) (2026-10-25, Opus) — VALIDÉ en fin de niveau (FR, DE, et sans ROM EU) ; château (robot) À TESTER
MESURÉ (trace des chargements en jeu + statique) :
- Module « mesStageClear » = fichier du jeu 108 (US rommy 107, asm `774910.s` ; EU rommy 106). Les modules US 37 / 41 (EU 35 / 39, qui lisent
  aussi la langue : modèles seg. 3 `0x7530` EN / `0x7640` DE / `0x7750` FR) ne sont PAS chargés à la fin de stage : autre écran, non identifié.
- US : 10 lettres 3D S,T,A,G,E,C,L,E,A,R ; une lettre = objet créé toutes les 5 images par `func_80197138` (cases obj + 0x34..0x58,
  compteur + 0x5E jusqu'à 10) ; modèles `D_08000C50` (`0x03000058 + 0x100 × n`), positions `D_08000C78` / `D_08000CA0` / `D_08000CC8`, y fixe 274.
- Ressource `0x4A` (table du fichier 11, 8 octets = fichiers des segments 3..6) : US seg. 3 = fichier 532 (rommy 531, listes d'affichage des lettres,
  0x800 octets) ; textures 32×32 RGBA16 chargées par la table (id, destination) : ids `0x815F..0x8166` → `0x03000800..0x03004000`, morceaux PIC du fichier 534.
- EU : ressource `0x4A` (EN) / `0x111` (FR) / `0x142` (DE) → seg. 3 = fichiers EU 668 / 669 / 670 (listes de lettres, FR 11 modèles, DE 7),
  seg. 4 = alphabet commun ids EU `0x8180..0x8189` → `0x04000000 + 0x800 n` (fichier EU 673 = US 534 + 10 morceaux), plus 1 / 3 / 3 textures
  propres en seg. 3 (ids EU `0x8160` / `0x8161..0x8163` / `0x8164..0x8166`). Le module EU écrit ses tables à l'exécution (bss `0x19D0` modèles,
  `0x1A08` / `0x1A40` / `0x1A78` / `0x1AB0` positions, 14 cases) : DE 12 lettres sur 2 lignes, FR 14 lettres (octet obj + 0x8F = nombre).
- Chargement d'un id : `func_800040D0(id, dest)` lit le morceau puis, si « PIC », `func_80005820` (en-tête) + `func_80005888` (décodage).
  Table des positions EU : `0x2F8C4 + (id − 0x8000) × 4` ; plages id → fichier : US `0x2EFDC`, EU `0x2F804`.
OBSTACLE : le module US est câblé pour 10 lettres (structure de l'objet) ; FR / DE en demandent 14 / 12.
MESURÉ (diff instruction par instruction US 107 / EU 106) : même code, différences = disposition du contrôleur (compteurs + 0x6C / 0x6E,
nombre + 0x6F, 14 cases), tables par langue (EU bss : `0x19D0` modèles = US `C50`, `0x1A08` x0 = `C78`, `0x1A40` y0 = `CA0`, `0x1A78` x1 = `CC8`,
`0x1AB0` y final, US constant 3), paires de lettres qui tournent (images 0x2D..0x3C), sens de rotation par lettre, éclair sauté pour la 14e lettre FR,
création toutes les 4 images (50 Hz). Valeurs FR / DE extraites en simulant le code EU (tables recopiées dans `ga_lang_stc.c`).
FAIT (`src/game/ga_lang_stc.c` + `ga_lang_eu.cpp` + `ga.toml`, toutes langues) : le code US garde la main, avec des patchs d'instructions
(compteurs déplacés, boucles sur l'octet + 0x6F = nombre × 4, tables lues en `0x80E2F000`) et 8 hooks (init, création toutes les 4 images en FR / DE,
paires, segments 3 / 4 des modèles, y final, rotation, éclair). En FR / DE à l'init : listes EU (rommy EU 668 / 669) copiées en `0x80E20000`
(seg. 3), 13 textures PIC EU décodées par le décodeur du jeu (`func_80005820` / `func_80005888` appelées depuis le C) en seg. 3 / `0x80E24000` (seg. 4).
En anglais : tables US recopiées, lettres US (mêmes valeurs qu'avant).
À TESTER : fin de stage en EN (inchangé ?), FR, DE ; robot de château (même module ? non vérifié) ; message « EU texture did not decode » dans le journal.
1er test (2026-10-25) : freeze en prenant le laissez-passer puis plantage (`gga_crash.txt` : lecture à `0xF801004C` dans `func_800D40FC`,
le parcours des objets). Cause probable (supposée, taille de l'objet non mesurée) : compteurs déplacés à travail + 0x6C..0x6F = au-delà de la zone
de travail US → objet suivant écrasé. CORRIGÉ : compteurs US gardés (+ 0x5C / + 0x5E), 14 cases de lettres en RAM libre `0x80E30034`
(boucles : `or $s1, $s2, $zero` → `lui $s1, 0x80E3` ; création : hook à `0x08000268`), nombre × 4 dans l'octet travail + 0x34
(anciennes cases US), paires US aussi faites en C. Rien n'est écrit au-delà de travail + 0x5F. Recompilé, à retester.

## « AREA CLEAR » des robots de château (2026-10-25, Opus) — COMPILÉ, À TESTER (2e essai, overlay du combat)
FAIT (choix de l'utilisateur : comme l'EU ; `ga_lang_stc.c` « AREA CLEAR after a robot fight », 7 hooks `ga.toml`) :
- MESURÉ (diff US / EU de l'objet lettre, EU 0x412C octets) : même code, étendu par index ; seuls changent par index le modèle, la position de
  départ (k-ième lettre : 6 positions US puis (−3, −151)) et la cible (x, y) de `func_8017B4A0` (z −50, vitesse 1,5, échelles 0,03 / 0,09
  identiques) ; la traînée (`func_8018953C`) prend l'index comme mode → même table de modèles. Création immédiate (`func_800D41AC(obj, 2, …)`).
- Ressource 0xF2 : seg. 3 = US rommy 709 / EU rommy 847 (0x1A20) ; 29 assets EU 0x8458..0x8474 (paires (id, adresse) à ROM EU 0x34054,
  archive EU rommy 848 ; 0x845D brut, les autres PIC).
- Contrôleur `func_80189914` : hook avant 0x80189A0C (FR / DE : lettres EU créées en C comme index US 2 « modèle », index EU retenu par objet ;
  sons 0x206 ; `$v0` faussé à 0x7FFF pour sauter les créations / sons US) ; hook avant 0x80189D00 (vraie image remise avant le test de fin 0x9B).
- Lettre : fin d'init 0x8018871C (modèle EU + seg. 3 = `0x80E50000` + départ) ; bloc de l'index 2 : 0x80188B7C (traînée en attente),
  0x80188BA4 (cible EU). Traînée : 0x80189738 (modèle EU). Fichier EU + 29 assets décodés en `0x80E50000` à la frame 5.
- À TESTER : fin de robot en FR, DE, EN (inchangé), sans ROM EU. Non vérifié : ce que montrent exactement les mots FR / DE.
2e ANALYSE (avant le code ci-dessus) :
CORRECTION (test de l'utilisateur, 2026-10-25 : toujours « AREA CLEAR », et le hook ci-dessous ne s'est jamais exécuté) :
- Le vrai code est l'overlay du combat de robot (fichier du jeu 10 = US rommy 9, asm `61A700.s`, vram US 0x801738A0, EU 0x80177EB0).
  US : `func_801894DC(x, y, z, index)` crée une lettre 3D aux images 5, 15… 85 (9 lettres A R E A / C L E A R, index 0..8) depuis
  l'appelant vers 0x80189A0C ; l'objet lettre `func_80188224` (0x12B8 octets) choisit le modèle (ressource 0xF2, seg. 3 `0x030006E0 + …`)
  et la position de départ selon l'index.
- EU (rommy 9, lecture de la langue à +0x192EC) : EN = l'US ; FR = 13 lettres, index 0x0A..0x16, créées en (0,0,0) aux images 5, 13… 0x62 ;
  DE = 13 lettres, index 0x1E..0x2A. Modèles EU (idx:modèle) : a:af0 b:bc0 c:bc0 d:c90 e:d60 f:e30 10:f00 11:d60 12:af0 13:fd0 14:10a0 15:bc0 16:e30 ;
  1e:1170 1f:1240 20:1310 21:13e0 22:1240 23:14b0 24:1650 25:1580 26:1720 27:17f0 28:18c0 29:1240 2a:1990 → le fichier EU de la ressource 0xF2
  contient beaucoup plus de lettres que l'US (dernière US : 0xa20).
- L'objet lettre EU fait 0x1A7C octets (US 0x12B8) et diffère presque partout (positions et échelles par index, suite de l'animation) :
  bien plus gros que STAGE CLEAR. Pas commencé ; choix demandé à l'utilisateur.
- Le 1er essai (ci-dessous) visait les modules 37 / 41 : ce n'est PAS « AREA CLEAR » (carré 4 sommets + icône 64×32, écran non identifié).
  Le code reste en place car il copie le comportement EU (modèle selon la langue), mais il n'a jamais été vu en jeu.
1er essai :
MESURÉ (trace des chargements en jeu, robot battu) : module = fichier du jeu 37 (US rommy 36, asm `6F9620.s` ; EU rommy 35), chargé avec le
fichier 492 (US rommy 491). Le module crée 2 modèles par `func_80197534` : `0x03007420` (commun) et `0x03007530` (texte). Le module 41
(US rommy 40, EU 39) a le même code (non vu en jeu, traité aussi).
- EU : le module choisit `0x03007750` en FR, `0x03007640` en DE, `0x03007530` en EN ; fichier EU 626 = US 491 + 0x220 octets (2 listes) + retouches de
  2 octets ailleurs (50 Hz ?). Chaque liste = un carré (4 sommets) avec une texture 64×32 4 bits + palette en seg. 3 après le fichier : FR asset EU
  `0x80E5` → `0x0300E8D0`, DE = l'anglais EU `0x80E4` → `0x0300E4B0` (la texture anglaise EU diffère de l'US, 480 octets compressés contre 384),
  archive EU rommy 635 (US 500). L'allemand a donc d'autres sommets mais la texture anglaise EU (non vu : ce que l'image contient exactement).
FAIT (`ga_lang_stc.c`, `ga_area_model` ; hooks `ga.toml` dans les 2 modules, après la création du modèle texte) : en FR / DE, fichier EU 626
copié en `0x80E40000`, texture décodée par le décodeur PIC du jeu à son adresse EU, modèle + 0x40 = liste EU, + 0x44 (seg. 3) = `0x80E40000`.
À TESTER : fin de robot en FR, DE, et sans ROM EU.

## Plan : choix de langue EN / FR / DE dans la version US (proposé le 2026-10-24, rien de fait)
Idée : reprendre les textes officiels de la ROM EU, convertis pour le code US, et les servir à la place des fichiers US selon la langue choisie.
1. [Opus] **Police** : trouver le fichier de police (candidats ci-dessus, format PIC à comprendre) et le code qui dessine les glyphes ; vérifier qu'on peut charger la police EU dans l'US (même format, mêmes tailles de glyphes).
2. [Opus] **Redirection** : dans `ga_file_load_begin` (`file_load` `0x80003E50`, déjà accroché), si langue ≠ anglais et fichier US rommy 329..341 / 345..367 : donner le fichier EU converti (op `0x0B` − `0x15B0`, op `0x09` × 1,2). Idem pour la police et les images traduites.
   Source des données : la ROM EU **du joueur** (comme la ROM US : choisie dans le lanceur, convertie au démarrage), PAS dans un mod distribué (ce serait distribuer le texte du jeu).
3. **Menu** : choix de l'utilisateur = menu titre du jeu, comme l'EU. Fait (voir « Menu Language » ci-dessus), à tester.
4. [Sonnet] Petites chaînes du menu titre / Controller Pak en FR / DE (tables de 3 chaînes), images traduites (modules 33 / 35 / 39 / 106).
Non couvert : les voix / sons qui changent selon la langue (fichier 6, `+0x332` / `+0x664`), à confirmer.
