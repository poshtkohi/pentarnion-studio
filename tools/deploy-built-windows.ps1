[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BinDirectory,
      [Parameter(Mandatory=$true)][string]$ProjectRoot,
      [Parameter(Mandatory=$true)][string]$MsysRoot)
$ErrorActionPreference='Stop'
$visited=@{}
foreach ($exe in Get-ChildItem -LiteralPath $BinDirectory -Filter '*.exe') {
    & "$PSScriptRoot\deploy-windows.ps1" -Binary $exe.FullName -ProjectRoot $ProjectRoot -MsysRoot $MsysRoot -VisitedImages $visited
}
# Root shared coordinators may be loaded by lazy modules rather than imported
# directly by an executable. Include their transitive runtime dependencies too.
foreach ($library in Get-ChildItem -LiteralPath $BinDirectory -File -Filter '*.dll') {
    & "$PSScriptRoot\deploy-windows.ps1" -Binary $library.FullName -ProjectRoot $ProjectRoot -MsysRoot $MsysRoot -NoQtPlugins -VisitedImages $visited
}
# Lazy modules are absent from the executable's PE import graph. Deploy their
# dependent runtimes explicitly beside the module that needs them.
$modules=Join-Path $BinDirectory 'modules'
if(Test-Path -LiteralPath $modules){
    foreach($module in Get-ChildItem -LiteralPath $modules -File -Filter '*.dll'){
        & "$PSScriptRoot\deploy-windows.ps1" -Binary $module.FullName -ProjectRoot $ProjectRoot -MsysRoot $MsysRoot -NoQtPlugins -VisitedImages $visited
    }
}
