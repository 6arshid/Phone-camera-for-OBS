<#
.SYNOPSIS
  Clones the OBS Studio sources of a given version (shallow) into
  deps/obs-headers so the plugin can compile against the official
  libobs headers.

.EXAMPLE
  ./scripts/fetch-obs-headers.ps1
  ./scripts/fetch-obs-headers.ps1 -Version 32.0.4
#>
[CmdletBinding()]
param(
    [string]$Version = "32.0.4",
    [string]$OutDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "deps\obs-headers")
)

$ErrorActionPreference = "Stop"
$repo = Join-Path $OutDir "obs-studio"

if (Test-Path (Join-Path $repo "libobs\obs.h")) {
    Write-Host "OBS headers already present at $repo"
    exit 0
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Write-Host "Cloning obs-studio $Version (shallow) ..."
& git clone --depth 1 --branch $Version https://github.com/obsproject/obs-studio.git $repo
if ($LASTEXITCODE -ne 0) { Write-Error "git clone failed"; exit 1 }

if (-not (Test-Path (Join-Path $repo "libobs\obs.h"))) { Write-Error "clone succeeded but libobs/obs.h missing"; exit 1 }
Write-Host "OBS headers ready: $repo"
