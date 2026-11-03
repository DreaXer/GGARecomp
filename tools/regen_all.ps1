# Regenere TOUT le code recompile puis compile l'application (chaine de notes/REPRISE.md, section 6), avec controle.
# Usage (racine du projet, PowerShell) :
#   .\tools\regen_all.ps1               chaine complete (apres un changement de config/symbol_addrs.txt, gen_splat.py...)
#   .\tools\regen_all.ps1 -HooksOnly    seulement N64Recomp + compilation (apres un changement de ga.toml / src/)
#   .\tools\regen_all.ps1 -JtblSizes    chaine complete + recalcul des tailles de fonctions a table de saut
# S'arrete a la premiere erreur. Journal de chaque etape dans build\*_log.txt.
param([switch]$HooksOnly, [switch]$JtblSizes)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$venv = ".\.venv\Scripts\python.exe"

function Step($name, [scriptblock]$cmd) {
    Write-Host "== $name" -ForegroundColor Cyan
    $global:LASTEXITCODE = 0
    & $cmd
    if ($LASTEXITCODE -ne 0) { throw "ECHEC : $name (voir build\*_log.txt)" }
}
function Split() {
    Step "gen_splat" { python tools\gen_splat.py }
    if (Test-Path build\splat) { Remove-Item -Recurse -Force build\splat }
    Step "splat split" { & $venv -m splat split config\splat.yaml *> build\splat_log.txt }
}

if (-not $HooksOnly) {
    Split
    if ($JtblSizes) {
        Step "fix_jtbl_sizes" { python tools\fix_jtbl_sizes.py }
        Split
    }
    Step "build_elf" { & .\tools\build_elf.ps1 -Force *> build\build_elf_log.txt }
    Step "dump-context" { .\N64Recomp.exe ga.dump_context.toml --dump-context *> build\dump_log.txt }
    Move-Item -Force dump.toml syms\ga.syms.toml
    Move-Item -Force data_dump.toml syms\ga.datasyms.toml
    Step "check_chain (ELF == ROM, offsets ROM)" { & $venv tools\check_chain.py }
}
Step "N64Recomp ga.toml" { .\N64Recomp.exe ga.toml *> build\recomp_log.txt }
Step "compilation de l'application" { & .\tools\build_app.ps1 *> build\build_app_log.txt }
Write-Host "OK : build-vs\GGARecompiled.exe" -ForegroundColor Green
