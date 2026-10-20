# Fetch the same pinned core as fetch-reference.sh, without requiring Git Bash.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Git([string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git failed with exit code $LASTEXITCODE" }
}
$projectDir = Split-Path -Parent $PSScriptRoot
$referenceDir = Join-Path $projectDir 'references/MuseScore'
$revision = '4509c9072c583f7ab0c805ded2ab88f29e0ec0e5'
$lockFile = Join-Path $referenceDir 'JIANPU_SOURCE_LOCK'
$expectedLock = @($revision, '42b02245d36f1f37dd2b04a382e27c28c9ece006', 'a9db17d2f0478859711873f2db156eedfc082040')
if ((Test-Path $lockFile) -and -not (Test-Path "$referenceDir/.git")) {
    $actualLock = @(Get-Content $lockFile)
    if (($actualLock -join ' ') -ne ($expectedLock -join ' ')) {
        throw 'Source archive revision manifest differs.'
    }
    foreach ($file in @('muse/framework/global/globalmodule.cpp', 'muse_deps/buildtools/manifest.cmake')) {
        if (-not (Test-Path (Join-Path $referenceDir $file))) {
            throw "Incomplete corresponding-source archive: missing $file"
        }
    }
    Write-Host 'Using the complete pinned core from the corresponding-source archive.'
    return
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Install Git for Windows first.' }
New-Item -ItemType Directory -Force -Path "$projectDir/references", "$projectDir/artifacts" | Out-Null
if (-not (Test-Path $referenceDir)) {
    Invoke-Git @('clone', '--depth', '1', '--no-checkout', 'https://github.com/musescore/MuseScore.git', $referenceDir)
    Invoke-Git @('-C', $referenceDir, 'fetch', '--depth', '1', 'origin', $revision)
    Invoke-Git @('-C', $referenceDir, 'checkout', '--detach', $revision)
}
$actual = Invoke-Git @('-C', $referenceDir, 'rev-parse', 'HEAD')
if ($actual -ne $revision) {
    throw 'Reference revision differs. Existing checkout preserved; use a separate directory for another revision.'
}
Invoke-Git @('-C', $referenceDir, 'submodule', 'update', '--init', '--recursive', '--depth', '1')
$submodules = @(Invoke-Git @('-C', $referenceDir, 'submodule', 'status', '--recursive'))
@($actual) + $submodules | Set-Content "$projectDir/artifacts/musescore-revisions.txt" -Encoding UTF8
Write-Host "Pinned MuseScore core: $actual"
