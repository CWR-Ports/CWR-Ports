{ pkgs ? import <nixpkgs> { config.allowUnfree = true; config.android_sdk.accept_license = true; } }:

let
  androidEnv = pkgs.androidenv.composeAndroidPackages {
    platformToolsVersion = "35.0.2";
    buildToolsVersions = [ "34.0.0" ];
    includeEmulator = false;
    emulatorVersion = "33.1.24";
    platformVersions = [ "34" "26" ];
    includeSources = false;
    includeSystemImages = false;
    systemImageTypes = [ "google_apis_playstore" ];
    abiVersions = [ "arm64-v8a" ];
    cmakeVersions = [ "3.22.1" ];
    includeNDK = true;
    ndkVersions = [ "26.1.10909125" ];
    useGoogleAPIs = false;
    useGoogleTVAddOns = false;
  };
in
pkgs.mkShell {
  buildInputs = [
    androidEnv.androidsdk
    pkgs.gradle
    pkgs.jdk17
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config-unwrapped
    pkgs.perl
    pkgs.python3
    pkgs.autoconf
    pkgs.autoconf-archive
    pkgs.automake
    pkgs.libtool
    pkgs.libGL
    pkgs.vulkan-headers
    pkgs.vulkan-loader
    pkgs.clang-tools
    pkgs.shaderc
    
    # Graphics and windowing dependencies
    pkgs.wayland
    pkgs.wayland-protocols
    pkgs.libxkbcommon
    pkgs.libx11
    pkgs.libxcursor
    pkgs.libxi
    pkgs.libxrandr
    pkgs.libxext
    pkgs.libxtst
    pkgs.libxfixes
    pkgs.ibus
    pkgs.dbus
    pkgs.libdecor
    
    # Audio dependencies
    pkgs.alsa-lib
    pkgs.libpulseaudio
  ];
  
  ANDROID_HOME = "${androidEnv.androidsdk}/libexec/android-sdk";
  ANDROID_NDK_ROOT = "${androidEnv.androidsdk}/libexec/android-sdk/ndk/26.1.10909125";
  ANDROID_NDK_HOME = "${androidEnv.androidsdk}/libexec/android-sdk/ndk/26.1.10909125";
  VCPKG_FORCE_SYSTEM_BINARIES = "1";
  LD_LIBRARY_PATH = "${pkgs.stdenv.cc.cc.lib}/lib:/run/opengl-driver/lib:${pkgs.lib.makeLibraryPath [
    pkgs.libGL
    pkgs.vulkan-loader
    pkgs.wayland
    pkgs.libxkbcommon
    pkgs.libx11
    pkgs.libxcursor
    pkgs.libxi
    pkgs.libxrandr
    pkgs.libxext
    pkgs.libxtst
    pkgs.libxfixes
    pkgs.ibus
    pkgs.dbus
    pkgs.libdecor
    pkgs.alsa-lib
    pkgs.libpulseaudio
  ]}";
  hardeningDisable = [ "format" ];
}
