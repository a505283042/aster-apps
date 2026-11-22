# Copy ALV1 artistic-lyric font resources to the shared Aster SD folder.
# Safe for Windows USB MSC: does not format/delete anything.
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z]:\\?
$ErrorActionPreference = "Stop"

$root = $Drive.Substring(0, 1).ToUpperInvariant() + ":\"
$asterDir = Join-Path $root "aster"
if (!(Test-Path -LiteralPath $asterDir -PathType Container)) {
    throw "$root is not a confirmed Aster SD volume: the 'aster' folder is missing. Stop and check Windows disk letter."
}

$source = Join-Path $PSScriptRoot "..\text"
$dest = Join-Path $asterDir "shared\hifi\fonts"
$expected = @{
    "paper-serif.bin" = 2518748
    "paper-bold.bin" = 1766506
}
$files = @("paper-serif.bin", "paper-bold.bin", "paper-serif-OFL.txt", "paper-bold-OFL.txt")

# Preflight every source before creating any destination folders.
foreach ($filename in $files) {
    $src = Join-Path $source $filename
    if (!(Test-Path -LiteralPath $src -PathType Leaf)) {
        throw "Missing source: $src. Run from an aster-apps checkout containing HiFiPlayer/text."
    }
    if ($expected.ContainsKey($filename) -and (Get-Item -LiteralPath $src).Length -ne $expected[$filename]) {
        throw "Unexpected ALV1 source length for $filename; do not copy unverified version."
    }
}
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Write-Host "Shared font destination: $dest"

foreach ($filename in $files) {
    $src = Join-Path $source $filename
    $target = Join-Path $dest $filename
    $sha = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash
    $match = (Test-Path -LiteralPath $target -PathType Leaf) -and
             ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $sha)
    if ($match) {
        Write-Host "[SKIP] $filename already matches."
        continue
    }
    Copy-Item -LiteralPath $src -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sha) {
        throw "Copy verification failed: $filename. Eject safely only after repairing the transfer."
    }
    Write-Host "[OK] $filename copied and SHA256 verified."
}
Write-Host "All shared fonts and OFL licenses are verified."

if ($Package) {
    if (!(Test-Path -LiteralPath $Package -PathType Leaf)) {
        throw "Missing APP package: $Package. Build and verify alpha.5 before staging."
    }
    if ([System.IO.Path]::GetFileName($Package) -ne "aster.hifi.ast") {
        throw "Expected package filename aster.hifi.ast, got: $Package"
    }
    $installDir = Join-Path $asterDir "install"
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    $targetPackage = Join-Path $installDir "aster.hifi.ast"
    $sourceHash = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
    if (!(Test-Path -LiteralPath $targetPackage -PathType Leaf) -or
        (Get-FileHash -LiteralPath $targetPackage -Algorithm SHA256).Hash -ne $sourceHash) {
        Copy-Item -LiteralPath $Package -Destination $targetPackage -Force
    }
    if ((Get-FileHash -LiteralPath $targetPackage -Algorithm SHA256).Hash -ne $sourceHash) {
        throw "APP package SHA256 mismatch: $targetPackage"
    }
    $size = (Get-Item -LiteralPath $targetPackage).Length
    Write-Host "[OK] aster.hifi.ast ($size bytes) staged and SHA256 verified."
}
Write-Host "Windows: safely eject the SD disk before returning to FakePod. Then run aster app install /SD:/aster/install/aster.hifi.ast."
)]
    [string]$Drive,

    # Optional built .ast; stage it in the same exclusive USB session.
    [string]$Package
)
$ErrorActionPreference = "Stop"

$root = $Drive.Substring(0, 1).ToUpperInvariant() + ":\"
$asterDir = Join-Path $root "aster"
if (!(Test-Path -LiteralPath $asterDir -PathType Container)) {
    throw "$root is not a confirmed Aster SD volume: the 'aster' folder is missing. Stop and check Windows disk letter."
}

$source = Join-Path $PSScriptRoot "..\text"
$dest = Join-Path $asterDir "shared\hifi\fonts"
$expected = @{
    "paper-serif.bin" = 2518748
    "paper-bold.bin" = 1766506
}
$files = @("paper-serif.bin", "paper-bold.bin", "paper-serif-OFL.txt", "paper-bold-OFL.txt")

# Preflight every source before creating any destination folders.
foreach ($filename in $files) {
    $src = Join-Path $source $filename
    if (!(Test-Path -LiteralPath $src -PathType Leaf)) {
        throw "Missing source: $src. Run from an aster-apps checkout containing HiFiPlayer/text."
    }
    if ($expected.ContainsKey($filename) -and (Get-Item -LiteralPath $src).Length -ne $expected[$filename]) {
        throw "Unexpected ALV1 source length for $filename; do not copy unverified version."
    }
}
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Write-Host "Shared font destination: $dest"

foreach ($filename in $files) {
    $src = Join-Path $source $filename
    $target = Join-Path $dest $filename
    $sha = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash
    $match = (Test-Path -LiteralPath $target -PathType Leaf) -and
             ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $sha)
    if ($match) {
        Write-Host "[SKIP] $filename already matches."
        continue
    }
    Copy-Item -LiteralPath $src -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sha) {
        throw "Copy verification failed: $filename. Eject safely only after repairing the transfer."
    }
    Write-Host "[OK] $filename copied and SHA256 verified."
}
Write-Host "All shared fonts and OFL licenses are verified. Safely eject the SD disk in Windows before returning to the device."
