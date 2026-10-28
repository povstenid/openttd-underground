<#
.SYNOPSIS
    Builds an Android APK of OpenTTD (phones, tablets and Android TV).

.DESCRIPTION
    1. Builds the dependencies for each ABI with vcpkg (SDL2 as a shared library, the rest static).
    2. Builds strgen/settingsgen for the host, as the cross build cannot run its own tools.
    3. Cross-compiles OpenTTD with the NDK into libopenttd.so for each ABI.
    4. Packs the libraries, the game data (languages, base sets, script libraries, the metro set) and the
       Java activity into an APK and signs it with a local key.

    Needs the Android SDK (platform 34, build-tools 34.0.0, NDK), a JDK 17 and vcpkg.
    OpenTTD has no graphics of its own: point -Baseset at a directory containing OpenGFX
    (opengfx-*.tar as downloaded from https://cdn.openttd.org/opengfx-releases/, or opengfx.obg with its
    .grf files); OpenSFX/OpenMSX can be placed there too.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File os\android\build-apk.ps1 -Baseset E:\content\opengfx
#>
param(
    [string]$Sdk = $(if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { 'C:\Android\Sdk' }),
    [string]$NdkVersion = '26.3.11579264',
    [string]$BuildToolsVersion = '34.0.0',
    [int]$Platform = 34,
    [string]$Jdk = $(if ($env:JAVA_HOME) { $env:JAVA_HOME } else { 'C:\Android\jdk-17.0.20.1+1' }),
    [string]$Vcpkg = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\vcpkg' }),
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build-android'),
    [string]$Baseset = '',
    [string]$DepsDir = '',
    [string[]]$Abis = @('arm64-v8a', 'armeabi-v7a'),
    # 24: getifaddrs, used to find the local network addresses, exists since Android 7.0.
    [int]$MinSdk = 24
)

$ErrorActionPreference = 'Stop'

# "powershell -File" passes a comma separated list as one string.
$Abis = @($Abis | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

$Source = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$AndroidDir = $PSScriptRoot
New-Item -ItemType Directory -Force $BuildDir | Out-Null
$BuildDir = (Resolve-Path $BuildDir).Path

$Ndk = Join-Path $Sdk "ndk\$NdkVersion"
$BuildTools = Join-Path $Sdk "build-tools\$BuildToolsVersion"
$AndroidJar = Join-Path $Sdk "platforms\android-$Platform\android.jar"
$Ninja = Get-ChildItem (Join-Path $Sdk 'cmake') -Recurse -Filter ninja.exe | Select-Object -First 1 -ExpandProperty FullName
$LlvmStrip = Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
if (-not $DepsDir) { $DepsDir = Join-Path $BuildDir 'vcpkg_installed' }
$Triplets = @{ 'arm64-v8a' = 'arm64-android'; 'armeabi-v7a' = 'arm-neon-android'; 'x86' = 'x86-android' }
$NdkTriples = @{ 'arm64-v8a' = 'aarch64-linux-android'; 'armeabi-v7a' = 'arm-linux-androideabi'; 'x86' = 'i686-linux-android' }

foreach ($path in @($Ndk, $BuildTools, $AndroidJar, $Jdk, (Join-Path $Vcpkg 'vcpkg.exe'))) {
    if (-not (Test-Path $path)) { throw "Missing $path; check the -Sdk/-NdkVersion/-Jdk/-Vcpkg parameters." }
}
if (-not $Ninja) { throw "No ninja.exe found under $Sdk\cmake; install an SDK CMake package." }

$env:JAVA_HOME = $Jdk
$env:PATH = "$Jdk\bin;$env:PATH"
$env:ANDROID_NDK_HOME = $Ndk

function Invoke-Tool {
    param([string]$Exe, [string[]]$Arguments)
    # Windows PowerShell turns any stderr output of a tool into an error when output is redirected; rely on the exit code instead.
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

function Step([string]$Text) { Write-Host "`n=== $Text" -ForegroundColor Cyan }

# Version: the APK version code must grow with every build, the commit count does.
Push-Location $Source
$VersionCode = [int](git rev-list --count HEAD)
$Hash = (git rev-parse --short=10 HEAD)
Pop-Location
$CMakeVersion = (Select-String -Path (Join-Path $Source 'CMakeLists.txt') -Pattern '^\s*VERSION\s+([0-9.]+)').Matches[0].Groups[1].Value
$VersionName = "$CMakeVersion-g$Hash"

Step "Dependencies (vcpkg)"
foreach ($abi in $Abis) {
    $triplet = $Triplets[$abi]
    if (-not $triplet) { throw "Unsupported ABI $abi" }
    Push-Location $BuildDir  # Outside the source tree, so vcpkg does not pick up the manifest of the desktop build.
    try {
        Invoke-Tool (Join-Path $Vcpkg 'vcpkg.exe') @('install', 'zlib', 'liblzma', 'libpng', 'lzo', 'freetype', 'sdl2',
            '--classic', "--triplet=$triplet", "--overlay-triplets=$AndroidDir\vcpkg-triplets", "--x-install-root=$DepsDir")
    } finally { Pop-Location }

    $sdlVersion = Join-Path $DepsDir "$triplet\include\SDL2\SDL_version.h"
    $native = (Select-String -Path $sdlVersion -Pattern '#define SDL_(MAJOR_VERSION|MINOR_VERSION|PATCHLEVEL)\s+(\d+)').Matches | ForEach-Object { $_.Groups[2].Value }
    $java = (Select-String -Path "$AndroidDir\java\org\libsdl\app\SDLActivity.java" -Pattern 'SDL_(MAJOR|MINOR|MICRO)_VERSION = (\d+);').Matches | ForEach-Object { $_.Groups[2].Value }
    if (($native -join '.') -ne ($java -join '.')) {
        throw "SDL $($native -join '.') from vcpkg does not match the Java files of SDL $($java -join '.') in os\android\java\org\libsdl\app; copy them from the SDL $($native -join '.') source (android-project)."
    }
}

Step "Host tools"
$HostDir = Join-Path $BuildDir 'host'
Invoke-Tool cmake @('-S', $Source, '-B', $HostDir, '-DOPTION_TOOLS_ONLY=ON')
Invoke-Tool cmake @('--build', $HostDir, '--config', 'Release')

$Libs = @{}
foreach ($abi in $Abis) {
    Step "OpenTTD for $abi"
    $triplet = $Triplets[$abi]
    $abiDir = Join-Path $BuildDir $abi
    Invoke-Tool cmake @('-S', $Source, '-B', $abiDir, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$Ninja",
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg\scripts\buildsystems\vcpkg.cmake",
        "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$Ndk\build\cmake\android.toolchain.cmake",
        "-DVCPKG_TARGET_TRIPLET=$triplet",
        '-DVCPKG_MANIFEST_MODE=OFF',
        "-DVCPKG_INSTALLED_DIR=$DepsDir",
        "-DANDROID_ABI=$abi",
        '-DANDROID_STL=c++_shared',  # Like the vcpkg libraries (SDL2), so the app has a single C++ runtime.
        "-DANDROID_PLATFORM=android-$MinSdk",
        "-DHOST_BINARY_DIR=$HostDir")
    Invoke-Tool cmake @('--build', $abiDir, '--target', 'openttd')
    $Libs[$abi] = @((Join-Path $abiDir 'libopenttd.so'), (Join-Path $DepsDir "$triplet\lib\libSDL2.so"),
        (Join-Path $Ndk "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\$($NdkTriples[$abi])\libc++_shared.so"))
}

Step "Staging"
$Stage = Join-Path $BuildDir 'apk'
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
$Assets = Join-Path $Stage 'assets'
$DataBuild = Join-Path $BuildDir $Abis[0]

New-Item -ItemType Directory -Force "$Assets\lang", "$Assets\baseset", "$Assets\scripts" | Out-Null
Copy-Item "$DataBuild\lang\*.lng" "$Assets\lang"
Copy-Item "$DataBuild\baseset\*" "$Assets\baseset" -Recurse
Copy-Item "$Source\bin\scripts\*" "$Assets\scripts"
foreach ($dir in 'ai', 'game') {
    Copy-Item "$DataBuild\$dir" "$Assets\$dir" -Recurse
    if (Test-Path "$Assets\$dir\regression") { Remove-Item -Recurse -Force "$Assets\$dir\regression" }
}
# The bundled metro set; a first new game enables it, as the default config has no [newgrf] section.
if (-not (Test-Path "$DataBuild\newgrf\metro_set.grf")) { throw "No metro_set.grf in $DataBuild\newgrf; the OpenTTD build copies it there." }
Copy-Item "$DataBuild\newgrf" "$Assets\newgrf" -Recurse
if ($Baseset) {
    Get-ChildItem $Baseset -File | Where-Object { -not (Test-Path (Join-Path "$Assets\baseset" $_.Name)) } | Copy-Item -Destination "$Assets\baseset"
}
if (-not (Get-ChildItem "$Assets\baseset" -Filter 'opengfx*' | Where-Object { $_.Extension -in '.obg', '.tar' })) {
    throw "No OpenGFX in the base sets; pass -Baseset with a directory containing opengfx-*.tar or opengfx.obg with its .grf files."
}

$Native = Join-Path $Stage 'native'
foreach ($abi in $Abis) {
    New-Item -ItemType Directory -Force "$Native\lib\$abi" | Out-Null
    foreach ($lib in $Libs[$abi]) {
        Invoke-Tool $LlvmStrip @('--strip-unneeded', '-o', (Join-Path "$Native\lib\$abi" (Split-Path $lib -Leaf)), $lib)
    }
}

Step "Java"
$Classes = Join-Path $Stage 'classes'
New-Item -ItemType Directory -Force $Classes, "$Stage\dex" | Out-Null
$JavaFiles = Get-ChildItem "$AndroidDir\java" -Recurse -Filter *.java | ForEach-Object { $_.FullName }
Invoke-Tool javac (@('-source', '8', '-target', '8', '-Xlint:-options', '-nowarn', '-encoding', 'UTF-8', '-bootclasspath', "$AndroidJar;$BuildTools\core-lambda-stubs.jar", '-d', $Classes) + $JavaFiles)
Invoke-Tool jar @('cf', "$Stage\classes.jar", '-C', $Classes, '.')
Invoke-Tool "$BuildTools\d8.bat" @('--release', '--min-api', "$MinSdk", '--lib', $AndroidJar, '--output', "$Stage\dex", "$Stage\classes.jar")

Step "APK"
$Unsigned = Join-Path $Stage 'unsigned.apk'
Invoke-Tool "$BuildTools\aapt2.exe" @('compile', '--dir', "$AndroidDir\res", '-o', "$Stage\res.zip")
Invoke-Tool "$BuildTools\aapt2.exe" @('link', '-o', $Unsigned, '-I', $AndroidJar,
    '--manifest', "$AndroidDir\AndroidManifest.xml",
    '--min-sdk-version', "$MinSdk", '--target-sdk-version', "$Platform",
    '--version-code', "$VersionCode", '--version-name', $VersionName, "$Stage\res.zip")

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($Unsigned, 'Update')
try {
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, "$Stage\dex\classes.dex", 'classes.dex') | Out-Null
    # Not via "aapt2 link -A": on Windows it stores the asset paths with backslashes, which Android cannot find.
    foreach ($file in Get-ChildItem $Assets -Recurse -File) {
        $name = 'assets/' + $file.FullName.Substring($Assets.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $name) | Out-Null
    }
    foreach ($abi in $Abis) {
        foreach ($lib in Get-ChildItem "$Native\lib\$abi") {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $lib.FullName, "lib/$abi/$($lib.Name)") | Out-Null
        }
    }
} finally { $zip.Dispose() }

$Aligned = Join-Path $Stage 'aligned.apk'
Invoke-Tool "$BuildTools\zipalign.exe" @('-p', '-f', '4', $Unsigned, $Aligned)

# A local key; keep it, as Android only installs updates signed with the same key.
$Keystore = Join-Path $BuildDir 'openttd-android.keystore'
if (-not (Test-Path $Keystore)) {
    Invoke-Tool keytool @('-genkeypair', '-keystore', $Keystore, '-storepass', 'openttd', '-keypass', 'openttd',
        '-alias', 'openttd', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=OpenTTD Android build')
}
$Apk = Join-Path $BuildDir "OpenTTD-$VersionName.apk"
Invoke-Tool "$BuildTools\apksigner.bat" @('sign', '--ks', $Keystore, '--ks-pass', 'pass:openttd', '--key-pass', 'pass:openttd', '--out', $Apk, $Aligned)

Step "Done"
Write-Host "$Apk ($([math]::Round((Get-Item $Apk).Length / 1MB, 1)) MB, ABIs: $($Abis -join ', '), version $VersionName / $VersionCode)"
