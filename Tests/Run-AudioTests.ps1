param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $env:USERPROFILE 'vcpkg' }
. (Join-Path $RepoRoot 'Setup/AudioDependency.ps1')
$AudioRoot = Join-Path $VcpkgRoot 'installed'
if (-not (Test-HitoriAudioDependency $AudioRoot)) { $AudioRoot = Join-Path $VcpkgRoot 'installed-hitori-audio' }
if (-not (Test-HitoriAudioDependency $AudioRoot)) { throw 'Run SlnGenerator.bat first.' }
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$VsPath = & $VsWhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $VsPath) { throw 'MSVC not found.' }
$DevCmd = Join-Path $VsPath 'Common7/Tools/VsDevCmd.bat'
& $env:ComSpec /d /s /c "`"$DevCmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$Installed = Join-Path $VcpkgRoot 'installed/x64-windows'
$Audio = Join-Path $AudioRoot 'x64-windows'
$Output = Join-Path $RepoRoot 'Build/AudioTests'
New-Item -ItemType Directory -Force $Output | Out-Null
$Runtime = if ($Configuration -eq 'Debug') { '/MDd' } else { '/MD' }
$LibSubdir = if ($Configuration -eq 'Debug') { 'debug/lib' } else { 'lib' }
$BinSubdir = if ($Configuration -eq 'Debug') { 'debug/bin' } else { 'bin' }
Push-Location $RepoRoot
try {
    $Options = @('/nologo','/std:c++20','/EHsc','/utf-8','/Z7', $Runtime,
        '/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/DUNICODE','/D_UNICODE','/DDIRECTX_TOOLKIT_IMPORT','/D_WIN32_WINNT=0x0A00',
        '/ISource/Runtime','/ISource/ThirdParty/json', "/I$Audio/include/directxtk", "/I$Audio/include",
        "/I$Installed/include", "/I$Installed/include/luajit", "/Fo$Output/", "/Fe$Output/AudioTests.exe")
    $EngineBin = Join-Path $RepoRoot "Build/Bin/$Configuration-windows-x86_64"
    & cl.exe @Options Tests/AudioTests.cpp /link "/LIBPATH:$Audio/$LibSubdir" "/LIBPATH:$EngineBin" HitoriEngine.lib DirectXTK.lib ole32.lib /SUBSYSTEM:CONSOLE
    if ($LASTEXITCODE -ne 0) { throw 'Audio test compilation failed.' }
    Copy-Item -LiteralPath (Join-Path $Audio "$BinSubdir/DirectXTK.dll") -Destination $Output -Force
    & (Join-Path $Output 'AudioTests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Audio tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
