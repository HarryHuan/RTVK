# RTVK 环境检测脚本 v2
# Usage: .\scripts\setup_env.ps1

$ErrorActionPreference = "Continue"
$all_ok = $true

function Test-Tool($Name, $Command, $MinVersion, $VersionPattern) {
    Write-Host -NoNewline "[ ] $Name ... "
    try {
        $out = Invoke-Expression $Command 2>&1 | Select-Object -First 5 | Out-String
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $null) {
            Write-Host "NOT FOUND" -ForegroundColor Red
            $script:all_ok = $false
            return
        }
        if ($VersionPattern) {
            if ($out -match $VersionPattern) {
                Write-Host $matches[0] -ForegroundColor Green
            } else {
                Write-Host ("FOUND (unknown ver)") -ForegroundColor Yellow
            }
        } else {
            Write-Host "OK" -ForegroundColor Green
        }
    } catch {
        Write-Host "NOT FOUND" -ForegroundColor Red
        $script:all_ok = $false
    }
}

Write-Host "`n========== RTVK Environment Check v2 ==========`n" -ForegroundColor Cyan

# CMake
Test-Tool "CMake" 'cmake --version' "3.25" '[\d\.]+'

# Vulkan SDK
Write-Host -NoNewline "[ ] Vulkan SDK ... "
if ($env:VULKAN_SDK) {
    Write-Host $env:VULKAN_SDK -ForegroundColor Green
} else {
    Write-Host "NOT SET" -ForegroundColor Red
    $all_ok = $false
}

# glslc
Test-Tool "glslc" 'glslc --version' $null 'shaderc [\w\.]+'

# VS2022 — check common paths including D: drive
Write-Host -NoNewline "[ ] VS2022 ... "
$vsPaths = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"
)
$vsFound = $false
foreach ($p in $vsPaths) {
    if (Test-Path $p) { $vsFound = $true; break }
}
if ($vsFound) {
    Write-Host "FOUND" -ForegroundColor Green
    # Show msbuild version if available
    $msbuildDirs = @(
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($mb in $msbuildDirs) {
        if (Test-Path $mb) {
            $ver = & $mb -version -nologo 2>&1
            Write-Host "       MSBuild $ver" -ForegroundColor Gray
            break
        }
    }
} else {
    Write-Host "NOT FOUND — install VS2022" -ForegroundColor Red
    $all_ok = $false
}

# Qt6
Write-Host -NoNewline "[ ] Qt6 ... "
$qt6Dirs = @("C:\Qt\6.*", "D:\Qt\6.*")
$qt6Path = $null
foreach ($pattern in $qt6Dirs) {
    $found = Get-Item $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $qt6Path = $found.FullName; break }
}
if ($qt6Path) {
    Write-Host $qt6Path -ForegroundColor Green
    $qmake = Get-ChildItem "$qt6Path\*\bin\qmake*.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($qmake) {
        Write-Host "       $($qmake.FullName)" -ForegroundColor Gray
    }
} else {
    Write-Host "NOT FOUND — install Qt 6.5+ https://www.qt.io/download" -ForegroundColor Red
    $all_ok = $false
}

# GPU
Write-Host -NoNewline "[ ] GPU ... "
$gpu = vulkaninfo --summary 2>$null | Select-String "deviceName" | ForEach-Object { ($_.Line -replace '\s*deviceName\s*=\s*', '') }
if ($gpu) { Write-Host ($gpu -join "; ") -ForegroundColor Green }
else { Write-Host "UNKNOWN" -ForegroundColor Yellow }

Write-Host "`n================================================`n" -ForegroundColor Cyan
if ($all_ok) {
    Write-Host "ALL CHECKS PASSED" -ForegroundColor Green
    Write-Host "Next: cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64" -ForegroundColor White
} else {
    Write-Host "SOME CHECKS FAILED" -ForegroundColor Red
}
