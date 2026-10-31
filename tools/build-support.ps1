# Shared dependency-preparation helpers. No global installs or network operations.
function Get-PentaBuildJobs([string]$Value=$env:PENTA_BUILD_JOBS){
    if(!$Value){return 1}
    [uint32]$parsed=0
    if($Value -notmatch '^\d+$' -or ![uint32]::TryParse($Value,[ref]$parsed) -or $parsed -gt 1024){
        throw 'PENTA_BUILD_JOBS must be an integer in 0..1024.'
    }
    if($parsed -eq 0){return [Math]::Max(1,[Environment]::ProcessorCount)}
    return [int]$parsed
}

function Assert-PentaOfflineCache([string]$ProjectRoot){
    $third=Join-Path $ProjectRoot 'third-party';$requirements=@{}
    function Add-CacheRequirement([string]$Name,[string]$Hash,[string]$Relative='downloads'){
        if($Name -match '[\\/]' -or $Name -in @('.','..')){throw "Invalid cache filename: $Name"}
        if($Hash -notmatch '^[a-fA-F0-9]{64}$'){throw "Invalid cache checksum for $Name"}
        $path=Join-Path (Join-Path $third $Relative) $Name
        if($requirements.ContainsKey($path) -and $requirements[$path] -ine $Hash){throw "Conflicting cache pins: $Name"}
        $requirements[$path]=$Hash
    }
    function Visit-CacheNode($Node){
        if($null -eq $Node -or $Node -is [string] -or $Node -is [ValueType]){return}
        if($Node -is [array]){foreach($item in $Node){Visit-CacheNode $item};return}
        $properties=$Node.PSObject.Properties
        if($properties['sha256']){
            if($properties['filename']){Add-CacheRequirement $Node.filename $Node.sha256}
            elseif($properties['archive']){Add-CacheRequirement $Node.archive $Node.sha256}
            elseif($properties['commit'] -and $properties['name'] -and $Node.name -eq 'JKQTMathText'){
                Add-CacheRequirement ('JKQtPlotter-'+$Node.commit+'.zip') $Node.sha256
            }
        }
        foreach($property in $properties){Visit-CacheNode $property.Value}
    }
    foreach($name in @('dependencies.lock.json','numerical-dependencies.lock.json','mathtext.lock.json',
        'sundials.lock.json','idas-dependencies.lock.json','fitting-dependencies.lock.json',
        'evolution-dependencies.lock.json','xlsxio.lock.json')){
        $path=Join-Path $third $name
        if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing dependency lock: $path"}
        Visit-CacheNode (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
    }
    $mfem=Get-Content -LiteralPath (Join-Path $third 'mfem-4.10-pin.json') -Raw | ConvertFrom-Json
    Add-CacheRequirement 'mfem-4.10-d964264.zip' $mfem.archive_sha256 '.'
    $missing=@()
    foreach($path in ($requirements.Keys | Sort-Object)){
        if(!(Test-Path -LiteralPath $path -PathType Leaf)){$missing+=$path;continue}
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $requirements[$path]){
            throw "Offline dependency checksum mismatch: $path"
        }
    }
    if($missing.Count){throw ('Offline cache is incomplete; no preparation was started. Missing: '+($missing -join ', '))}
    Write-Host "Verified $($requirements.Count) pinned offline dependency archives."
}
