param(
    [string]$ProjectRoot='',
    [string]$CMake="cmake",[string]$Ninja="ninja",
    [string]$Cxx=$(if($env:PENTA_MSYS2_ROOT){Join-Path $env:PENTA_MSYS2_ROOT 'ucrt64/bin/g++.exe'}else{'C:/msys64/ucrt64/bin/g++.exe'}),
    [ValidateRange(0,1024)][int]$Jobs=$(if($env:PENTA_BUILD_JOBS){$env:PENTA_BUILD_JOBS}else{1})
)
$ErrorActionPreference="Stop"
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$Jobs=Get-PentaBuildJobs "$Jobs"
$projectRoot=[IO.Path]::GetFullPath($ProjectRoot)
$dependencyRoot=Join-Path $projectRoot 'third-party'
$archive=Join-Path $dependencyRoot 'mfem-4.10-d964264.zip'
$source=Join-Path $dependencyRoot 'mfem-source-4.10'
$build=Join-Path $dependencyRoot 'mfem-build'
$prefix=Join-Path $dependencyRoot 'mfem-sdk'
$commit='d964264cdb9a13e94a201b6c236c7721e0c8765f'
$expected='cbc7a7c813b4aeaeab50d5825812c88322d8df234b473b1b982fb8b8de7f4625'
$url="https://codeload.github.com/mfem/mfem/zip/$commit"
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $url -OutFile $archive}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){throw 'MFEM archive SHA256 mismatch; no source was extracted.'}
if(!(Test-Path -LiteralPath $source)){
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try{
        if($zip.Entries.Count -gt 20000){throw 'MFEM archive entry bound exceeded.'}
        [long]$total=0
        foreach($entry in $zip.Entries){
            $total+=$entry.Length
            $parts=$entry.FullName.Split('/')
            if($total -gt 350000000 -or $parts[0] -ne "mfem-$commit" -or $parts -contains '..' -or $entry.FullName.Contains('\') -or $entry.FullName.Contains(':')){throw 'MFEM archive path/size validation failed.'}
        }
        foreach($entry in $zip.Entries){
            $relative=$entry.FullName.Substring("mfem-$commit".Length).TrimStart('/')
            if(!$relative){continue}
            $target=[IO.Path]::GetFullPath((Join-Path $source $relative))
            if(!$target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'MFEM extraction escaped the dependency source directory.'}
            if($entry.FullName.EndsWith('/')){New-Item -ItemType Directory -Path $target -Force | Out-Null}
            else{New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null;[IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)}
        }
        [IO.File]::WriteAllText((Join-Path $source '.mfem-pin'),$commit)
    }finally{$zip.Dispose()}
}
if(!(Test-Path -LiteralPath (Join-Path $source '.mfem-pin')) -or [IO.File]::ReadAllText((Join-Path $source '.mfem-pin')).Trim() -ne $commit){throw 'Existing MFEM source is not marked with the expected pin; preserve it and choose a clean dependency directory.'}
$savedPath=$env:PATH
try{
    $env:PATH=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Cxx))+';'+$env:PATH
    & $CMake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$Cxx" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix" -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DMFEM_PRECISION=double -DMFEM_USE_MPI=OFF -DMFEM_USE_METIS=OFF -DMFEM_USE_LAPACK=OFF -DMFEM_USE_EXCEPTIONS=ON -DMFEM_THREAD_SAFE=ON -DMFEM_USE_MEMALLOC=OFF -DMFEM_USE_OPENMP=OFF -DMFEM_USE_LEGACY_OPENMP=OFF -DMFEM_ENABLE_TESTING=OFF -DMFEM_ENABLE_EXAMPLES=OFF -DMFEM_ENABLE_MINIAPPS=OFF
    if($LASTEXITCODE){throw 'MFEM configure failed.'}
    & $CMake --build $build --target install --parallel $Jobs
    if($LASTEXITCODE){throw 'MFEM build/install failed.'}
    $licenses=Join-Path $prefix 'share/licenses/mfem'
    New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'LICENSE'),(Join-Path $source 'NOTICE') -Destination $licenses
}finally{$env:PATH=$savedPath}
