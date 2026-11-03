# Desassemble le code principal en clair de GGA (RAM 0x80000400..0x8003ED10) -> build/main.dis.txt
# Usage : .\tools\disasm_main.ps1   (depuis la racine du projet ; PowerShell)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = "$root\build"
New-Item -ItemType Directory -Force $build | Out-Null

# IPL3 copie la ROM depuis 0x1000 vers 0x80000400 ; le boot efface la BSS a partir de 0x8003ED10 => code+donnees = 0x3E910 octets
$size = 0x3E910
$rom = [IO.File]::ReadAllBytes("$root\baserom.us.z64")
[IO.File]::WriteAllBytes("$build\main.bin", $rom[0x1000..(0x1000 + $size - 1)])

# objcopy du clang de LLVM suffit ; l'objdump de C:\Program Files\LLVM n'a PAS la cible mips : utiliser celui de mingw64
Push-Location $build  # objcopy nomme la section d'apres le chemin : rester dans build/
& "C:\Program Files\LLVM\bin\llvm-objcopy.exe" -I binary -O elf32-bigmips --rename-section .data=.text,alloc,load,readonly,code main.bin main.elf
Pop-Location
# --adjust-vma donne les adresses RAM ; les cibles de jal sont affichees sans le 0x80000000 (ex. jal 0x121d0 = 0x800121d0)
& "C:\msys64\mingw64\bin\llvm-objdump.exe" -d --triple=mips --mcpu=mips3 --adjust-vma=0x80000400 --no-show-raw-insn "$build\main.elf" |
    ForEach-Object { $_ -replace ' <_binary_main_bin_[a-z_]+[+0-9a-fx]*>', '' } |
    Out-File -Encoding utf8 "$build\main.dis.txt"
Write-Host "OK : $build\main.dis.txt"
