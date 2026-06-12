<#
.SYNOPSIS
    Packages each plugin in this repo into its own release .zip for upload to GitHub Releases.

.DESCRIPTION
    For every plugin folder (one that contains a .uplugin), this builds a clean, drop-in
    archive containing only what an end user needs:

        <Plugin>/
            <Plugin>.uplugin
            Binaries/Win64/*.dll, *.modules   (compiled module + manifest)
            Source/...                        (so it can rebuild on an engine mismatch)
            README.md, LICENSE                (if present)

    Build temp (Intermediate/) is excluded. Debug symbols (*.pdb) are excluded by default;
    pass -IncludePdb to keep them. The version in each zip name is read from the .uplugin's
    "VersionName".

.PARAMETER OutDir
    Where the .zip files are written. Defaults to "dist" at the repo root.

.PARAMETER IncludePdb
    Include *.pdb debug symbols in the archives.

.PARAMETER IncludeSource
    Include the Source/ folder (default: on). Use -IncludeSource:$false to ship binaries only.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File package_releases.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File package_releases.ps1 -IncludePdb -OutDir C:\out
#>
param(
    [string]$OutDir = (Join-Path $PSScriptRoot "dist"),
    [switch]$IncludePdb,
    [bool]$IncludeSource = $true
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot

function Get-PluginVersion {
    param([string]$UpluginPath)
    try {
        $json = Get-Content -Raw -Path $UpluginPath | ConvertFrom-Json
        if ($json.VersionName) { return [string]$json.VersionName }
    } catch { }
    return "1.0.0"
}

# Discover plugins: any direct subfolder containing a *.uplugin
$plugins = Get-ChildItem -Path $Root -Directory | Where-Object {
    Get-ChildItem -Path $_.FullName -Filter *.uplugin -File -ErrorAction SilentlyContinue
}

if (-not $plugins) {
    Write-Error "No plugins (*.uplugin) found under $Root."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

foreach ($plugin in $plugins) {
    $name    = $plugin.Name
    $uplugin = Get-ChildItem -Path $plugin.FullName -Filter *.uplugin -File | Select-Object -First 1
    $version = Get-PluginVersion $uplugin.FullName

    Write-Host "Packaging $name v$version ..." -ForegroundColor Cyan

    $dll = Join-Path $plugin.FullName "Binaries\Win64\UE4Editor-$name.dll"
    if (-not (Test-Path $dll)) {
        Write-Warning "  Skipping $name - not built yet (missing $dll). Build it first."
        continue
    }

    # Stage into a temp folder named after the plugin so it extracts to Plugins/<Plugin>/
    $stageRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("pkg_" + [Guid]::NewGuid().ToString("N"))
    $stage     = Join-Path $stageRoot $name
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    try {
        # .uplugin
        Copy-Item $uplugin.FullName $stage -Force

        # Binaries (dll + .modules always; pdb optional)
        $binSrc = Join-Path $plugin.FullName "Binaries\Win64"
        $binDst = Join-Path $stage "Binaries\Win64"
        New-Item -ItemType Directory -Force -Path $binDst | Out-Null
        Get-ChildItem -Path $binSrc -File | Where-Object {
            $_.Extension -ne ".pdb" -or $IncludePdb
        } | ForEach-Object { Copy-Item $_.FullName $binDst -Force }

        # Resources (icons etc.) if present
        $resSrc = Join-Path $plugin.FullName "Resources"
        if (Test-Path $resSrc) {
            Copy-Item $resSrc (Join-Path $stage "Resources") -Recurse -Force
        }

        # Source (lets users rebuild against their engine build); exclude Intermediate
        $srcSrc = Join-Path $plugin.FullName "Source"
        if ($IncludeSource -and (Test-Path $srcSrc)) {
            Copy-Item $srcSrc (Join-Path $stage "Source") -Recurse -Force
        }

        # Docs
        foreach ($doc in @("README.md", "LICENSE")) {
            $docPath = Join-Path $plugin.FullName $doc
            if (Test-Path $docPath) { Copy-Item $docPath $stage -Force }
        }

        # Zip it
        $zipPath = Join-Path $OutDir "$name-v$version.zip"
        if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
        Compress-Archive -Path $stage -DestinationPath $zipPath -CompressionLevel Optimal

        $sizeKb = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
        Write-Host ("  -> {0} ({1} KB)" -f $zipPath, $sizeKb) -ForegroundColor Green
    }
    finally {
        Remove-Item $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "Done. Release archives are in: $OutDir" -ForegroundColor Cyan
