# Compatibility entry used by the checked-in Code::Blocks project.
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release',
      [ValidateSet('Configure','Build','Clean','Rebuild','Check')][string]$Action='Build',
      [switch]$Test,[switch]$Headless,[switch]$Run,[switch]$Offscreen,[switch]$DryRun,[switch]$Help,
      [ValidateRange(0,1024)][int]$Jobs=$(if($env:PENTA_BUILD_JOBS){$env:PENTA_BUILD_JOBS}else{1}),
      [string]$MsysRoot=$(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}))
$forward=@{Configuration=$Configuration;Action=$Action;Test=$Test;Headless=$Headless;Run=$Run;
    Offscreen=$Offscreen;DryRun=$DryRun;Help=$Help;Jobs=$Jobs;MsysRoot=$MsysRoot}
& (Join-Path $PSScriptRoot 'build-windows.ps1') @forward
