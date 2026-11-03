# Continuer le projet avec le chat claude.ai (compte gratuit, Sonnet, sans accès au PC)

Le chat ne voit ni tes fichiers ni ton terminal : **tu es ses yeux et ses mains**. Il réfléchit et te dit quoi changer ;
toi tu envoies les fichiers, tu lances les commandes et tu recopies les résultats. Ce guide dit quoi envoyer et quoi taper.

## 1. Démarrer une conversation
1. Ouvre une **nouvelle conversation par problème** (le compte gratuit a peu de messages et une mémoire limitée : une
   conversation courte et ciblée marche mieux qu'une très longue).
2. **Joins** (bouton trombone) au minimum : `CLAUDE.md`, `notes/REPRISE.md`, ce guide. Puis les fichiers utiles à la tâche (§ 4).
3. Colle ce message de départ :

```
Tu m'aides à continuer une recompilation statique N64 (Goemon's Great Adventure, N64Recomp + RT64).
Lis les fichiers joints : CLAUDE.md (règles), notes/REPRISE.md (état et prochaine tâche), notes/GUIDE_CLAUDE_AI.md.
Tu n'as PAS accès à mon PC : c'est moi qui lance les commandes et qui modifie les fichiers.
Règles pour tes réponses :
- parle en français, tutoie-moi, explique le pourquoi (je débute en recompilation) ;
- quand tu veux voir quelque chose, donne-moi la commande PowerShell exacte à lancer depuis la racine du projet ;
- pour modifier un fichier, donne « dans le fichier X, remplace exactement ce bloc : ... par : ... » (jamais un gros fichier entier) ;
- dis-moi ensuite quelle commande lancer (tools\regen_all.ps1 avec ou sans -HooksOnly, voir REPRISE.md) ;
- distingue ce qui est mesuré de ce qui est supposé ; si tu n'es pas sûr, propose d'abord une mesure.
Tâche du jour : <décris le problème, colle le résultat du dernier test>.
```

## 2. Les commandes dont tu auras besoin (PowerShell, depuis la racine du projet)
Ouvrir PowerShell dans le projet : dans l'Explorateur, dossier `GGARecomp`, clic droit → « Ouvrir dans le terminal ».

| But | Commande |
|---|---|
| Recompiler après une modification de `ga.toml` ou de `src\...` | `.\tools\regen_all.ps1 -HooksOnly` |
| Tout régénérer (après `config\symbol_addrs.txt`, `tools\gen_splat.py`…) | `.\tools\regen_all.ps1` (≈ 2 min) |
| Idem + recalcul des fonctions à table de saut | `.\tools\regen_all.ps1 -JtblSizes` |
| Lancer le jeu avec journal | `.\build-vs\GGARecompiled.exe --log build\run_log.txt` |
| Fin du journal, derniers mappages, plantage Windows | voir les blocs juste sous le tableau (ils contiennent des `|`) |
| Rapport de violation d'accès | `Get-Content gga_crash.txt` |
| Offset de plantage → fonction | `python tools\crash_where.py 0xA5E0FC` (même compilation que le plantage) |
| Quelle fonction contient une adresse du jeu | `python tools\find_addr.py 0x08006098` (ajouter le n° de fichier pour un seul module : `... 0x08006098 21`) |
| Voir le code assembleur d'une fonction | `Select-String -Path build\splat\asm\*.s -Pattern '^glabel func_08006098_680518' -Context 0,60` |
| Chercher un texte dans le projet | `Select-String -Path src\*\*.c,src\*\*.cpp,ga.toml -Pattern 'ga_module_mapped'` |

Fin du journal, sans les lignes répétitives :
```
Get-Content build\run_log.txt | Where-Object { $_ -notmatch '^\[GGA (TASK|DL)\]' } | Select-Object -Last 40
```
Derniers modules mappés et erreurs de recherche de fonction :
```
Select-String -Path build\run_log.txt -Pattern 'GGA MAP|GGA TLB|Failed' | Select-Object -Last 15
```
Dernier plantage vu par Windows (code d'exception, offset, module fautif) :
```
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'} -MaxEvents 1 | Format-List Message
```

Si une étape de `regen_all.ps1` échoue, envoie au chat le message `ECHEC : ...` + la fin du journal indiqué :
`Get-Content build\build_app_log.txt -Tail 40` (compilation), `build\recomp_log.txt` (N64Recomp), `build\splat_log.txt`, `build\build_elf_log.txt`.

## 3. Appliquer une modification proposée par le chat
- Ouvre le fichier dans un éditeur (Bloc-notes, ou VS Code : `code .` dans le terminal), **Ctrl+F** pour trouver le bloc à
  remplacer, colle le nouveau, enregistre. Garde l'indentation.
- Puis lance la commande indiquée (souvent `.\tools\regen_all.ps1 -HooksOnly`), puis le jeu avec journal, et renvoie le résultat.
- En cas de doute ou de casse : git te permet de revenir en arrière (`git diff` pour voir ce qui a changé, `git checkout -- <fichier>` pour annuler).
- Ne lance jamais de commande qui supprime des dossiers sans savoir ce qu'elle fait (attention : `lib` est une jonction, cf. REPRISE.md § 7).

## 4. Quels fichiers joindre selon la tâche
| Tâche | Fichiers à joindre (en plus de CLAUDE.md, REPRISE.md, ce guide) |
|---|---|
| Plantage « Failed to find function » / modules | `src\game\ga_modules.c`, `src\game\ga_tlb.cpp`, `ga.toml`, `include\gga_recomp\recomp.h` (partie GGA en haut), + les lignes `[GGA MAP]` / `[GGA TLB]` du journal, + la sortie de `find_addr.py` |
| Plantage dans RT64 / affichage | `src\main\rt64_render_context.cpp`, `gga_crash.txt`, sortie de `crash_where.py`, lignes `[GGA DL]` du journal |
| HUD décalé, carrés noirs | captures d'écran + `notes/OVERLAYS.md` ; le chat demandera des fonctions précises (assembleur via la commande du § 2) |
| Noms de fonctions / symboles | `config\symbol_addrs.txt`, `notes/OVERLAYS.md` |
| Comprendre l'architecture | `notes/OVERLAYS.md` (gros fichier : joindre seulement si nécessaire) |

Les très gros fichiers (`RecompiledFuncs\*.c`, `build\splat\asm\*.s`, la ROM) ne s'envoient pas : on en extrait des morceaux avec les commandes du § 2.

## 5. Fin de session
Demande au chat : « Écris le texte à ajouter dans notes/REPRISE.md (état, ce qui a été fait, ce qui reste) ». Colle-le dans le
fichier, puis fais ton commit git. La session suivante repartira de ce fichier.

## 6. Limites à garder en tête
- Sonnet sans le PC ne peut pas vérifier lui-même : demande-lui toujours **une mesure avant une correction** quand la cause n'est pas sûre (une ligne de journal, `find_addr.py`, l'assembleur d'une fonction).
- Les diagnostics compliqués (comme la TLB) ont demandé de lire beaucoup d'assembleur : avance par petites étapes, une hypothèse à la fois.
- Avec un compte ayant Claude Code (Opus), tout ce guide reste valable ; il suffit de dire « Lis CLAUDE.md et notes/REPRISE.md, puis continue ».
