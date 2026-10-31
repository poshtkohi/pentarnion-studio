<#
.SYNOPSIS
Configure/build Pentarnion with existing UCRT64 dependencies; never downloads.
.EXAMPLE
./tools/build-windows.ps1 -Configuration Release -Jobs 1
.EXAMPLE
./tools/build-windows.ps1 -Headless -Targets penta_algebra_tests -Test -TestRegex '^algebra$'
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Configuration='Release',
    [ValidateSet('Configure','Build','Clean','Rebuild','Check')][string]$Action='Build',
    [string]$SourceDirectory='',
    [string]$BuildDirectory='',
    [string]$DependencyRoot=$env:PENTA_DEPENDENCY_ROOT,
    [string]$MsysRoot=$(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}),
    [string]$Compiler='', [string]$CMake='', [string]$Ninja='',
    [string[]]$PrefixPath=@(), [string[]]$Targets=@(), [string[]]$CMakeArguments=@(),
    [ValidateRange(0,1024)][int]$Jobs=$(if($env:PENTA_BUILD_JOBS){$env:PENTA_BUILD_JOBS}else{1}),
    [ValidateRange(1,86400)][int]$TestTimeout=300,
    [string]$TestRegex='', [switch]$Headless, [switch]$AlgebraOnly, [switch]$Test,
    [switch]$Offscreen, [switch]$Run, [switch]$DryRun, [switch]$Help
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if($Help){
    Write-Output @'
Build Pentarnion from existing local dependencies (no downloads or installation).
  -Configuration Debug|Release|RelWithDebInfo|MinSizeRel (default Release)
  -Action Configure|Build|Clean|Rebuild|Check           (default Build)
  -Jobs N       compile workers; default 1, 0 resolves logical CPU count
  -Headless     omit the Qt IDE, preserving the selected configuration
  -AlgebraOnly  build existing algebra/elementary tests without the Studio SDK
  -Test [-TestRegex REGEX] [-Offscreen] [-TestTimeout SECONDS]
  -Targets NAME,... -CMakeArguments '-DNAME=VALUE',...
  -SourceDirectory DIR -BuildDirectory DIR -DependencyRoot DIR
  -MsysRoot DIR -Compiler FILE -CMake FILE -Ninja FILE -PrefixPath DIR,...
  -DryRun prints commands without changing files; -Run explicitly starts the IDE.
PENTA_MSYS2_ROOT overrides C:\msys64. PENTA_DEPENDENCY_ROOT overrides third-party.
PENTA_BUILD_JOBS controls build workers only; it does not change language threads.
See docs/BUILDING.md for pinned dependencies and explicit preparation steps.
'@
    return
}
if(!$SourceDirectory){$SourceDirectory=Split-Path -Parent $PSScriptRoot}
$source=[IO.Path]::GetFullPath($SourceDirectory)
if(!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt') -PathType Leaf)){throw "No CMakeLists.txt in $source"}
if(!$DependencyRoot){$DependencyRoot=Join-Path $source 'third-party'}
$dependencies=[IO.Path]::GetFullPath($DependencyRoot)
$sdk=Join-Path $dependencies 'windows-deps\ucrt64'
$msys=[IO.Path]::GetFullPath($MsysRoot)
if(!$Compiler){$Compiler=Join-Path $msys 'ucrt64\bin\g++.exe'}
$Compiler=[IO.Path]::GetFullPath($Compiler)
if(!$BuildDirectory){
    $profile='windows-'+$Configuration.ToLowerInvariant()
    if($Headless){$profile+='-headless'}
    if($AlgebraOnly){$profile+='-algebra'}
    $BuildDirectory=Join-Path $source ('build\'+$profile)
}
$build=[IO.Path]::GetFullPath($BuildDirectory)
if($build.TrimEnd('\','/') -eq $source.TrimEnd('\','/')){throw 'Use an out-of-source build directory.'}
if($Run -and ($Headless -or $AlgebraOnly)){throw '-Run requires an IDE build.'}
if($Jobs -eq 0){$Jobs=[Math]::Max(1,[Environment]::ProcessorCount)}

function Resolve-BuildTool([string]$Explicit,[string]$Name){
    if($Explicit){return [IO.Path]::GetFullPath($Explicit)}
    $local=Join-Path $sdk ('bin\'+$Name+'.exe')
    if(Test-Path -LiteralPath $local -PathType Leaf){return $local}
    $found=Get-Command ($Name+'.exe') -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if($found){return $found.Source}
    if($DryRun){return $local}
    throw "Missing $Name. Prepare dependencies explicitly; see docs/BUILDING.md."
}
$CMake=Resolve-BuildTool $CMake 'cmake';$Ninja=Resolve-BuildTool $Ninja 'ninja'
$CTest=Join-Path ([IO.Path]::GetDirectoryName($CMake)) 'ctest.exe'
if(!$DryRun){
    foreach($file in @($Compiler,$CMake,$Ninja)){
        if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing build tool: $file"}
    }
}
$prefixes=@($sdk,(Join-Path $dependencies 'mfem-sdk'),(Join-Path $dependencies 'pagmo-sdk'))+$PrefixPath
if($env:CMAKE_PREFIX_PATH){$prefixes+=@($env:CMAKE_PREFIX_PATH -split ';')}
$project=$source
if($AlgebraOnly){$project=Join-Path $source 'cmake\algebra-check'}
$configure=@('-S',$project,'-B',$build,'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_CXX_COMPILER=$Compiler","-DCMAKE_MAKE_PROGRAM=$Ninja",
    ('-DCMAKE_PREFIX_PATH='+($prefixes -join ';')))
if($AlgebraOnly){$configure+="-DPENTA_SOURCE_ROOT=$source"}
else{
    $configure+=@('-DPENTA_BUILD_TESTS=ON',('-DPENTA_BUILD_IDE='+$(if($Headless){'OFF'}else{'ON'})),
        "-DPENTA_IDAS_ROOT=$(Join-Path $dependencies 'idas-sdk')","-DPENTA_PETSC_SDK=$(Join-Path $dependencies 'petsc-sdk')")
    $serialization=Join-Path $sdk 'lib\cmake\boost_serialization-1.92.0-static'
    if(Test-Path -LiteralPath (Join-Path $serialization 'boost_serialization-config.cmake')){
        $configure+="-Dboost_serialization_DIR=$serialization"
    }
}
$configure+=$CMakeArguments
function Invoke-BuildCommand([string]$Program,[string[]]$Arguments){
    Write-Host ('> '+$Program+' '+(($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '))
    if(!$DryRun){
        & $Program @Arguments
        if($LASTEXITCODE -ne 0){throw "Build command failed with exit code $LASTEXITCODE`: $Program"}
    }
}
$oldPath=$env:PATH;$oldQt=$env:QT_QPA_PLATFORM
try{
    $env:PATH="$sdk\bin;$([IO.Path]::GetDirectoryName($Compiler));$oldPath"
    Invoke-BuildCommand $CMake $configure
    if($Action -eq 'Configure'){return}
    if($Action -eq 'Clean' -or $Action -eq 'Rebuild'){
        Invoke-BuildCommand $CMake @('--build',$build,'--target','clean','--parallel',"$Jobs")
        if($Action -eq 'Clean'){return}
    }
    $arguments=@('--build',$build,'--parallel',"$Jobs")
    if($Targets.Count){$arguments+=@('--target')+$Targets}
    Invoke-BuildCommand $CMake $arguments
    if($Test -or $Action -eq 'Check'){
        if($Offscreen){$env:QT_QPA_PLATFORM='offscreen'}
        $arguments=@('--test-dir',$build,'--output-on-failure','--parallel','1','--timeout',"$TestTimeout",'--no-tests=error')
        if($TestRegex){$arguments+=@('-R',$TestRegex)}
        Invoke-BuildCommand $CTest $arguments
    }
    if($Run){
        $executable=Join-Path $build 'bin\pentarnion-studio.exe'
        if($DryRun){Write-Host "> Start Studio: $executable"}
        else{Start-Process -FilePath $executable -WorkingDirectory (Join-Path $build 'bin')}
    }
}finally{$env:PATH=$oldPath;$env:QT_QPA_PLATFORM=$oldQt}
