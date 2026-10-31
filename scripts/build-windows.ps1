# Run from an x64 Native Tools / Developer PowerShell for Visual Studio 2022.
param(
    [string]$QtRoot = $env:QT_ROOT,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$SkipPackage
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
if (-not $QtRoot -or -not (Test-Path "$QtRoot/bin/windeployqt.exe")) {
    throw 'Set QT_ROOT to the Qt 6.10.2 msvc2022_64 directory.'
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run this script in an x64 Visual Studio 2022 developer environment (MSVC 19.40+).'
}
foreach ($program in @('git', 'cmake', 'ctest', 'ninja')) {
    if (-not (Get-Command $program -ErrorAction SilentlyContinue)) { throw "Required build tool not found: $program" }
}
if (-not $SkipPackage -and -not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Install Python 3.12 to create the corresponding-source package, or use -SkipPackage.'
}
$projectDir = Split-Path -Parent $PSScriptRoot
$previousQpa = [Environment]::GetEnvironmentVariable('QT_QPA_PLATFORM')
Push-Location $projectDir
try {
    & "$PSScriptRoot/fetch-reference.ps1"
    $env:QT_ROOT = (Resolve-Path $QtRoot).Path
    $env:PATH = "$env:QT_ROOT/bin;$env:PATH"
    $preset = 'windows-' + $Configuration.ToLowerInvariant()
    $buildDir = Join-Path $projectDir "build/$preset"
    $distDir = Join-Path $projectDir "artifacts/JianpuLab-$Configuration-win64"
    Invoke-Checked 'cmake' @('--preset', $preset)
    Invoke-Checked 'cmake' @('--build', '--preset', $preset, '--parallel', '4')
    # External dependency recipes may provide DLLs outside Qt's deployment scan.
    Get-ChildItem "$buildDir/_deps" -Recurse -Filter '*.dll' | ForEach-Object {
        Copy-Item $_.FullName $buildDir -Force
    }
    foreach ($program in @('jianpu_lab', 'jianpu_engine_probe', 'tst_document', 'tst_textinput', 'tst_projection', 'tst_editor', 'tst_scorewidget', 'tst_qtruntime')) {
        Invoke-Checked "$env:QT_ROOT/bin/windeployqt.exe" @("--$($Configuration.ToLowerInvariant())", '--compiler-runtime', '--no-translations', '--include-plugins', 'qoffscreen', "$buildDir/$program.exe")
    }
    Invoke-Checked 'ctest' @('--preset', $preset)
    Invoke-Checked 'cmake' @('--install', $buildDir, '--prefix', $distDir, '--config', $Configuration)
    Invoke-Checked "$env:QT_ROOT/bin/windeployqt.exe" @("--$($Configuration.ToLowerInvariant())", '--compiler-runtime', '--no-translations', '--include-plugins', 'qoffscreen', "$distDir/bin/jianpu_lab.exe")
    if ($Configuration -eq 'Release') {
        if (-not $env:VCToolsRedistDir) { throw 'VCToolsRedistDir is required for app-local MSVC runtime deployment.' }
        $crt = Get-ChildItem "$env:VCToolsRedistDir/x64" -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
        if (-not $crt) { throw 'MSVC x64 CRT redistribution directory not found.' }
        Get-ChildItem $crt.FullName -Filter '*.dll' | Copy-Item -Destination "$distDir/bin" -Force
    }
    $env:QT_QPA_PLATFORM = 'offscreen'
    $process = Start-Process "$distDir/bin/jianpu_lab.exe" -ArgumentList @('--smoke', ('"{0}"' -f "$distDir/examples/ensemble.musicxml")) -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Deployed application smoke test failed: $($process.ExitCode)" }
    Write-Host "Application: $distDir/bin/jianpu_lab.exe"
    if (-not $SkipPackage) {
        $archive = "$distDir.zip"
        Compress-Archive -Path $distDir -DestinationPath $archive -Force
        Invoke-Checked 'python' @('tools/package_source.py')
        Write-Host "Portable package: $archive"
        Write-Host 'Corresponding source: artifacts/JianpuLab-source.tar.gz'
    }
    Write-Host 'A clean Windows 10/11 machine must still be used for final interactive acceptance.'
} finally {
    if ($null -eq $previousQpa) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = $previousQpa }
    Pop-Location
}
