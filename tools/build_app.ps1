# Configure (au besoin) et compile l'application avec CMake + Ninja + clang-cl de Visual Studio, comme le projet DK64.
# Usage : .\tools\build_app.ps1 [-Target GGARecompiled] [-Configure]      (PowerShell, n'importe ou)
#   -Target RecompiledFuncs : seulement le code genere par N64Recomp (verification rapide)
#   -Configure : refait la configuration CMake meme si build-vs existe
param([string]$Target = "GGARecompiled", [switch]$Configure)
$ErrorActionPreference = "Continue"  # CMake / Ninja ecrivent des avertissements sur stderr ; les echecs sont testes avec $LASTEXITCODE
$root = Split-Path $PSScriptRoot -Parent
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community"
$clang = "$vs\VC\Tools\Llvm\x64\bin\clang-cl.exe"
if (-not (Test-Path $clang)) { throw "clang-cl introuvable : $clang (adapter `$vs dans ce script)" }

# environnement de "x64 Native Tools" (INCLUDE, LIB, PATH...) ; vcvars64.bat a besoin de vswhere.exe dans le PATH
$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:Path"
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
# cmake + ninja de Visual Studio (comme le projet DK64). PAS ceux de msys64\mingw64 : son cmake rate la compilation des ressources (app.rc, RC1107)
$env:Path = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:Path"
$env:Path = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:Path"

Set-Location $root
if ($Configure -or -not (Test-Path "build-vs\CMakeCache.txt")) {
    cmake -S . -B build-vs -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=$clang" "-DCMAKE_CXX_COMPILER=$clang"
    if ($LASTEXITCODE -ne 0) { throw "echec de la configuration CMake" }
}
cmake --build build-vs --target $Target -j8
if ($LASTEXITCODE -ne 0) { throw "echec de la compilation ($Target)" }
Write-Host "OK : $Target" -ForegroundColor Green
