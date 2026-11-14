#!/bin/bash
# Builds FFmpegDecode.xcframework: a single LGPL dynamic framework holding FFmpeg's
# audio decoders and Susurrus's C shim. Only LGPL code is enabled (no --enable-gpl
# and no --enable-nonfree). Run from anywhere; takes roughly 15 minutes on 4 cores.
set -euo pipefail

VERSION="${FFMPEG_VERSION:-9.0.2}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# FFmpeg's configure cannot handle spaces in paths, so this stays out of the project folder.
WORK="${FFMPEG_WORK_DIR:-$HOME/Library/Caches/susurrus-ffmpeg}"
OUTPUT="$ROOT/Packages/SusurrusKit/Vendor/FFmpegDecode.xcframework"
SHIM="$ROOT/Vendor/FFmpegDecode/shim"
JOBS="$(sysctl -n hw.ncpu)"

mkdir -p "$WORK"
TARBALL="$WORK/ffmpeg-$VERSION.tar.xz"
SOURCE="$WORK/ffmpeg-$VERSION"
[ -f "$TARBALL" ] || curl -fL -o "$TARBALL" "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
[ -d "$SOURCE" ] || tar -xf "$TARBALL" -C "$WORK"

DECODERS="vorbis,opus,wmav1,wmav2,wmapro,wmalossless,ape,wavpack,tta,alac,flac,mp1,mp2,mp3,mp3float,aac,pcm_s16le,pcm_s24le,pcm_f32le"
DEMUXERS="ogg,asf,ape,wv,tta,matroska,mov,flac,mp3,aac,wav"
PARSERS="vorbis,opus,mpegaudio,aac,flac"

build_slice() { # name sdk target arch extra_configure...
  local name="$1" sdk="$2" target="$3" arch="$4"; shift 4
  local prefix="$WORK/install/$name" objdir="$WORK/obj/$name"
  local sysroot; sysroot="$(xcrun --sdk "$sdk" --show-sdk-path)"
  local cc; cc="$(xcrun --sdk "$sdk" --find clang)"
  if [ ! -f "$prefix/lib/libavformat.a" ]; then
    rm -rf "$objdir"; mkdir -p "$objdir"
    (cd "$objdir" && "$SOURCE/configure" \
      --prefix="$prefix" --enable-cross-compile --target-os=darwin --arch="$arch" \
      --cc="$cc" --sysroot="$sysroot" \
      --extra-cflags="-target $target -fembed-bitcode=off -Os" \
      --extra-ldflags="-target $target" \
      --disable-everything --disable-programs --disable-doc --disable-debug \
      --disable-avdevice --disable-avfilter --disable-swscale --disable-network \
      --disable-autodetect --disable-iconv --disable-zlib --disable-bzlib \
      --enable-static --disable-shared --enable-pic --enable-small \
      --enable-swresample --enable-protocol=file \
      --enable-decoder="$DECODERS" --enable-demuxer="$DEMUXERS" --enable-parser="$PARSERS" \
      "$@" >/dev/null)
    make -C "$objdir" -j"$JOBS" >/dev/null 2>&1
    make -C "$objdir" install >/dev/null 2>&1
  fi

  local linked="$WORK/linked/$name"; mkdir -p "$linked"
  "$cc" -target "$target" -isysroot "$sysroot" -I"$prefix/include" -I"$SHIM" -Os -fPIC \
    -c "$SHIM/fdg_decoder.c" -o "$linked/fdg_decoder.o"
  "$cc" -target "$target" -isysroot "$sysroot" -dynamiclib \
    -install_name @rpath/FFmpegDecode.framework/FFmpegDecode \
    -compatibility_version 1 -current_version 1 \
    "$linked/fdg_decoder.o" -L"$prefix/lib" -lavformat -lavcodec -lswresample -lavutil \
    -Wl,-exported_symbol,'_fdg_*' -Wl,-dead_strip \
    -framework CoreFoundation \
    -o "$linked/FFmpegDecode"
  echo "built $name"
}

write_framework() { # directory dylib platform min_os versioned
  local dir="$1" dylib="$2" platform="$3" minos="$4" versioned="$5"
  rm -rf "$dir"
  local contents="$dir"
  if [ "$versioned" = yes ]; then
    contents="$dir/Versions/A"; mkdir -p "$contents/Resources" "$contents/Headers" "$contents/Modules"
    ln -s A "$dir/Versions/Current"
    ln -s Versions/Current/FFmpegDecode "$dir/FFmpegDecode"
    ln -s Versions/Current/Resources "$dir/Resources"
    ln -s Versions/Current/Headers "$dir/Headers"
    ln -s Versions/Current/Modules "$dir/Modules"
  else
    mkdir -p "$contents/Headers" "$contents/Modules"
  fi
  cp "$dylib" "$contents/FFmpegDecode"
  cp "$SHIM/fdg_decoder.h" "$contents/Headers/"
  cat > "$contents/Headers/FFmpegDecode.h" <<'H'
#include <FFmpegDecode/fdg_decoder.h>
H
  cat > "$contents/Modules/module.modulemap" <<'M'
framework module FFmpegDecode {
  umbrella header "FFmpegDecode.h"
  export *
  module * { export * }
}
M
  local plist="$contents/Info.plist"
  [ "$versioned" = yes ] && plist="$contents/Resources/Info.plist"
  # FFmpeg's file protocol calls fstat, a required-reason API, so the framework declares it.
  if [ "$versioned" = yes ]; then
    cp "$ROOT/Vendor/FFmpegDecode/PrivacyInfo.xcprivacy" "$contents/Resources/"
  else
    cp "$ROOT/Vendor/FFmpegDecode/PrivacyInfo.xcprivacy" "$contents/"
  fi
  cat > "$plist" <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleDevelopmentRegion</key><string>en</string>
<key>CFBundleExecutable</key><string>FFmpegDecode</string>
<key>CFBundleIdentifier</key><string>app.foredge.FFmpegDecode</string>
<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
<key>CFBundleName</key><string>FFmpegDecode</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleSupportedPlatforms</key><array><string>$platform</string></array>
<key>MinimumOSVersion</key><string>$minos</string>
</dict></plist>
P
}

build_slice ios-arm64 iphoneos arm64-apple-ios17.0 aarch64
build_slice sim-arm64 iphonesimulator arm64-apple-ios17.0-simulator aarch64
build_slice sim-x86_64 iphonesimulator x86_64-apple-ios17.0-simulator x86_64 --disable-x86asm
build_slice mac-arm64 macosx arm64-apple-macos14.0 aarch64
build_slice mac-x86_64 macosx x86_64-apple-macos14.0 x86_64 --disable-x86asm

STAGE="$WORK/frameworks"; rm -rf "$STAGE"; mkdir -p "$STAGE/ios" "$STAGE/sim" "$STAGE/mac"
lipo -create "$WORK/linked/sim-arm64/FFmpegDecode" "$WORK/linked/sim-x86_64/FFmpegDecode" -output "$WORK/linked/sim-universal"
lipo -create "$WORK/linked/mac-arm64/FFmpegDecode" "$WORK/linked/mac-x86_64/FFmpegDecode" -output "$WORK/linked/mac-universal"
write_framework "$STAGE/ios/FFmpegDecode.framework" "$WORK/linked/ios-arm64/FFmpegDecode" iPhoneOS 17.0 no
write_framework "$STAGE/sim/FFmpegDecode.framework" "$WORK/linked/sim-universal" iPhoneSimulator 17.0 no
write_framework "$STAGE/mac/FFmpegDecode.framework" "$WORK/linked/mac-universal" MacOSX 14.0 yes

rm -rf "$OUTPUT"
xcodebuild -create-xcframework \
  -framework "$STAGE/ios/FFmpegDecode.framework" \
  -framework "$STAGE/sim/FFmpegDecode.framework" \
  -framework "$STAGE/mac/FFmpegDecode.framework" \
  -output "$OUTPUT"
echo "wrote $OUTPUT"
