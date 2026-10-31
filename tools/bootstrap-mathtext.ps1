[CmdletBinding()]
param([string]$ProjectRoot='',
      [string]$MsysRoot = $(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}))
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$third=Join-Path ([IO.Path]::GetFullPath($ProjectRoot)) 'third-party'
$lock=Get-Content -LiteralPath (Join-Path $third 'mathtext.lock.json') -Raw | ConvertFrom-Json
$archive=Join-Path $third ('downloads\JKQtPlotter-'+$lock.commit+'.zip')
$sourceParent=Join-Path $third 'sources'
$source=Join-Path $sourceParent ('JKQtPlotter-'+$lock.commit)
$build=Join-Path $third 'build\jkqtmathtext'
$prefix=Join-Path $third 'windows-deps\ucrt64'
New-Item -ItemType Directory -Path (Split-Path -Parent $archive),$sourceParent -Force | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 $lock.url -o $archive
    if ($LASTEXITCODE -ne 0) {throw 'Math-text source download failed.'}
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $lock.sha256) {throw 'Math-text archive SHA256 mismatch.'}
if (!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $sourceParent
    if ($LASTEXITCODE -ne 0) {throw 'Math-text source extraction failed.'}
}
$cmake=Join-Path $prefix 'bin\cmake.exe'
$previousPath=$env:PATH
$env:PATH="$prefix\bin;$MsysRoot\ucrt64\bin;$previousPath"
try {
    & $cmake -S $source -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$prefix" "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_CXX_COMPILER=$MsysRoot/ucrt64/bin/g++.exe" '-DBUILD_SHARED_LIBS=ON' '-DQT_VERSION_MAJOR=6' '-DJKQtPlotter_BUILD_EXAMPLES=OFF' '-DJKQtPlotter_BUILD_TOOLS=OFF' '-DJKQtPlotter_BUILD_TESTS=OFF' '-DJKQtPlotter_BUILD_LIB_JKQTPLOTTER=OFF' '-DJKQtPlotter_BUILD_LIB_JKQTMATH=OFF' '-DJKQtPlotter_BUILD_LIB_JKQTFASTPLOTTER=OFF' '-DJKQtPlotter_BUILD_LIB_JKQTMATHTEXT=ON' '-DJKQtPlotter_BUILD_INCLUDE_XITS_FONTS=ON' '-DJKQtPlotter_BUILD_INCLUDE_FIRAMATH_FONTS=OFF' '-DJKQtPlotter_BUILD_FORCE_NO_PRINTER_SUPPORT=ON' '-DJKQtPlotter_BUILD_DECORATE_LIBNAMES_WITH_BUILDTYPE=OFF'
    if ($LASTEXITCODE -ne 0) {throw 'Math-text configure failed.'}
    & $cmake --build $build --parallel $buildJobs
    if ($LASTEXITCODE -ne 0) {throw 'Math-text build failed.'}
    & $cmake --install $build
    if ($LASTEXITCODE -ne 0) {throw 'Math-text install failed.'}
} finally {$env:PATH=$previousPath}
Write-Host 'Installed pinned JKQTMathText and bundled math fonts locally.'
