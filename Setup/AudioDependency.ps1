param([string]$InstallRoot, [switch]$Check)

# Inspect vcpkg's installed feature records AND the shared-library artifacts.
function Test-HitoriAudioDependency([string]$Root) {
    $StatusFile = Join-Path $Root 'vcpkg\status'
    if (-not (Test-Path -LiteralPath $StatusFile)) { return $false }
    $Records = (Get-Content -LiteralPath $StatusFile -Raw) -split '\r?\n\s*\r?\n'
    foreach ($Feature in @('core', 'xaudio2-9')) {
        $Found = $false
        foreach ($Record in $Records) {
            if ($Record -notmatch '(?m)^Package: directxtk\r?$' -or
                $Record -notmatch '(?m)^Architecture: x64-windows\r?$' -or
                $Record -notmatch '(?m)^Status: install ok installed\r?$') { continue }
            if (($Feature -eq 'core' -and $Record -notmatch '(?m)^Feature:') -or
                $Record -match "(?m)^Feature: $Feature\r?`$") { $Found = $true }
        }
        if (-not $Found) { return $false }
    }
    foreach ($File in @('include\directxtk\Audio.h', 'include\DirectXMath.h',
                        'lib\DirectXTK.lib', 'debug\lib\DirectXTK.lib',
                        'bin\DirectXTK.dll', 'debug\bin\DirectXTK.dll',
                        'share\directxtk\DirectXTK-targets.cmake')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root "x64-windows\$File"))) { return $false }
    }
    $Targets = Get-Content -LiteralPath (Join-Path $Root 'x64-windows\share\directxtk\DirectXTK-targets.cmake') -Raw
    return $Targets.Contains('SHARED IMPORTED') -and $Targets.Contains('DIRECTX_TOOLKIT_IMPORT')
}

if ($Check) {
    if (-not (Test-HitoriAudioDependency $InstallRoot)) {
        Write-Error "DirectXTK [xaudio2-9] shared x64-windows dependency is incomplete: $InstallRoot. Run SlnGenerator.bat again."
        exit 1
    }
    exit 0
}
