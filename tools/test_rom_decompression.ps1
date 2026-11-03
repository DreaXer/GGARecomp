# Compile et lance le test de la decompression C++ (src/game/rom_decompression.cpp) contre baserom.us.decompressed.z64.
# Usage : .\tools\test_rom_decompression.ps1   (PowerShell, n'importe ou). Ne compile PAS l'application.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mz = "$root\lib\N64ModernRuntime\thirdparty\miniz"   # via la jonction lib\ (voir notes/APPLICATION.md)
$out = "$root\build\test_decomp"
New-Item -ItemType Directory -Force $out | Out-Null
# miniz_export.h est genere par CMake dans l'application : ici une version vide suffit
"#define MINIZ_EXPORT`n" | Set-Content "$out\miniz_export.h" -Encoding ascii

$bin = "C:\msys64\mingw64\bin"
$env:Path = "$bin;" + $env:Path   # DLL de mingw (libstdc++, libwinpthread) pour test.exe
foreach ($c in "miniz.c", "miniz_tdef.c", "miniz_tinfl.c", "miniz_zip.c") {
    & "$bin\clang.exe" -O2 -c "$mz\$c" -I $mz -I $out -o "$out\$($c -replace '\.c$','.o')"
    if ($LASTEXITCODE -ne 0) { throw "echec : $c" }
}
& "$bin\clang++.exe" -std=c++20 -O2 -Wall -Wextra "$root\tools\test_rom_decompression.cpp" "$root\src\game\rom_decompression.cpp" `
    "$out\miniz.o" "$out\miniz_tdef.o" "$out\miniz_tinfl.o" "$out\miniz_zip.o" -I "$root\include" -I $mz -I $out -o "$out\test.exe"
if ($LASTEXITCODE -ne 0) { throw "echec de la compilation du test" }
& "$out\test.exe" "$root\baserom.us.z64" "$root\baserom.us.decompressed.z64"
exit $LASTEXITCODE
