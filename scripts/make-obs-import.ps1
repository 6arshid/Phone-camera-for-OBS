<#
.SYNOPSIS
  Generates an MSVC import library (obs.lib) from the installed OBS Studio
  obs.dll so that the plugin can link against libobs without building the
  whole OBS Studio project.

.DESCRIPTION
  OBS Studio release builds ship obs.dll but not obs.lib.  This script uses
  `dumpbin /exports` to enumerate the exported symbols and `lib /def:` to
  build a COFF import library.  The result is written to deps/obs-import/.

.EXAMPLE
  ./scripts/make-obs-import.ps1
  ./scripts/make-obs-import.ps1 -ObsRoot "C:\Program Files\obs-studio" -OutDir "deps/obs-import"
#>
[CmdletBinding()]
param(
    [string]$ObsRoot = "C:\Program Files\obs-studio",
    [string]$OutDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "deps\obs-import"),
    [string]$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
)

$ErrorActionPreference = "Stop"

function Fail($msg) { Write-Error $msg; exit 1 }

$dll = Join-Path $ObsRoot "bin\64bit\obs.dll"
if (-not (Test-Path $dll)) { Fail "obs.dll not found at $dll (is OBS Studio installed?)" }

if (-not (Test-Path $VsWhere)) { Fail "vswhere.exe not found at $VsWhere (install Visual Studio 2022)" }
$vsPath = & $VsWhere -latest -property installationPath
if (-not $vsPath) { Fail "No Visual Studio installation found" }

$vcTools = Join-Path $vsPath "VC\Tools\MSVC"
$toolset = Get-ChildItem $vcTools -Directory | Sort-Object Name | Select-Object -Last 1
$dumpbin = Join-Path $toolset.FullName "bin\Hostx64\x64\dumpbin.exe"
$libexe  = Join-Path $toolset.FullName "bin\Hostx64\x64\lib.exe"
if (-not (Test-Path $dumpbin)) { Fail "dumpbin.exe not found at $dumpbin" }
if (-not (Test-Path $libexe))  { Fail "lib.exe not found at $libexe" }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

Write-Host "Enumerating exports of $dll ..."
$exports = & $dumpbin /exports $dll | Out-String

$names = @()
foreach ($line in ($exports -split "`r?`n")) {
    if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s+=\s+\S+') {
        $names += $Matches[1]
    } elseif ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') {
        $names += $Matches[1]
    }
}
if ($names.Count -lt 100) { Fail "Only parsed $($names.Count) exports - aborting" }
Write-Host "Parsed $($names.Count) exported symbols"

$defPath = Join-Path $OutDir "obs.def"
$lines = @("LIBRARY obs.dll", "EXPORTS") + ($names | ForEach-Object { "    $_" })
Set-Content -Path $defPath -Value $lines -Encoding ascii

$libPath = Join-Path $OutDir "obs.lib"
if (Test-Path $libPath) { Remove-Item $libPath -Force }
& $libexe /nologo /def:$defPath /out:$libPath /machine:x64 | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "lib.exe failed with exit code $LASTEXITCODE" }

Write-Host "Created $libPath ($([math]::Round((Get-Item $libPath).Length/1KB)) KB)"
