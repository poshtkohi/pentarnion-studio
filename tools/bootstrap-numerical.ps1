[CmdletBinding()]
param([string]$ProjectRoot='',[string]$LockFile='')
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$third=Join-Path ([IO.Path]::GetFullPath($ProjectRoot)) 'third-party'
if(!$LockFile){$LockFile=Join-Path $third 'numerical-dependencies.lock.json'}
$lock=Get-Content -LiteralPath $LockFile -Raw | ConvertFrom-Json
$cache=Join-Path $third 'downloads';$prefix=Join-Path $third 'windows-deps'
New-Item -ItemType Directory -Path $cache,$prefix -Force | Out-Null
foreach($package in $lock.packages){
    $archive=Join-Path $cache $package.filename
    if(!(Test-Path -LiteralPath $archive)){
        & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 $package.url -o $archive
        if($LASTEXITCODE -ne 0){throw ('Download failed: '+$package.name)}
    }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $package.sha256){throw ('SHA256 mismatch: '+$package.name)}
    $stamp=Join-Path $prefix ('.installed-numerical-'+$package.version+'-'+($package.name -replace '[^a-zA-Z0-9]','_'))
    if(!(Test-Path -LiteralPath $stamp)){
        & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $prefix @($package.payload)
        if($LASTEXITCODE -ne 0){throw ('Extraction failed: '+$package.name)}
        [IO.File]::WriteAllText($stamp,$package.sha256+"`n")
    }
    Write-Output ('Verified local '+$package.name+' '+$package.version)
}
