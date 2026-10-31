[CmdletBinding()]
param([string]$ProjectRoot='',
      [string]$MsysRoot=$(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}))
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$third=Join-Path ([IO.Path]::GetFullPath($ProjectRoot)) 'third-party'
$package=(Get-Content -LiteralPath (Join-Path $third 'evolution-dependencies.lock.json') -Raw | ConvertFrom-Json).packages[0]
$downloads=Join-Path $third 'downloads'
$sources=Join-Path $third 'sources'
$prefix=Join-Path $third 'pagmo-sdk'
$sdk=Join-Path $third 'windows-deps\ucrt64'
New-Item -ItemType Directory -Path $downloads,$sources -Force | Out-Null
$archive=Join-Path $downloads $package.filename
if(!(Test-Path -LiteralPath $archive)){
    & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 $package.url -o $archive
    if($LASTEXITCODE -ne 0){throw 'pagmo2 download failed'}
}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $package.sha256){throw 'pagmo2 source hash mismatch'}
$source=Join-Path $sources $package.source_directory
if(!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))){
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $sources
    if($LASTEXITCODE -ne 0){throw 'pagmo2 extraction failed'}
}
$config=Join-Path $source 'CMakeLists.txt'
$text=[IO.File]::ReadAllText($config).Replace('find_package(Eigen3 3.3 REQUIRED NO_MODULE)','find_package(Eigen3 REQUIRED NO_MODULE)')
[IO.File]::WriteAllText($config,$text,[Text.UTF8Encoding]::new($false))
$config=Join-Path $source 'config.hpp.in'
$text=[IO.File]::ReadAllText($config)
if(!$text.Contains('#if __cplusplus >= 202002L')){
    $text=$text.Replace('#include <ciso646>',"#if __cplusplus >= 202002L`n#include <version>`n#else`n#include <ciso646>`n#endif")
    [IO.File]::WriteAllText($config,$text,[Text.UTF8Encoding]::new($false))
}
& (Join-Path $PSScriptRoot 'patch-pagmo-state.ps1') -SourceRoot $source
$build=Join-Path $third 'build\pagmo2'
$previousPath=$env:PATH
$env:PATH="$sdk\bin;$MsysRoot\ucrt64\bin;$previousPath"
try{
    $cmake=Join-Path $sdk 'bin\cmake.exe'
    & $cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$sdk/bin/ninja.exe" "-DCMAKE_CXX_COMPILER=$MsysRoot/ucrt64/bin/g++.exe" "-DCMAKE_C_COMPILER=$MsysRoot/ucrt64/bin/gcc.exe" '-DCMAKE_BUILD_TYPE=Release' '-DPAGMO_WITH_EIGEN3=ON' '-DPAGMO_BUILD_STATIC_LIBRARY=ON' "-Dboost_serialization_DIR=$sdk/lib/cmake/boost_serialization-1.92.0-static" '-DPAGMO_BUILD_TESTS=OFF' '-DPAGMO_BUILD_BENCHMARKS=OFF' "-DCMAKE_PREFIX_PATH=$sdk" "-DCMAKE_INSTALL_PREFIX=$prefix"
    if($LASTEXITCODE -ne 0){throw 'pagmo2 configure failed (Eigen, TBB and Boost.Serialization required)'}
    & $cmake --build $build --parallel $buildJobs
    if($LASTEXITCODE -ne 0){throw 'pagmo2 build failed'}
    & $cmake --install $build
    if($LASTEXITCODE -ne 0){throw 'pagmo2 installation failed'}
    $notices=Join-Path $prefix 'share\licenses\pagmo2'
    New-Item -ItemType Directory -Path $notices -Force | Out-Null
    foreach($name in @('COPYING.gpl3','COPYING.lgpl3')){Copy-Item -LiteralPath (Join-Path $source $name) -Destination $notices}
}finally{$env:PATH=$previousPath}
Write-Output 'Pinned pagmo2 evolutionary backend installed under third-party/pagmo-sdk. Global MSYS2 was not modified.'
