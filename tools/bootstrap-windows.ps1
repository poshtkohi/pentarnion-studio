[CmdletBinding()]
param([string]$ProjectRoot='', [string]$LockPath, [switch]$Offline)
$ErrorActionPreference = 'Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
if($Offline){Assert-PentaOfflineCache $ProjectRoot}
$thirdParty = Join-Path $ProjectRoot 'third-party'
$cache = Join-Path $thirdParty 'downloads'
$prefix = Join-Path $thirdParty 'windows-deps'
$lockPath = if ($LockPath) { [IO.Path]::GetFullPath($LockPath) } else { Join-Path $thirdParty 'dependencies.lock.json' }
if (!(Test-Path -LiteralPath $lockPath)) { throw "Missing dependency lock: $lockPath" }
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $cache,$prefix -Force | Out-Null
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
$curl = Join-Path $env:SystemRoot 'System32\curl.exe'
$installed = @()
function Get-Sha256([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','') }
    finally { $stream.Dispose(); $hash.Dispose() }
}
foreach ($package in $lock.packages) {
    $archive = Join-Path $cache $package.filename
    $stamp = Join-Path $prefix ('.installed-' + $package.name + '-' + $package.version)
    if (!(Test-Path -LiteralPath $archive) -or (Get-Sha256 $archive) -ine $package.sha256) {
        Write-Host "Downloading $($package.name) $($package.version)"
        & $curl -fsSL --retry 3 --connect-timeout 30 $package.url -o $archive
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($package.url)" }
    }
    if ((Get-Sha256 $archive) -ine $package.sha256) {
        throw "SHA256 verification failed for $archive"
    }
    if (!(Test-Path -LiteralPath $stamp)) {
        # Only the package's UCRT64 payload is installed; its archive metadata
        # remains available in downloads. Nothing is written to C:\msys64.
        & $tar -xf $archive -C $prefix --exclude '.BUILDINFO' --exclude '.MTREE' --exclude '.PKGINFO' --exclude '.INSTALL'
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $archive" }
        [IO.File]::WriteAllText($stamp, $package.sha256 + "`n")
    }
    $installed += "$($package.name) $($package.version) SHA256=$($package.sha256)"
}
[IO.File]::WriteAllLines((Join-Path $thirdParty 'INSTALLED.txt'), $installed)
Write-Host "Verified $($installed.Count) project-local packages in $prefix"
if (Test-Path -LiteralPath (Join-Path $ProjectRoot 'third-party\mathtext.lock.json')) {
    & (Join-Path $ProjectRoot 'tools\bootstrap-mathtext.ps1') -ProjectRoot $ProjectRoot
}
Write-Host 'Next: tools\build.ps1 -Configuration Release -Test'
if (Test-Path -LiteralPath (Join-Path $ProjectRoot 'third-party\sundials.lock.json')) {
    & (Join-Path $ProjectRoot 'tools\bootstrap-sundials.ps1') -ProjectRoot $ProjectRoot
}
if (Test-Path -LiteralPath (Join-Path $ProjectRoot 'third-party\numerical-dependencies.lock.json')) {
    & (Join-Path $ProjectRoot 'tools\bootstrap-numerical.ps1') -ProjectRoot $ProjectRoot
}
if (Test-Path -LiteralPath (Join-Path $ProjectRoot 'third-party\fitting-dependencies.lock.json')) {
    & (Join-Path $ProjectRoot 'tools\bootstrap-fitting.ps1') -ProjectRoot $ProjectRoot
}
foreach($script in @('bootstrap-xlsxio.ps1','bootstrap-evolution.ps1','bootstrap-idas.ps1')){
    & (Join-Path $ProjectRoot ('tools\'+$script)) -ProjectRoot $ProjectRoot
}
$mfemMsysRoot=if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}
& (Join-Path $ProjectRoot 'tools\bootstrap-mfem.ps1') -ProjectRoot $ProjectRoot `
    -CMake (Join-Path $prefix 'ucrt64\bin\cmake.exe') `
    -Ninja (Join-Path $prefix 'ucrt64\bin\ninja.exe') `
    -Cxx (Join-Path $mfemMsysRoot 'ucrt64\bin\g++.exe')
