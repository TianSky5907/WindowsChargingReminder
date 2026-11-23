param(
    [ValidateSet('x64', 'arm64')]
    [string]$Arch = 'x64',
    [switch]$Clean,
    [string]$Cxx = '',
    [string]$Windres = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root "build\$Arch"
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($version)) {
    throw 'VERSION file is empty'
}

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item $buildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$sources = Get-ChildItem (Join-Path $root 'src') -Recurse -Filter *.cpp |
           ForEach-Object { $_.FullName }
if ($sources.Count -eq 0) {
    throw 'No source files found under src\'
}

if ($Arch -eq 'x64') {
    if ([string]::IsNullOrWhiteSpace($Cxx)) {
        $Cxx = 'D:\Applications\msys2\ucrt64\bin\g++.exe'
    }
    if ([string]::IsNullOrWhiteSpace($Windres)) {
        $Windres = 'D:\Applications\msys2\ucrt64\bin\windres.exe'
    }
} else {
    if ([string]::IsNullOrWhiteSpace($Cxx)) {
        $Cxx = 'D:\Applications\msys2\clangarm64\bin\clang++.exe'
    }
    if ([string]::IsNullOrWhiteSpace($Windres)) {
        $Windres = 'D:\Applications\msys2\clangarm64\bin\windres.exe'
    }
}

if (-not (Test-Path $Cxx)) {
    throw "Compiler not found: $Cxx"
}
if (-not (Test-Path $Windres)) {
    throw "windres not found: $Windres"
}

$outFile = Join-Path $buildDir 'WindowsChargingReminder.exe'
$resourceObject = Join-Path $buildDir 'app.res.o'
& $windres -O coff -o $resourceObject (Join-Path $root 'src\app.rc')
if ($LASTEXITCODE -ne 0) {
    throw "Resource compile failed with exit code $LASTEXITCODE"
}

$compilerArgs = @(
    '-std=c++17',
    '-O2',
    '-Wall',
    '-Wextra',
    '-municode',
    '-mwindows',
    '-static',
    '-s',
    '-DUNICODE',
    '-D_UNICODE',
    '-DWIN32_LEAN_AND_MEAN',
    '-DNOMINMAX',
    '-D_WIN32_WINNT=0x0A00'
) + $sources + @(
    $resourceObject,
    '-o', $outFile,
    '-lole32',
    '-luuid',
    '-luser32',
    '-lshell32',
    '-lshlwapi',
    '-lgdi32',
    '-ladvapi32',
    '-lpowrprof',
    '-ldwmapi',
    '-ld2d1',
    '-ldwrite',
    '-lcomdlg32'
)

& $cxx @compilerArgs
if ($LASTEXITCODE -ne 0) {
    throw "Compile failed with exit code $LASTEXITCODE"
}

$distDir = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$finalFile = Join-Path $distDir "WindowsChargingReminder-$version-$Arch.exe"
Copy-Item $outFile $finalFile -Force

Write-Host "Built $finalFile"
