[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [string]$Project)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $root ('build\windows-'+$Configuration.ToLowerInvariant()+'\bin\pentarnion-studio.exe')
if (!(Test-Path -LiteralPath $exe)) { throw 'Build the application first with tools\build.ps1.' }
if ($Project) {
    Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList @('--project',('"'+[IO.Path]::GetFullPath($Project)+'"'))
} else {
    Start-Process -FilePath $exe -WorkingDirectory $root
}
