param(
    [ValidateSet('Debug', 'Release', 'ObjViewer', 'Benchmark', 'Benchmark_Debug')]
    [string]$Configuration = 'Release',
    [switch]$Build,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

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
if (-not ((Test-Path (Join-Path $Installed 'include\luajit\lua.h')) -and
           (Test-Path (Join-Path $Installed 'lib\lua51.lib')) -and
           (Test-Path (Join-Path $Installed 'bin\lua51.dll')))) {
    & $Vcpkg install luajit:x64-windows --classic
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install LuaJIT.' }
}
if (-not (Test-Path (Join-Path $Installed 'include\sol\sol.hpp'))) {
    & $Vcpkg install sol2:x64-windows --classic
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install sol2.' }
}

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere)) { throw 'Visual Studio Installer (vswhere.exe) was not found.' }
$VsPath = (& $VsWhere -latest -prerelease -all -products '*' -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
$VsVersion = (& $VsWhere -latest -prerelease -all -products '*' -requires Microsoft.Component.MSBuild -property installationVersion | Select-Object -First 1)
if (-not $VsPath -or -not $VsVersion) {
    $Candidates = @(
        @{ Path = "C:\Program Files\Microsoft Visual Studio\18\Insiders"; Version = "18.0" },
        @{ Path = "C:\Program Files\Microsoft Visual Studio\2022\Community"; Version = "17.0" },
        @{ Path = "C:\Program Files\Microsoft Visual Studio\2022\Professional"; Version = "17.0" },
        @{ Path = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"; Version = "17.0" }
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
    & (Join-Path $PSScriptRoot 'premake5.exe') $Action
    if ($LASTEXITCODE -ne 0) { throw 'Solution generation failed.' }
    if ($Build -and -not $SkipBuild) {
        $MsBuild = Join-Path $VsPath 'MSBuild\Current\Bin\MSBuild.exe'
        if (-not (Test-Path $MsBuild)) { throw "MSBuild not found: $MsBuild" }
        $SavedClOptions = $env:_CL_
        try {
            if ($Action -eq 'vs2026') { $env:_CL_ = "$SavedClOptions /Z7 /Y-" }
            foreach ($Project in @('Source\Editor\HitoriEditor.vcxproj', 'Source\Game\HitoriGame.vcxproj')) {
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
