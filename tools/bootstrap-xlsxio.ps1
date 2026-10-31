[CmdletBinding()]
param([string]$ProjectRoot='',[string]$MsysRoot=$(if($env:PENTA_MSYS2_ROOT){$env:PENTA_MSYS2_ROOT}else{'C:\msys64'}))
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
. (Join-Path $PSScriptRoot 'build-support.ps1')
$buildJobs=Get-PentaBuildJobs
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
$depRoot=Join-Path $ProjectRoot 'third-party'
$lock=Get-Content -LiteralPath (Join-Path $depRoot 'xlsxio.lock.json') -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'xlsxio-windows-utf8.h') -Algorithm SHA256).Hash -ine $lock.patch_sha256.windows_utf8_header){throw 'XLSXIO UTF-8 integration patch hash mismatch'}
if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'patch-xlsxio-styles.cmake') -Algorithm SHA256).Hash -ine $lock.patch_sha256.styles_cmake){throw 'XLSXIO stylesheet integration patch hash mismatch'}
$cache=Join-Path $depRoot 'downloads'
$sourceRoot=Join-Path $depRoot 'sources'
$prefixRoot=Join-Path $depRoot 'windows-deps'
$prefix=Join-Path $prefixRoot 'ucrt64'
$build=Join-Path $depRoot 'build\xlsxio-windows'
New-Item -ItemType Directory -Force -Path $cache,$sourceRoot,$build,$prefixRoot | Out-Null
foreach($item in @($lock.xlsxio,$lock.minizip_windows)) {
    $archive=Join-Path $cache $item.filename
    if(!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $item.sha256) {
        & "$env:SystemRoot\System32\curl.exe" -fsSL --retry 3 --connect-timeout 30 $item.url -o $archive
        if($LASTEXITCODE -ne 0){throw "Download failed: $($item.url)"}
    }
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $item.sha256){throw "Dependency SHA256 mismatch: $archive"}
}
$source=Join-Path $sourceRoot ('xlsxio-'+$lock.xlsxio.version)
if(!(Test-Path -LiteralPath (Join-Path $source 'include\xlsxio_version.h'))) {
    & "$env:SystemRoot\System32\tar.exe" -xf (Join-Path $cache $lock.xlsxio.filename) -C $sourceRoot
    if($LASTEXITCODE -ne 0){throw 'XLSXIO extraction failed'}
}
& "$env:SystemRoot\System32\tar.exe" -xf (Join-Path $cache $lock.minizip_windows.filename) -C $prefixRoot --exclude '.BUILDINFO' --exclude '.MTREE' --exclude '.PKGINFO' --exclude '.INSTALL'
if($LASTEXITCODE -ne 0){throw 'Minizip extraction failed'}
# The upstream Minizip writer uses the Windows ANSI code page for filenames.
# Convert only that path boundary to wide Win32 callbacks; data APIs stay intact.
$writerPath=Join-Path $source 'lib\xlsxio_write.c'
$writer=[IO.File]::ReadAllText($writerPath)
if(!$writer.Contains('#include "penta_xlsxio_windows_utf8.h"')) {
    $needle='DLL_EXPORT_XLSXIO xlsxiowriter xlsxiowrite_open (const char* filename, const char* sheetname)'
    if(!$writer.Contains($needle) -or !$writer.Contains('unlink(filename);') -or !$writer.Contains('zipOpen(handle->filename, 0)')){throw 'The pinned XLSXIO writer source does not match its integration patch'}
    $writer=$writer.Replace($needle,"#include `"penta_xlsxio_windows_utf8.h`"`n`n"+$needle)
    $writer=$writer.Replace('    unlink(filename);','    penta_xlsxio_unlink(filename);')
    $writer=$writer.Replace('zipOpen(handle->filename, 0)','penta_xlsxio_zip_open(handle->filename)')
    [IO.File]::WriteAllText($writerPath,$writer,[Text.UTF8Encoding]::new($false))
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'xlsxio-windows-utf8.h') -Destination (Join-Path $source 'lib\penta_xlsxio_windows_utf8.h') -Force
$cmake=Join-Path $prefix 'bin\cmake.exe'
& $cmake ('-DXLSXIO_SOURCE_DIR='+$source) -P (Join-Path $PSScriptRoot 'patch-xlsxio-styles.cmake')
if($LASTEXITCODE -ne 0){throw 'XLSXIO stylesheet integration patch failed'}
$oldPath=$env:PATH
try {
    $env:PATH=(Join-Path $MsysRoot 'ucrt64\bin')+';'+(Join-Path $prefix 'bin')+';'+$env:SystemRoot+'\System32;'+$env:SystemRoot
    & $cmake -S $source -B $build -G Ninja ('-DCMAKE_C_COMPILER='+ (Join-Path $MsysRoot 'ucrt64\bin\gcc.exe')) ('-DCMAKE_CXX_COMPILER='+ (Join-Path $MsysRoot 'ucrt64\bin\g++.exe')) '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' '-DCMAKE_BUILD_TYPE=Release' ('-DCMAKE_PREFIX_PATH='+$prefix) ('-DCMAKE_INSTALL_PREFIX='+$prefix) '-DBUILD_STATIC=OFF' '-DBUILD_SHARED=ON' '-DBUILD_TOOLS=OFF' '-DBUILD_EXAMPLES=OFF' '-DBUILD_DOCUMENTATION=OFF' '-DBUILD_PC_FILES=ON'
    if($LASTEXITCODE -ne 0){throw 'XLSXIO configure failed'}
    & $cmake --build $build --parallel 2
    if($LASTEXITCODE -ne 0){throw 'XLSXIO build failed'}
    & $cmake --install $build
    if($LASTEXITCODE -ne 0){throw 'XLSXIO install failed'}
} finally {$env:PATH=$oldPath}
$license=Join-Path $prefix 'share\licenses\xlsxio'
New-Item -ItemType Directory -Force -Path $license | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'LICENSE.txt') -Destination (Join-Path $license 'LICENSE.txt') -Force
[IO.File]::WriteAllText((Join-Path $license 'PENTARNION_PATCH.txt'),'Windows UTF-8 filenames use Minizip wide Win32 callbacks; see tools/xlsxio-windows-utf8.h and bootstrap-xlsxio.ps1. SpreadsheetML uses none/gray125 pattern fills and a Normal default style; see tools/patch-xlsxio-styles.cmake. Upstream XLSXIO 0.2.37 is MIT licensed.'+"`n")
$provenance=@{upstream_version=$lock.xlsxio.version;upstream_archive_sha256=$lock.xlsxio.sha256;patched_writer_sha256=(Get-FileHash -LiteralPath $writerPath -Algorithm SHA256).Hash.ToLowerInvariant();windows_utf8_header_sha256=$lock.patch_sha256.windows_utf8_header;stylesheet_patch_sha256=$lock.patch_sha256.styles_cmake}
[IO.File]::WriteAllText((Join-Path $license 'SOURCE_PROVENANCE.json'),($provenance | ConvertTo-Json)+"`n",[Text.UTF8Encoding]::new($false))
Write-Host 'Installed XLSXIO read/write and Minizip in the project SDK. C:/msys64 was reused unchanged.'
