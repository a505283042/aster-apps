# Copy artistic ALV1 fonts and optionally a built .ast through Windows USB MSC.
# Does not delete or format any files; checks every copied file with SHA256.
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z]:\\?$')]
    [string]$Drive,
    [string]$Package
)
$ErrorActionPreference = 'Stop'

$root = $Drive.Substring(0, 1).ToUpperInvariant() + ':\'
$asterDir = Join-Path $root 'aster'
if (!(Test-Path -LiteralPath $asterDir -PathType Container)) {
    throw "$root is not an Aster TF card: missing 'aster' folder. Check the Windows drive letter."
}

$sourceDir = Join-Path $PSScriptRoot '..\text'
$destination = Join-Path $asterDir 'shared\hifi\fonts'
$expectedLengths = @{
    'paper-serif.bin' = 2518748
    'paper-bold.bin' = 1766506
}
$files = @('paper-serif.bin', 'paper-bold.bin',
           'paper-serif-OFL.txt', 'paper-bold-OFL.txt')

# Preflight all sources before touching the destination.
foreach ($name in $files) {
    $sourcePath = Join-Path $sourceDir $name
    if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Source missing: $sourcePath"
    }
    if ($expectedLengths.ContainsKey($name) -and
        (Get-Item -LiteralPath $sourcePath).Length -ne $expectedLengths[$name]) {
        throw "Incorrect $name length; refuse an unexpected font revision."
    }
}

New-Item -ItemType Directory -Path $destination -Force | Out-Null
Write-Host "Font destination: $destination"
foreach ($name in $files) {
    $sourcePath = Join-Path $sourceDir $name
    $targetPath = Join-Path $destination $name
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $matches = $false
    if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
        $matches = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -eq $sourceHash
    }
    if ($matches) {
        Write-Host "[SKIP] $name unchanged"
    } else {
        Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
        if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $sourceHash) {
            throw "SHA256 mismatch after copying $name"
        }
        Write-Host "[OK] $name verified"
    }
}

if ($Package) {
    if (!(Test-Path -LiteralPath $Package -PathType Leaf)) {
        throw "APP package not found: $Package"
    }
    if ([System.IO.Path]::GetFileName($Package) -ne 'aster.hifi.ast') {
        throw "This script only stages aster.hifi.ast"
    }
    $installDir = Join-Path $asterDir 'install'
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    $targetPackage = Join-Path $installDir 'aster.hifi.ast'
    $hash = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
    $copyPackage = $true
    if (Test-Path -LiteralPath $targetPackage -PathType Leaf) {
        $copyPackage = (Get-FileHash -LiteralPath $targetPackage -Algorithm SHA256).Hash -ne $hash
    }
    if ($copyPackage) {
        Copy-Item -LiteralPath $Package -Destination $targetPackage -Force
    }
    if ((Get-FileHash -LiteralPath $targetPackage -Algorithm SHA256).Hash -ne $hash) {
        throw "APP package SHA256 verification failed"
    }
    Write-Host "[OK] APP package verified: $targetPackage"
}

Write-Host "Copy succeeded. Eject the TF card safely in Windows and let FakePod remount it."
