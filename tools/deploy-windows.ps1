[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Binary,
      [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
      [string]$MsysRoot = $(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}),
      [switch]$NoQtPlugins,
      [hashtable]$VisitedImages)
$ErrorActionPreference = 'Stop'
$sdk = Join-Path $ProjectRoot 'third-party\windows-deps\ucrt64'
$destination = Split-Path -Parent $Binary
$objdump = Join-Path $MsysRoot 'ucrt64\bin\objdump.exe'
if (!(Test-Path -LiteralPath $Binary)) { throw "No built executable: $Binary" }
if (!(Test-Path -LiteralPath $objdump)) { throw "UCRT64 objdump not found: $objdump" }
$seen = if($null -ne $VisitedImages){$VisitedImages}else{@{}}
function Get-Sha256([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','') }
    finally { $stream.Dispose(); $hash.Dispose() }
}
function Copy-Runtime([string]$image) {
    $key = ([IO.Path]::GetFullPath($destination)+'|'+[IO.Path]::GetFullPath($image)).ToLowerInvariant()
    if ($seen.ContainsKey($key)) { return }
    $seen[$key] = $true
    $imports = & $objdump -p $image
    if ($LASTEXITCODE -ne 0) { throw "Cannot read PE imports: $image" }
    foreach ($line in $imports) {
        if ($line -match 'DLL Name:\s*(\S+)') {
            $dll = $Matches[1]
            $source = Join-Path $sdk ('bin\'+$dll)
            if (!(Test-Path -LiteralPath $source)) { $source = Join-Path $MsysRoot ('ucrt64\bin\'+$dll) }
            if (Test-Path -LiteralPath $source) {
                $dest = Join-Path $destination $dll
                if (!(Test-Path -LiteralPath $dest) -or (Get-Sha256 $source) -ne (Get-Sha256 $dest)) {
                    Copy-Item -LiteralPath $source -Destination $dest
                }
                Copy-Runtime $source
            }
        }
    }
}
Copy-Runtime $Binary
if($NoQtPlugins){return}
$platformDestination = Join-Path $destination 'platforms'
New-Item -ItemType Directory -Path $platformDestination -Force | Out-Null
foreach ($plugin in @('qwindows.dll','qoffscreen.dll','qminimal.dll')) {
    $source = Join-Path $sdk ('share\qt6\plugins\platforms\'+$plugin)
    $target = Join-Path $platformDestination $plugin
    if (!(Test-Path -LiteralPath $target) -or (Get-Sha256 $source) -ne (Get-Sha256 $target)) {
        Copy-Item -LiteralPath $source -Destination $target
    }
    Copy-Runtime $source
}
# QImage's JPEG encoder and SVG/icon readers are runtime plugins. They are not
# reachable from the executable's PE imports or the platform-plugin closure.
# Qt Network discovers its TLS backends dynamically when an explicit Copilot
# request uses HTTPS. Include these plugins and their PE dependency closure.
foreach ($family in @('imageformats','iconengines','tls')) {
    $pluginSource = Join-Path $sdk ('share\qt6\plugins\'+$family)
    if (!(Test-Path -LiteralPath $pluginSource -PathType Container)) { continue }
    $pluginDestination = Join-Path $destination $family
    New-Item -ItemType Directory -Path $pluginDestination -Force | Out-Null
    foreach ($plugin in Get-ChildItem -LiteralPath $pluginSource -File -Filter '*.dll') {
        $target = Join-Path $pluginDestination $plugin.Name
        if (!(Test-Path -LiteralPath $target) -or (Get-Sha256 $plugin.FullName) -ne (Get-Sha256 $target)) {
            Copy-Item -LiteralPath $plugin.FullName -Destination $target
        }
        Copy-Runtime $plugin.FullName
    }
}
# Qt's OpenSSL TLS backend resolves these libraries dynamically, so objdump
# cannot discover them from its PE imports. Schannel remains available too.
if(Test-Path -LiteralPath (Join-Path $destination 'tls\qopensslbackend.dll')){
    foreach($name in @('libssl-3-x64.dll','libcrypto-3-x64.dll')){
        $source=Join-Path $sdk ('bin\'+$name)
        if(!(Test-Path -LiteralPath $source)){throw "OpenSSL TLS runtime missing: $name"}
        $target=Join-Path $destination $name
        if(!(Test-Path -LiteralPath $target) -or (Get-Sha256 $source) -ne (Get-Sha256 $target)){Copy-Item -LiteralPath $source -Destination $target}
        Copy-Runtime $source
    }
}
[IO.File]::WriteAllText((Join-Path $destination 'qt.conf'), "[Paths]`nPlugins=.`n")
