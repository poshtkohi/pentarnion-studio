[CmdletBinding()]
param([string]$ProjectRoot='',
      [string]$MsysRoot=$(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}))
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$third=Join-Path ([IO.Path]::GetFullPath($ProjectRoot)) 'third-party'
$lock=Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\third-party\fitting-dependencies.lock.json') -Raw | ConvertFrom-Json
$cache=Join-Path $third 'downloads';$sourceParent=Join-Path $third 'sources'
$prefix=Join-Path $third 'windows-deps\ucrt64'
New-Item -ItemType Directory -Path $cache,$sourceParent -Force | Out-Null
foreach($package in $lock.packages){
    $archive=Join-Path $cache $package.filename
    if(!(Test-Path -LiteralPath $archive)){
        & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 $package.url -o $archive
        if($LASTEXITCODE -ne 0){throw ('Download failed: '+$package.name)}
    }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $package.sha256){throw ('Checksum mismatch: '+$package.name)}
    if($package.name -eq 'NLopt'){
        & "$env:SystemRoot\System32\tar.exe" -xf $archive -C (Split-Path -Parent $prefix) @($package.payload)
        if($LASTEXITCODE -ne 0){throw 'NLopt extraction failed'}
        $sourceArchive=Join-Path $cache $package.source.filename
        if(!(Test-Path -LiteralPath $sourceArchive)){
            & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 $package.source.url -o $sourceArchive
            if($LASTEXITCODE -ne 0){throw 'NLopt source download failed'}
        }
        if((Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash -ine $package.source.sha256){throw 'NLopt source checksum mismatch'}
        & "$env:SystemRoot\System32\tar.exe" -xf $sourceArchive -C $sourceParent
        if($LASTEXITCODE -ne 0){throw 'NLopt source extraction failed'}
        $license=Join-Path $prefix 'share\licenses\nlopt'
        New-Item -ItemType Directory -Path $license -Force | Out-Null
        foreach($name in @('COPYING','COPYRIGHT','AUTHORS')){Copy-Item -LiteralPath (Join-Path (Join-Path $sourceParent $package.source.source_directory) $name) -Destination $license}
    } else {
        $source=Join-Path $sourceParent $package.source_directory
        if(!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))){
            & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $sourceParent
            if($LASTEXITCODE -ne 0){throw 'Ceres extraction failed'}
        }
        # Ceres 2.2 predates Eigen 5's CMake major-version compatibility check.
        # Keep Ceres' own >=3.3 checks and build/test against our pinned Eigen;
        # allow its config to be discovered across the major-version boundary.
        $ceresConfig=Join-Path $source 'CMakeLists.txt'
        $configText=[IO.File]::ReadAllText($ceresConfig)
        $configText=$configText.Replace('find_package(Eigen3 3.3 REQUIRED)','find_package(Eigen3 REQUIRED)')
        [IO.File]::WriteAllText($ceresConfig,$configText)
        $fileSource=Join-Path $source 'internal\ceres\file.cc'
        $fileText=[IO.File]::ReadAllText($fileSource)
        if(!$fileText.Contains('#include <cstdint>')){
            [IO.File]::WriteAllText($fileSource,"#include <cstdint>`n"+$fileText)
        }
        $build=Join-Path $third 'build\ceres-eigen'
        $cmake=Join-Path $prefix 'bin\cmake.exe';$previousPath=$env:PATH
        $env:PATH="$prefix\bin;$MsysRoot\ucrt64\bin;$previousPath"
        try {
            & $cmake -S $source -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_PREFIX_PATH=$prefix" "-DCMAKE_CXX_COMPILER=$MsysRoot/ucrt64/bin/g++.exe" '-DBUILD_SHARED_LIBS=ON' '-DBUILD_TESTING=OFF' '-DBUILD_EXAMPLES=OFF' '-DMINIGLOG=ON' '-DGFLAGS=OFF' '-DSUITESPARSE=OFF' '-DLAPACK=OFF' '-DUSE_CUDA=OFF' '-DEIGENSPARSE=ON' '-DEIGENMETIS=OFF' '-DCMAKE_POSITION_INDEPENDENT_CODE=ON'
            if($LASTEXITCODE -ne 0){throw 'Ceres configure failed'}
            & $cmake --build $build --parallel $buildJobs
            if($LASTEXITCODE -ne 0){throw 'Ceres build failed'}
            & $cmake --install $build
            if($LASTEXITCODE -ne 0){throw 'Ceres install failed'}
            $license=Join-Path $prefix 'share\licenses\ceres-solver'
            New-Item -ItemType Directory -Path $license -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination $license
        } finally {$env:PATH=$previousPath}
    }
}
Write-Output 'Pinned Ceres (Eigen, no CUDA/SuiteSparse) and NLopt installed locally.'
