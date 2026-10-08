<#
.SYNOPSIS
    Builds targetlines.dll (32-bit) for Ashita v4.

.DESCRIPTION
    Locates Visual Studio (2022 or newer) with vswhere, enters an x86 developer
    environment, and runs CMake + Ninja. The Ashita v4 SDK folder is taken from
    -SdkPath, or from the ASHITA4_SDK_PATH environment variable.

.EXAMPLE
    .\build.ps1 -SdkPath "C:\Ashita\plugins\sdk"
    .\build.ps1 -SdkPath "C:\Ashita\plugins\sdk" -Config Debug
#>
[CmdletBinding()]
param(
    [string]$SdkPath = $env:ASHITA4_SDK_PATH,
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $SdkPath -or -not (Test-Path (Join-Path $SdkPath 'Ashita.h'))) {
    throw "Ashita v4 SDK not found. Pass -SdkPath <Ashita>\plugins\sdk or set ASHITA4_SDK_PATH."
}
$env:ASHITA4_SDK_PATH = (Resolve-Path $SdkPath).Path

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2022 or newer with the 'Desktop development with C++' workload."
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "No Visual Studio installation with the C++ toolset was found."
}

$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$buildDir = Join-Path $root "out\build\x86-$($Config.ToLower())"
if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Force $buildDir | Out-Null

# Run CMake and Ninja inside the x86 developer command prompt so cl.exe, the
# Windows SDK and Visual Studio's bundled CMake/Ninja are all on PATH.
$script = @"
@echo off
set "ASHITA4_SDK_PATH=$($env:ASHITA4_SDK_PATH)"
call "$devCmd" -arch=x86 -host_arch=x64 -no_logo
if errorlevel 1 exit /b 1
cmake -S "$root" -B "$buildDir" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe "-DASHITA4_SDK_PATH=$($env:ASHITA4_SDK_PATH)"
if errorlevel 1 exit /b 1
cmake --build "$buildDir"
"@
$batch = Join-Path $buildDir 'run-build.cmd'
Set-Content -Path $batch -Value $script -Encoding ASCII

# Native tools write progress to stderr; do not let PowerShell treat that as a failure.
$ErrorActionPreference = 'Continue'
& cmd.exe /c "`"$batch`""
$exitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($exitCode -ne 0) {
    throw "Build failed with exit code $exitCode."
}

$dll = Join-Path $root 'bin\targetlines.dll'
if (Test-Path $dll) {
    Write-Host "Built: $dll"
    Write-Host "Copy it into your Ashita 'plugins' folder and run: /load targetlines"
}
