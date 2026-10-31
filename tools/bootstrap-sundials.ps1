[CmdletBinding()]
param([string]$ProjectRoot='',
      [string]$LockFile = '',
      [string]$MsysRoot = $(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}))
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$third=Join-Path ([IO.Path]::GetFullPath($ProjectRoot)) 'third-party'
if (!$LockFile) {$LockFile=Join-Path $third 'sundials.lock.json'}
$lock=Get-Content -LiteralPath $LockFile -Raw | ConvertFrom-Json
$archive=Join-Path $third ('downloads\'+$lock.archive)
$sourceParent=Join-Path $third 'sources'
$source=Join-Path $sourceParent $lock.source_directory
$build=Join-Path $third 'build\sundials-cvode-arkode'
$prefix=Join-Path $third 'windows-deps\ucrt64'
New-Item -ItemType Directory -Path (Split-Path -Parent $archive),$sourceParent -Force | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 $lock.url -o $archive
    if ($LASTEXITCODE -ne 0) {throw 'SUNDIALS source download failed.'}
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $lock.sha256) {throw 'SUNDIALS archive SHA256 mismatch.'}
if (!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $sourceParent
    if ($LASTEXITCODE -ne 0) {throw 'SUNDIALS source extraction failed.'}
}
$cmake=Join-Path $prefix 'bin\cmake.exe'
$previousPath=$env:PATH
$env:PATH="$prefix\bin;$MsysRoot\ucrt64\bin;$previousPath"
try {
    & $cmake -S $source -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_C_COMPILER=$MsysRoot/ucrt64/bin/gcc.exe" '-DBUILD_SHARED_LIBS=OFF' '-DBUILD_STATIC_LIBS=ON' '-DSUNDIALS_ENABLE_CVODE=ON' '-DSUNDIALS_ENABLE_ARKODE=ON' '-DSUNDIALS_ENABLE_CVODES=ON' '-DSUNDIALS_ENABLE_IDA=OFF' '-DSUNDIALS_ENABLE_IDAS=OFF' '-DSUNDIALS_ENABLE_KINSOL=ON' '-DSUNDIALS_ENABLE_C_EXAMPLES=OFF' '-DSUNDIALS_ENABLE_CXX_EXAMPLES=OFF' '-DSUNDIALS_EXAMPLES_INSTALL=OFF' '-DSUNDIALS_ENABLE_MPI=OFF' '-DSUNDIALS_ENABLE_OPENMP=OFF' '-DSUNDIALS_ENABLE_FORTRAN=OFF' '-DSUNDIALS_INDEX_SIZE=64' '-DSUNDIALS_PRECISION=double'
    if ($LASTEXITCODE -ne 0) {throw 'SUNDIALS configure failed.'}
    & $cmake --build $build --parallel $buildJobs
    if ($LASTEXITCODE -ne 0) {throw 'SUNDIALS build failed.'}
    & $cmake --install $build
    if ($LASTEXITCODE -ne 0) {throw 'SUNDIALS install failed.'}
    $licenseTarget=Join-Path $prefix 'share\doc\sundials'
    New-Item -ItemType Directory -Path $licenseTarget -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination $licenseTarget
} finally {$env:PATH=$previousPath}
Write-Host 'Installed pinned SUNDIALS CVODE, CVODES, ARKODE and KINSOL locally; existing MSYS2 was reused.'

