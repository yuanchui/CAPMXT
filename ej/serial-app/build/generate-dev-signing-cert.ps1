# UTF-8 BOM: keeps default -Subject parsing reliable on Windows PowerShell 5.1
# Self-signed code-signing PFX for electron-builder CSC_LINK (dev/test only).
param(
    [string]$OutPfx,
    [string]$Subject = 'CN=yuanchu, E=tom958@foxmail.com',
    [string]$Password = ''
)

$ErrorActionPreference = 'Stop'

if (-not $OutPfx) {
    $OutPfx = Join-Path $PSScriptRoot 'dev-signing.pfx'
}

if (-not $Password) {
    $Password = 'DevSignChangeMe!'
    Write-Host 'Using default -Password (not for production): DevSignChangeMe!' -ForegroundColor Yellow
}

$secure = ConvertTo-SecureString -String $Password -AsPlainText -Force

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -CertStoreLocation Cert:\CurrentUser\My `
    -KeyExportPolicy Exportable `
    -KeySpec Signature `
    -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -NotAfter (Get-Date).AddYears(5)

try {
    Export-PfxCertificate -Cert $cert -FilePath $OutPfx -Password $secure | Out-Null
}
finally {
    Remove-Item -Path "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey
}

Write-Host ''
Write-Host "Wrote: $OutPfx"
Write-Host "Thumbprint: $($cert.Thumbprint)"
Write-Host ''
Write-Host 'Before package: use PowerShell lines in PowerShell, or CMD lines in cmd.exe (not mixed).' -ForegroundColor Cyan
Write-Host ''
Write-Host '  PowerShell:' -ForegroundColor Yellow
Write-Host "    `$env:CSC_LINK = '$OutPfx'"
Write-Host "    `$env:CSC_KEY_PASSWORD = '$Password'"
Write-Host '    pnpm run package:user:nsis:small'
Write-Host ''
Write-Host '  CMD:' -ForegroundColor Yellow
$cmdLink = 'set "CSC_LINK=' + $OutPfx + '"'
$cmdPass = 'set "CSC_KEY_PASSWORD=' + $Password + '"'
Write-Host "    $cmdLink"
Write-Host "    $cmdPass"
Write-Host '    pnpm run package:user:nsis:small'
Write-Host ''
