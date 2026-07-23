# Build helper for the ZAMN native port.
#
# The MSVC toolchain, plus the CMake/Ninja bundled with Visual Studio, are not
# on PATH by default. This script sources the VS "x64 Native Tools" environment
# (vcvars64.bat) and configures+builds with Ninja inside that environment.
#
#   pwsh tools/build.ps1            # configure (if needed) + build
#   pwsh tools/build.ps1 -Clean     # wipe build/ first
param([switch]$Clean)
$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
if (-not $vs) { throw "Visual Studio not found via vswhere." }

$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
if (-not (Test-Path $build)) { New-Item -ItemType Directory $build | Out-Null }

# vcvars64.bat puts cl, plus the CMake/Ninja bundled with VS, on PATH. Do NOT
# reset PATH in this same compound line: cmd expands %PATH% at parse time (the
# pre-vcvars value), which would clobber the MSVC paths vcvars just added.
$inner = "`"$vcvars`" && " +
         "cmake -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release && " +
         "cmake --build `"$build`""

cmd /c $inner
if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
Write-Host "`nBuild OK. Binaries in: $build" -ForegroundColor Green
