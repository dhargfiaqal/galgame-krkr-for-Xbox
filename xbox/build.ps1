param(
    [ValidateSet('ninja', 'vs', 'cmake')]
    [string] $Backend = 'ninja',
    [string] $GameArchiveUrl = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $root 'engine\krkrsdl2'
$build = Join-Path $root 'out\meson'
$stage = Join-Path $root 'out\stage'
$msix = Join-Path $root 'out\KRKR-Xbox.msix'

function Require-Command([string] $name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Missing '$name'. Install Visual Studio 2022 (Desktop C++), Meson, Ninja and the Windows 10 SDK."
    }
}

Require-Command 'git'
if ($Backend -eq 'cmake') {
    Require-Command 'cmake'
    Require-Command 'msbuild'
    if (-not $env:VCPKG_ROOT) { throw 'Set VCPKG_ROOT to a vcpkg checkout containing the x64-uwp triplet.' }
} else {
    Require-Command 'meson'
    if ($Backend -eq 'ninja') { Require-Command 'ninja' }
    if ($Backend -eq 'vs') { Require-Command 'msbuild' }
}
Require-Command 'makeappx'

if (-not (Test-Path (Join-Path $engine 'meson.build'))) {
    if (Test-Path $engine) { Remove-Item $engine -Recurse -Force }
    git clone --depth 1 https://github.com/krkrsdl2/krkrsdl2.git $engine
    git -C $engine submodule update --init --recursive
}

$entryPath = Join-Path $engine 'src\core\sdl2\SDLEntrypoint.cpp'
$pickerPath = Join-Path $engine 'src\core\sdl2\krkr-xbox-folder-picker.cpp'
$pickerTemplate = Join-Path $PSScriptRoot 'krkr-xbox-folder-picker.cpp'
Copy-Item $pickerTemplate $pickerPath -Force
$entryText = [IO.File]::ReadAllText($entryPath) -replace "`r`n", "`n"
$newline = "`n"
$declaration = 'extern "C" const char *krkr_xbox_pick_game_folder();'
$signature = '#if defined(USE_SDL_MAIN)' + $newline + 'extern "C" int SDL_main(int argc, char **argv)'
$signatureReplacement = '#if defined(__WINRT__) || defined(USE_SDL_MAIN)' + $newline + 'extern "C" int SDL_main(int argc, char **argv)'
if ($entryText.Contains('#if defined(__WINRT__) || defined(USE_SDL_MAIN)')) {
    $entryAlreadyAdapted = $true
} elseif ($entryText.Contains($signature)) {
    $entryAlreadyAdapted = $false
} else {
    throw 'KRKR SDL2 entrypoint does not match the supported upstream revision.'
}
if (-not $entryAlreadyAdapted) {
    $entryText = $entryText.Replace($signature, $signatureReplacement)
}
$includeLine = '#include "SysInitImpl.h"'
if (-not $entryText.Contains($declaration)) {
    $entryText = $entryText.Replace($includeLine, $includeLine + $newline + $declaration)
}
$mainOpen = "{$newline`ttry$newline`t{$newline"
$mainOpenReplacement = "{$newline#ifdef __WINRT__$newline`tconst char *selected_folder = krkr_xbox_pick_game_folder();$newline`tif (selected_folder == nullptr) return 0;$newline`tchar *folder_argv[] = { argv[0], const_cast<char *>(selected_folder) };$newline`targc = 2;$newline`targv = folder_argv;$newline#endif$newline`ttry$newline`t{$newline"
if (-not $entryText.Contains('#ifdef __WINRT__' + $newline + "`tconst char *selected_folder")) {
    if (-not $entryText.Contains($mainOpen)) { throw 'KRKR SDL2 main body does not match the supported upstream revision.' }
    $entryText = $entryText.Replace($mainOpen, $mainOpenReplacement)
}
if (-not $entryText.EndsWith("`n")) { $entryText += "`n" }
[IO.File]::WriteAllText($entryPath, $entryText, [Text.UTF8Encoding]::new($false))

if (Test-Path $build) { Remove-Item $build -Recurse -Force }
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null

if ($Backend -eq 'cmake') {
    $cmakeLists = Join-Path $engine 'CMakeLists.txt'
    $cmakeText = [IO.File]::ReadAllText($cmakeLists)
    $sdlWinrtSource = 'external/SDL/src/main/winrt/SDL_winrt_main_NonXAML.cpp'
    $pickerSource = 'src/core/sdl2/krkr-xbox-folder-picker.cpp'
    if (-not $cmakeText.Contains('src/core/sdl2/SDLEntrypoint.cpp') -or -not (Test-Path (Join-Path $engine $sdlWinrtSource))) {
        throw 'KRKR SDL2 CMake source layout does not contain the expected SDL WinRT entrypoint.'
    }
    if (-not $cmakeText.Contains($sdlWinrtSource) -or -not $cmakeText.Contains($pickerSource)) {
        $cmakeText = $cmakeText.Replace('src/core/sdl2/SDLEntrypoint.cpp', "src/core/sdl2/SDLEntrypoint.cpp`n    $sdlWinrtSource`n    $pickerSource")
        $cmakeText += $newline + 'set_source_files_properties(' + $sdlWinrtSource + ' ' + $pickerSource + ' PROPERTIES COMPILE_OPTIONS "/ZW")' + $newline
        [IO.File]::WriteAllText($cmakeLists, $cmakeText, [Text.UTF8Encoding]::new($false))
    }
    $toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found: $toolchain" }
    cmake -S $engine -B $build -G 'Visual Studio 17 2022' -A x64 `
        -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0.18362.0 `
        -DCMAKE_TOOLCHAIN_FILE=$toolchain -DVCPKG_TARGET_TRIPLET=x64-uwp `
        '-DCMAKE_CXX_FLAGS=/DWINAPI_FAMILY=WINAPI_FAMILY_APP /D__WINRT__'
    cmake --build $build --config Release --parallel
} else {
    meson setup $build $engine --backend=$Backend --native-file (Join-Path $PSScriptRoot 'meson-uwp.ini') --buildtype=release
    meson compile -C $build
}

$binary = Get-ChildItem $build -Filter '*.exe' -Recurse | Where-Object { $_.Name -notmatch 'test' } | Sort-Object FullName | Select-Object -First 1
if (-not $binary) { throw 'Build completed but no executable was produced.' }
Copy-Item $binary.FullName (Join-Path $stage 'krkrsdl2.exe')

Copy-Item (Join-Path $PSScriptRoot 'Package.appxmanifest') $stage
Add-Type -AssemblyName System.Drawing
$assetDirectory = Join-Path $stage 'Assets'
New-Item -ItemType Directory -Path $assetDirectory | Out-Null
foreach ($asset in @(@{ Name = 'StoreLogo.png'; Size = 50 }, @{ Name = 'Square150x150Logo.png'; Size = 150 }, @{ Name = 'Square44x44Logo.png'; Size = 44 })) {
    $bitmap = New-Object System.Drawing.Bitmap($asset.Size, $asset.Size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::FromArgb(20, 24, 32))
    $graphics.Dispose()
    $bitmap.Save((Join-Path $assetDirectory $asset.Name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}
foreach ($name in @('StoreLogo.png', 'Square150x150Logo.png', 'Square44x44Logo.png')) {
    if (-not (Test-Path (Join-Path $assetDirectory $name))) { throw "Failed to generate package asset: $name" }
}

$game = $env:KRKR_GAME
if (-not $game -and $GameArchiveUrl) {
    $download = Join-Path $root 'out\game.zip'
    $game = Join-Path $root 'out\game'
    New-Item -ItemType Directory -Path $game -Force | Out-Null
    Invoke-WebRequest -Uri $GameArchiveUrl -OutFile $download
    Expand-Archive -Path $download -DestinationPath $game -Force
}
if ($game) {
    if (-not (Test-Path $game)) { throw "Game path does not exist: $game" }
    if (-not (Test-Path (Join-Path $game 'startup.tjs')) -and -not (Get-ChildItem $game -Filter '*.xp3' -File)) {
        throw 'KRKR_GAME must contain startup.tjs or at least one XP3 archive.'
    }
    Copy-Item (Join-Path $game '*') $stage -Recurse -Force
}

if (Test-Path $msix) { Remove-Item $msix -Force }
makeappx pack /d $stage /p $msix /o
Write-Host "Created $msix"