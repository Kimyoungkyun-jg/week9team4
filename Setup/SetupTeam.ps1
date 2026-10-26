param(
    [ValidateSet('Debug', 'Release', 'ObjViewer', 'Benchmark', 'Benchmark_Debug')]
    [string]$Configuration = 'Release',
    [switch]$Build,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'AudioDependency.ps1')
$Stage = 'Initialize'
$LogDir = Join-Path $RepoRoot 'Build\Logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Start-Transcript -Path (Join-Path $LogDir 'SetupTeam.log') -Force | Out-Null
try {
$Stage = 'Initialize ImGui submodule'
if (-not (Test-Path (Join-Path $RepoRoot 'Source\ThirdParty\ImGui\imgui.cpp'))) {
    & git -C $RepoRoot submodule update --init -- Source/ThirdParty/ImGui
    if ($LASTEXITCODE -ne 0) { throw 'ImGui submodule initialization failed. Check Git/network access.' }
}
$Stage = 'Locate/bootstrap vcpkg'

if ($env:VCPKG_ROOT) {
    $VcpkgRoot = $env:VCPKG_ROOT
} else {
    $VcpkgRoot = Join-Path $env:USERPROFILE 'vcpkg'
}
$Vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
if (-not (Test-Path $Vcpkg)) {
    if (-not (Test-Path $VcpkgRoot)) {
        & git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
        if ($LASTEXITCODE -ne 0) { throw 'Failed to clone vcpkg.' }
    }
    $Bootstrap = Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat'
    if (-not (Test-Path $Bootstrap)) { throw "vcpkg bootstrap script not found: $Bootstrap" }
    & $Bootstrap -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'Failed to bootstrap vcpkg.' }
}
$env:VCPKG_ROOT = (Resolve-Path $VcpkgRoot).Path

$Installed = Join-Path $env:VCPKG_ROOT 'installed\x64-windows'
$Stage = 'Install/verify LuaJIT'
if (-not ((Test-Path (Join-Path $Installed 'include\luajit\lua.h')) -and
           (Test-Path (Join-Path $Installed 'lib\lua51.lib')) -and
           (Test-Path (Join-Path $Installed 'bin\lua51.dll')))) {
    & $Vcpkg install luajit:x64-windows --classic
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install LuaJIT.' }
}
$Stage = 'Install/verify sol2'
if (-not (Test-Path (Join-Path $Installed 'include\sol\sol.hpp'))) {
    & $Vcpkg install sol2:x64-windows --classic
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install sol2.' }
}

$Stage = 'Install/verify DirectXTK for Audio'
$AudioRoot = Join-Path $env:VCPKG_ROOT 'installed'
if (-not (Test-HitoriAudioDependency $AudioRoot)) {
    # Never remove/rebuild packages in a teammate's shared classic install tree.
    $AudioRoot = Join-Path $env:VCPKG_ROOT 'installed-hitori-audio'
    if (-not (Test-HitoriAudioDependency $AudioRoot)) {
        $AudioArgs = @('install', 'directxtk[xaudio2-9]:x64-windows', '--classic', "--x-install-root=$AudioRoot")
        Write-Host "Installing audio in a separate tree: $AudioRoot"
        Write-Host "Command: `"$Vcpkg`" $AudioArgs"
        & $Vcpkg @AudioArgs
        if ($LASTEXITCODE -ne 0) { throw 'DirectXTK Audio installation failed. See vcpkg output/buildtrees logs above. No projects were generated.' }
        if (-not (Test-HitoriAudioDependency $AudioRoot)) { throw 'DirectXTK installation returned success but feature records or Debug/Release headers/libraries/DLLs are incomplete.' }
    }
}
Write-Host "Audio dependency verified: $AudioRoot"
& $Vcpkg list --classic "--x-install-root=$AudioRoot" directxtk
if ($LASTEXITCODE -ne 0) { throw 'Could not query the installed DirectXTK version.' }

$Stage = 'Find Visual Studio C++ tools'
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere)) { throw 'Visual Studio Installer (vswhere.exe) was not found.' }
$VsPath = (& $VsWhere -latest -prerelease -all -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$VsVersion = (& $VsWhere -latest -prerelease -all -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion | Select-Object -First 1)
if (-not $VsPath -or -not $VsVersion) {
    $Candidates = @(
        @{ Path = (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Insiders'); Version = "18.0" },
        @{ Path = (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community'); Version = "17.0" },
        @{ Path = (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Professional'); Version = "17.0" },
        @{ Path = (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Enterprise'); Version = "17.0" }
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path (Join-Path $Candidate.Path 'MSBuild\Current\Bin\MSBuild.exe')) {
            $VsPath = $Candidate.Path
            $VsVersion = $Candidate.Version
            break
        }
    }
}
if (-not $VsPath -or -not $VsVersion) { throw 'Visual Studio with MSBuild was not found.' }
$Action = if ([int]($VsVersion.Split('.')[0]) -ge 18) { 'vs2026' } else { 'vs2022' }

Push-Location $RepoRoot
try {
    $Stage = 'Generate solution'
    & (Join-Path $PSScriptRoot 'premake5.exe') $Action
    if ($LASTEXITCODE -ne 0) { throw 'Solution generation failed.' }
    if ($Build -and -not $SkipBuild) {
        $MsBuild = Join-Path $VsPath 'MSBuild\Current\Bin\MSBuild.exe'
        if (-not (Test-Path $MsBuild)) { throw "MSBuild not found: $MsBuild" }
        $SavedClOptions = $env:_CL_
        try {
            if ($Action -eq 'vs2026') { $env:_CL_ = "$SavedClOptions /Z7 /Y-" }
            foreach ($Project in @('Source\Editor\HitoriEditor.vcxproj', 'Source\Game\HitoriGame.vcxproj')) {
                $Stage = "Build $Configuration $Project"
                & $MsBuild $Project /t:Rebuild "/p:Configuration=$Configuration" /p:Platform=x64 /m:2 /nologo
                if ($LASTEXITCODE -ne 0) { throw "Build failed: $Project" }
            }
        } finally {
            $env:_CL_ = $SavedClOptions
        }
    }
    Write-Host 'Team setup completed.'
} finally {
    Pop-Location
}
} catch {
    Write-Host "FAILED stage: $Stage" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "Log: $LogDir\SetupTeam.log"
    Write-Host "vcpkg logs: $VcpkgRoot\buildtrees\directxtk"
    $BuildOption = if ($Build) { ' -Build' } else { '' }
    Write-Host "Retry from repository root: SlnGenerator.bat$BuildOption -Configuration $Configuration"
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
