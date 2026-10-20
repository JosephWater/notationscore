# Run in an x64 Developer PowerShell for Visual Studio 2022.
param(
    [string]$QtRoot = $env:QT_ROOT,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Git([string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git failed with exit code $LASTEXITCODE" }
}
$projectDir = Split-Path -Parent $PSScriptRoot
Push-Location $projectDir
try {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Install Git for Windows first.' }
    $changes = @(Invoke-Git @('status', '--porcelain'))
    if ($changes.Count -gt 0) {
        throw 'Working tree has local changes. Commit or stash them before updating. Run git status for details.'
    }
    $branch = Invoke-Git @('symbolic-ref', '--quiet', '--short', 'HEAD')
    $upstream = Invoke-Git @('rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{upstream}')
    Write-Host "Updating $branch from $upstream"
    Invoke-Git @('pull', '--ff-only')
    Invoke-Git @('log', '-1', '--format=Testing commit: %h %s')
    & "$PSScriptRoot/build-windows.ps1" -QtRoot $QtRoot -Configuration $Configuration -SkipPackage:(-not $Package)
} finally {
    Pop-Location
}
