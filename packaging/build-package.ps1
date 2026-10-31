[CmdletBinding()]
param(
    [ValidateSet("baby", "fab4", "all")]
    [string]$Target = "baby",
    [string]$BaseUrl = $env:SB_RELEASE_BASE_URL,
    [string]$OutputDirectory,
    [string]$ConfigPath
)
$ErrorActionPreference = 'Stop'
# Target packaging uses the same Unix ZIP permission and ELF validation path.
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$WslRoot = (& wsl.exe wslpath -a ($ProjectRoot.Replace('\', '/'))).Trim()
if ($BaseUrl) { $env:WSLENV = (($env:WSLENV + ':SB_RELEASE_BASE_URL').Trim(':')); $env:SB_RELEASE_BASE_URL = $BaseUrl }
if ($ConfigPath) {
    $env:SB_PACKAGE_CONFIG = (& wsl.exe wslpath -a ([IO.Path]::GetFullPath($ConfigPath).Replace('\', '/'))).Trim()
    $env:WSLENV = (($env:WSLENV + ':SB_PACKAGE_CONFIG').Trim(':'))
}
if ($OutputDirectory) {
    $env:SB_PACKAGE_OUTPUT = (& wsl.exe wslpath -a ([IO.Path]::GetFullPath($OutputDirectory).Replace('\', '/'))).Trim()
    $env:WSLENV = (($env:WSLENV + ':SB_PACKAGE_OUTPUT').Trim(':'))
}
& wsl.exe sh "$WslRoot/packaging/build-package.sh" $Target
if ($LASTEXITCODE -ne 0) { throw 'Target packaging failed' }
return
