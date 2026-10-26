$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $RepoRoot 'Setup/AudioDependency.ps1')
$Fixture = Join-Path $RepoRoot ('Build/AudioDependencyTests/' + [Guid]::NewGuid())
New-Item -ItemType Directory -Force (Join-Path $Fixture 'vcpkg') | Out-Null
function Assert-Dependency([bool]$Expected, [string]$Name) {
    if ((Test-HitoriAudioDependency $Fixture) -ne $Expected) { throw "FAIL $Name" }
    Write-Host "PASS $Name"
}
Assert-Dependency $false 'fresh install tree is incomplete'
$Core = "Package: directxtk`nArchitecture: x64-windows`nStatus: install ok installed`n`n"
$Feature = "Package: directxtk`nFeature: xaudio2-9`nArchitecture: x64-windows`nStatus: install ok installed`n`n"
$Status = Join-Path $Fixture 'vcpkg/status'
Set-Content -LiteralPath $Status -Value $Core
foreach ($File in @('include/directxtk/Audio.h', 'include/DirectXMath.h', 'lib/DirectXTK.lib',
                    'debug/lib/DirectXTK.lib', 'bin/DirectXTK.dll', 'debug/bin/DirectXTK.dll',
                    'share/directxtk/DirectXTK-targets.cmake')) {
    $Target = Join-Path $Fixture "x64-windows/$File"
    New-Item -ItemType Directory -Force (Split-Path $Target) | Out-Null
    Set-Content -LiteralPath $Target -Value 'test fixture'
}
$Targets = Join-Path $Fixture 'x64-windows/share/directxtk/DirectXTK-targets.cmake'
Set-Content -LiteralPath $Targets -Value 'SHARED IMPORTED DIRECTX_TOOLKIT_IMPORT'
Assert-Dependency $false 'Audio.h and binaries without feature record are insufficient'
Set-Content -LiteralPath $Status -Value ($Core + $Feature)
Assert-Dependency $true 'all required feature records and artifacts'
Set-Content -LiteralPath $Status -Value ($Core + ($Feature -replace 'x64-windows', 'x86-windows'))
Assert-Dependency $false 'wrong architecture feature rejected'
Set-Content -LiteralPath $Status -Value ($Core + ($Feature -replace 'install ok installed', 'deinstall ok installed'))
Assert-Dependency $false 'removed feature rejected'
Set-Content -LiteralPath $Status -Value ($Core + $Feature)
Set-Content -LiteralPath $Targets -Value 'STATIC IMPORTED'
Assert-Dependency $false 'unexpected package linkage rejected'
Set-Content -LiteralPath $Targets -Value 'SHARED IMPORTED DIRECTX_TOOLKIT_IMPORT'
$DebugDLL = Join-Path $Fixture 'x64-windows/debug/bin/DirectXTK.dll'
Move-Item -LiteralPath $DebugDLL -Destination "$DebugDLL.fixture-hidden"
Assert-Dependency $false 'missing Debug runtime rejected'
Move-Item -LiteralPath "$DebugDLL.fixture-hidden" -Destination $DebugDLL
Assert-Dependency $true 'repeat verification remains successful'
Write-Host 'Fixture files remain under Build/AudioDependencyTests; no shared packages were changed.'
