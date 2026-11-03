# Extrait les 4 fichiers de code de GGA et les desassemble -> build/code_<idx>_<vram>.dis.txt
# Usage : .\tools\disasm_code.ps1   (racine du projet ; apres .\tools\ga_rommy.py)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = "$root\build"

python -I "$PSScriptRoot\extract_code.py"
if ($LASTEXITCODE -ne 0) { throw "extract_code.py a echoue" }

Push-Location $build  # objcopy nomme la section d'apres le chemin : rester dans build/
foreach ($f in Get-ChildItem "code_*.bin") {
    $vram = [Convert]::ToUInt32(($f.BaseName -split '_')[2], 16)
    $elf = "$($f.BaseName).elf"
    & "C:\Program Files\LLVM\bin\llvm-objcopy.exe" -I binary -O elf32-bigmips --rename-section .data=.text,alloc,load,readonly,code $f.Name $elf
    # l'objdump de mingw64 a la cible mips ; cibles de jal affichees sans le prefixe 0x80000000
    & "C:\msys64\mingw64\bin\llvm-objdump.exe" -d --triple=mips --mcpu=mips3 --adjust-vma=$vram --no-show-raw-insn $elf |
        ForEach-Object { $_ -replace ' <_binary_[a-z0-9_]+_bin_[a-z_]+[+0-9a-fx]*>', '' } |
        Out-File -Encoding utf8 "$($f.BaseName).dis.txt"
    Write-Host "OK : $($f.BaseName).dis.txt"
}
Pop-Location
