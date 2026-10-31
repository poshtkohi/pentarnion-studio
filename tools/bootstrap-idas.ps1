[CmdletBinding()]
param([string]$ProjectRoot='',
      [string]$MsysRoot=$(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}), [string]$BuildDirectory='')
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$third=Join-Path ([IO.Path]::GetFullPath($ProjectRoot)) 'third-party'
$lock=Get-Content -LiteralPath (Join-Path $third 'idas-dependencies.lock.json') -Raw | ConvertFrom-Json
$archive=Join-Path $third ('downloads\'+$lock.archive)
$sourceParent=Join-Path $third 'sources'
$source=Join-Path $sourceParent $lock.source_directory
if(!$BuildDirectory){$BuildDirectory=Join-Path $third 'build\idas'}
$prefix=Join-Path $third 'idas-sdk'
New-Item -ItemType Directory -Path (Split-Path -Parent $archive),$sourceParent -Force | Out-Null
if(!(Test-Path -LiteralPath $archive)){
    & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 $lock.url -o $archive
    if($LASTEXITCODE -ne 0){throw 'IDAS pinned archive download failed.'}
}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $lock.sha256){throw 'IDAS archive SHA256 mismatch.'}
if(!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))){
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $sourceParent
    if($LASTEXITCODE -ne 0){throw 'IDAS source extraction failed.'}
}
$runtime=Join-Path $third 'windows-deps\ucrt64\bin'
$cmake=Join-Path $runtime 'cmake.exe'
if(!(Test-Path -LiteralPath $cmake)){$cmake=Join-Path $MsysRoot 'ucrt64\bin\cmake.exe'}
if(!(Test-Path -LiteralPath $cmake)){throw 'Run the existing project dependency bootstrap or install CMake in the existing MSYS2 UCRT64 toolchain.'}
$previousPath=$env:PATH
$env:PATH="$runtime;$MsysRoot\ucrt64\bin;$previousPath"
try{
    & $cmake -S $source -B $BuildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_C_COMPILER=$MsysRoot/ucrt64/bin/gcc.exe" '-DBUILD_SHARED_LIBS=OFF' '-DBUILD_STATIC_LIBS=ON' '-DSUNDIALS_ENABLE_CVODE=OFF' '-DSUNDIALS_ENABLE_CVODES=OFF' '-DSUNDIALS_ENABLE_ARKODE=OFF' '-DSUNDIALS_ENABLE_IDA=OFF' '-DSUNDIALS_ENABLE_IDAS=ON' '-DSUNDIALS_ENABLE_KINSOL=OFF' '-DSUNDIALS_ENABLE_C_EXAMPLES=OFF' '-DSUNDIALS_ENABLE_CXX_EXAMPLES=OFF' '-DSUNDIALS_EXAMPLES_INSTALL=OFF' '-DSUNDIALS_ENABLE_MPI=OFF' '-DSUNDIALS_ENABLE_OPENMP=OFF' '-DSUNDIALS_ENABLE_FORTRAN=OFF' '-DSUNDIALS_INDEX_SIZE=64' '-DSUNDIALS_PRECISION=double'
    if($LASTEXITCODE -ne 0){throw 'IDAS configuration failed.'}
    & $cmake --build $BuildDirectory --parallel $buildJobs
    if($LASTEXITCODE -ne 0){throw 'IDAS compilation failed.'}
    & $cmake --install $BuildDirectory
    if($LASTEXITCODE -ne 0){throw 'IDAS local install failed.'}
    $notice=Join-Path $prefix 'share\doc\idas'
    New-Item -ItemType Directory -Path $notice -Force | Out-Null
    foreach($name in @('LICENSE','NOTICE')){Copy-Item -LiteralPath (Join-Path $source $name) -Destination $notice}
}finally{$env:PATH=$previousPath}
Write-Host 'Installed pinned static IDAS locally; the existing MSYS2/global settings were preserved.'
