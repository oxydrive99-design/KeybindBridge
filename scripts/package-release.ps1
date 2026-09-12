param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,

    [string]$Version = "1.0.0",

    [string]$OutputDirectory = "release"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$resolvedDll = (Resolve-Path $DllPath).Path
$outputRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $projectRoot $OutputDirectory))
$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "KeybindBridge-release-" + [System.Guid]::NewGuid().ToString("N"))

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null

try {
    $dllStage = Join-Path $stagingRoot "windows"
    New-Item -ItemType Directory -Force -Path $dllStage | Out-Null
    Copy-Item $resolvedDll (Join-Path $dllStage "keybind_bridge.dll")
    Copy-Item (Join-Path $projectRoot "native/KeybindBridge.diagnostics.ini") $dllStage
    Copy-Item (Join-Path $projectRoot "INSTALL.txt") $dllStage
    Copy-Item (Join-Path $projectRoot "INSTALL_RU.txt") $dllStage

    $modsStage = Join-Path $stagingRoot "mods"
    New-Item -ItemType Directory -Force -Path $modsStage | Out-Null
    Copy-Item (Join-Path $projectRoot "lua-mod") (
        Join-Path $modsStage "KeybindBridge-Core") -Recurse
    Remove-Item (Join-Path $modsStage "KeybindBridge-Core/tests") `
        -Recurse -Force
    Copy-Item (Join-Path $projectRoot "examples/autonomous-flashlight-mod") (
        Join-Path $modsStage "KeybindBridge-Flashlight") -Recurse
    Copy-Item (Join-Path $projectRoot "INSTALL.txt") $modsStage
    Copy-Item (Join-Path $projectRoot "INSTALL_RU.txt") $modsStage

    $sdkStage = Join-Path $stagingRoot "sdk"
    New-Item -ItemType Directory -Force -Path $sdkStage | Out-Null
    Copy-Item (Join-Path $projectRoot "examples/sdk-template-mod") (
        Join-Path $sdkStage "sdk-template-mod") -Recurse
    Copy-Item (Join-Path $projectRoot "examples/snippets") (
        Join-Path $sdkStage "snippets") -Recurse
    Copy-Item (Join-Path $projectRoot "lua-mod/Scripts/KeybindAction.lua") $sdkStage
    Copy-Item (Join-Path $projectRoot "docs/API.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "docs/MODDERS_GUIDE.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "docs/MODDERS_GUIDE_RU.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "docs/MOD_STRUCTURE.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "docs/MOD_STRUCTURE_RU.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "README.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "README_RU.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "CHANGELOG.md") $sdkStage
    Copy-Item (Join-Path $projectRoot "LICENSE") $sdkStage

    $archives = @(
        @{
            Source = $dllStage
            Destination = Join-Path $outputRoot (
                "KeybindBridge-$Version-Windows-x64.zip")
        },
        @{
            Source = $modsStage
            Destination = Join-Path $outputRoot (
                "KeybindBridge-$Version-Mods.zip")
        },
        @{
            Source = $sdkStage
            Destination = Join-Path $outputRoot (
                "KeybindBridge-$Version-SDK.zip")
        }
    )

    foreach ($archive in $archives) {
        if (Test-Path $archive.Destination) {
            Remove-Item $archive.Destination -Force
        }
        Compress-Archive -Path (Join-Path $archive.Source "*") `
            -DestinationPath $archive.Destination -CompressionLevel Optimal
        Write-Host $archive.Destination
    }

    $checksumPath = Join-Path $outputRoot "SHA256SUMS.txt"
    $checksumLines = foreach ($archive in $archives) {
        $hash = Get-FileHash $archive.Destination -Algorithm SHA256
        "{0} *{1}" -f $hash.Hash.ToLowerInvariant(), (
            Split-Path $archive.Destination -Leaf)
    }
    $checksumLines | Set-Content $checksumPath -Encoding ascii
    Write-Host $checksumPath
}
finally {
    if (Test-Path $stagingRoot) {
        Remove-Item $stagingRoot -Recurse -Force
    }
}
