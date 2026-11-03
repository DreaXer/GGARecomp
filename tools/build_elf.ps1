# Assemble la sortie de splat (build/splat) et lie le tout en build/ga.elf avec les relocations (--emit-relocs).
# Usage : .\tools\build_elf.ps1 [-Force]   (depuis n'importe ou ; PowerShell ; apres `splat split config\splat.yaml`)
# Les objets sont recompiles seulement s'ils sont plus anciens que leur source (-Force : tout refaire).
param([switch]$Force)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root   # ga.ld utilise des chemins relatifs "build/splat/..."
$clang = "C:\msys64\mingw64\bin\clang.exe"
$objcopy = "C:\Program Files\LLVM\bin\llvm-objcopy.exe"
$lld = "C:\Program Files\LLVM\bin\ld.lld.exe"
$tmp = "build\tmp_s"
New-Item -ItemType Directory -Force $tmp | Out-Null

# objets attendus = ceux cites par le script de lien
$objs = Select-String -Path build\splat\ga.ld -Pattern 'build/splat/[^\s(]+\.o' -AllMatches |
    ForEach-Object { $_.Matches.Value } | Sort-Object -Unique
$done = 0
foreach ($o in $objs) {
    $src = $o -replace '\.o$', ''
    $srcW = $src -replace '/', '\'
    $oW = $o -replace '/', '\'
    if (-not (Test-Path $srcW)) { throw "source manquante : $srcW" }
    if (-not $Force -and (Test-Path $oW) -and ((Get-Item $oW).LastWriteTime -gt (Get-Item $srcW).LastWriteTime)) { continue }
    if ($src -like '*.bin') {
        # .incbin plutot qu'objcopy : l'objet d'objcopy porte l'ABI n64 et lld le refuse avec o32
        $t = "$tmp\" + ($src -replace '[/\\]', '_') + ".s"
        ".section .data, `"aw`"`n.incbin `"$src`"`n" | Set-Content $t -Encoding ascii -NoNewline
        & $clang --target=mips-linux-gnu -mips3 -mabi=32 -mno-abicalls -c -x assembler $t -o $oW
    } else {
        # clang ne connait pas `.set gp=64` (inutile avec -mips3) ; les directives .ent/.aent/.end sont retirees de include/macro.inc
        $t = "$tmp\" + ($src -replace '[/\\]', '_')
        (Get-Content $srcW -Raw) -replace '(?m)^\s*\.set gp=64\s*$', '' | Set-Content $t -Encoding ascii -NoNewline
        & $clang --target=mips-linux-gnu -mips3 -mabi=32 -mno-abicalls -modd-spreg -c -x assembler -Iinclude $t -o $oW
    }
    if ($LASTEXITCODE -ne 0) { throw "echec : $src" }
    $done++
}
Write-Host "$($objs.Count) objets ($done recompiles)"

# le `/DISCARD/ { *(*); }` de splat fait refuser a lld de jeter .shstrtab : on ne jette que ce qui est inutile
(Get-Content build\splat\ga.ld -Raw) -replace '\*\(\*\);', '*(.MIPS.abiflags) *(.reginfo) *(.gnu.attributes) *(.pdr) *(.mdebug*) *(.comment) *(.options) *(.note*);' |
    Set-Content build\ga.link.ld -Encoding ascii -NoNewline
# ld.lld veut les objets sur la ligne de commande (le script ne suffit pas) ; fichier de reponse pour ne pas depasser la limite Windows
$objs | Set-Content build\objs.rsp -Encoding ascii
$ldArgs = @('-T', 'build\ga.link.ld', '-T', 'build\splat\undefined_syms_auto.ld', '-T', 'build\splat\undefined_funcs_auto.ld',
    '--no-check-sections', '--emit-relocs', '--error-limit=0', '-e', 'entrypoint', '-Map', 'build\ga.map', '-o', 'build\ga.elf', '@build\objs.rsp')
$extra = "build\undefined_local_auto.ld"
Remove-Item $extra -ErrorAction SilentlyContinue
$ErrorActionPreference = "Continue"  # PowerShell 5.1 traite le stderr de lld comme une erreur avec 2>&1
$out = & $lld @ldArgs 2>&1 | Out-String
$ErrorActionPreference = "Stop"
if ($LASTEXITCODE -ne 0) {
    # Etiquettes locales `.L<vram>_<rom>` d'un segment referencees depuis un autre (pointeurs de fonctions de modules 0x08xxxxxx ou d'overlays
    # 0x8017xxxx, adresses partagees par plusieurs fichiers) : on les definit comme constantes absolues = leur adresse virtuelle.
    $und = [regex]::Matches($out, 'undefined symbol: (\S+)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
    $bad = $und | Where-Object { $_ -notmatch '^\.L[0-9A-F]{8}_[0-9A-F]+$' }
    if (-not $und -or $bad) { Write-Host $out; throw "echec du lien (ld.lld) : symboles non resolus ou autre erreur" }
    $und | ForEach-Object { "$_ = 0x$($_.Substring(2, 8));" } | Set-Content $extra -Encoding ascii
    Write-Host "$($und.Count) etiquettes locales definies dans $extra"
    $ldArgs = @('-T', $extra) + $ldArgs
    $ErrorActionPreference = "Continue"
    & $lld @ldArgs 2>&1 | Out-String | Write-Host
    $ErrorActionPreference = "Stop"
    if ($LASTEXITCODE -ne 0) { throw "echec du lien (ld.lld)" }
}
# clang n'ordonne pas les relocations HI16/LO16 comme GNU as ; N64Recomp l'exige (voir tools/fix_relocs.py)
python -I "$PSScriptRoot\fix_relocs.py" build\ga.elf
if ($LASTEXITCODE -ne 0) { throw "echec de fix_relocs.py" }
Write-Host "OK : build\ga.elf" -ForegroundColor Green
