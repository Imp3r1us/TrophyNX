# Patches DELTARUNE chapters 1-4 of your own dump for TrophyNX and writes them in the LayeredFS layout.
#   .\patch.ps1 -Romfs D:\dump\romfs -Cli C:\Tools\UTMT_CLI\UndertaleModCli.exe -Out D:\sd
# -> D:\sd\atmosphere\contents\0100A0D022A68000\romfs\chapterN_switch\game.win (N = 1..4): copy that folder to the SD card.
param(
    [Parameter(Mandatory = $true)][string]$Romfs,
    [Parameter(Mandatory = $true)][string]$Cli,
    [string]$Out = "."
)
$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "TrophyNX_trophies.csx"
$dest = Join-Path $Out "atmosphere\contents\0100A0D022A68000\romfs"
foreach ($n in 1..4) {
    $src = Join-Path $Romfs "chapter${n}_switch\game.win"
    if (-not (Test-Path $src)) { throw "missing $src (pass the romfs folder of the dump)" }
    $dir = Join-Path $dest "chapter${n}_switch"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $dst = Join-Path $dir "game.win"
    Write-Host "Chapter ${n}: $src"
    # stdin closed: the CLI waits for input otherwise
    $log = cmd /c "`"$Cli`" load `"$src`" -s `"$script`" -o `"$dst`" -f < NUL 2>&1"
    $log | Where-Object { $_ -match "TrophyNX|rror|xception" } | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0 -or -not ($log -match "TrophyNX: (patched|already patched)")) {
        if (Test-Path $dst) { Remove-Item $dst }
        throw "chapter ${n}: the patch did not apply (see the messages above)"
    }
}
Write-Host "Done. Copy $(Join-Path $Out 'atmosphere') to the root of the SD card."
