param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $env:USERPROFILE 'vcpkg' }
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$VsPath = & $VsWhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1
if (-not $VsPath) { throw 'MSVC installation not found.' }
$DevCmd = Join-Path $VsPath 'Common7/Tools/VsDevCmd.bat'
& $env:ComSpec /d /s /c "`"$DevCmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$Installed = Join-Path $VcpkgRoot 'installed/x64-windows'
. (Join-Path $RepoRoot 'Setup/AudioDependency.ps1')
$AudioRoot = Join-Path $VcpkgRoot 'installed'
if (-not (Test-HitoriAudioDependency $AudioRoot)) {
    $AudioRoot = Join-Path $VcpkgRoot 'installed-hitori-audio'
}
if (-not (Test-HitoriAudioDependency $AudioRoot)) { throw 'Run SlnGenerator.bat first.' }
$Audio = Join-Path $AudioRoot 'x64-windows'
$Bin = Join-Path $RepoRoot "Build/Bin/$Configuration-windows-x86_64"
$Output = Join-Path $RepoRoot "Build/HeadBobTests/$Configuration"
New-Item -ItemType Directory -Force $Output | Out-Null
$Runtime = if ($Configuration -eq 'Debug') { '/MDd' } else { '/MD' }
$LibSubdir = if ($Configuration -eq 'Debug') { 'debug/lib' } else { 'lib' }
$BinSubdir = if ($Configuration -eq 'Debug') { 'debug/bin' } else { 'bin' }
$Defines = if ($Configuration -eq 'Debug') { @('/D_DEBUG','/DENGINE_DEBUG') } else { @('/DNDEBUG','/DENGINE_RELEASE') }
Push-Location $RepoRoot
try {
    $Options = @('/nologo','/std:c++20','/EHsc','/utf-8','/Z7','/Od', $Runtime,
        '/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/DUNICODE','/D_UNICODE','/DENGINE_PLATFORM_WINDOWS','/DSOL_ALL_SAFETIES_ON=1',
        '/ISource/Runtime','/ISource/Game','/ISource/ThirdParty/json', "/I$Installed/include", "/I$Installed/include/luajit",
        "/Fo$Output/", "/Fe$Output/HeadBobTests.exe") + $Defines
    & cl.exe @Options Tests/HeadBobTests.cpp /link "/LIBPATH:$Bin" "/LIBPATH:$Audio/$LibSubdir" "/LIBPATH:$Installed/lib" HitoriEngine.lib DirectXTK.lib lua51.lib d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib user32.lib gdi32.lib shell32.lib ole32.lib comdlg32.lib winmm.lib /SUBSYSTEM:CONSOLE
    if ($LASTEXITCODE -ne 0) { throw 'Head bob test compilation failed. Build HitoriEngine first.' }
    $env:PATH = "$Audio/$BinSubdir;$Installed/bin;$env:PATH"
    & (Join-Path $Output 'HeadBobTests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Head bob tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }

