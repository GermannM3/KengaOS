param(
    [Parameter(Mandatory = $true)]
    [string]$Package,
    [string]$InstallRoot = "$env:LOCALAPPDATA\KengaOS"
)

$ErrorActionPreference = 'Stop'
$packageRoot = (Resolve-Path -LiteralPath $Package).Path
$manifestPath = Join-Path $packageRoot 'release-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { throw "release-manifest.json not found in package" }

$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$stage = Join-Path ([IO.Path]::GetTempPath()) ("kengaos-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    foreach ($property in $manifest.artifacts.psobject.Properties) {
        $relative = [string]$property.Name
        $source = Join-Path $packageRoot $relative
        if (-not (Test-Path -LiteralPath $source)) { throw "Missing artifact: $relative" }
        $expected = [string]$property.Value.sha256
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant()
        if ($actual -ne $expected.ToLowerInvariant()) { throw "Hash mismatch: $relative" }
        $target = Join-Path $stage $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
    New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
    $slot = Join-Path $InstallRoot ("slot-" + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
    Move-Item -LiteralPath $stage -Destination $slot
    $active = @{ active = Split-Path -Leaf $slot; installedUtc = [DateTime]::UtcNow.ToString('o'); manifest = 'release-manifest.json' }
    $active | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $InstallRoot 'active.json')
    Write-Host "KengaOS installed to $slot"
    Write-Host "This portable installer validates the release and stages it safely; it does not repartition disks or replace the host bootloader."
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
}
