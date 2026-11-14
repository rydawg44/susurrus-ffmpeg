# FFmpeg source for Susurrus

Susurrus is an audiobook player for iPhone, Apple Watch and CarPlay. On iPhone it includes
`FFmpegDecode.framework`, a dynamic framework built from FFmpeg's audio decoders plus a small C shim.
It decodes Ogg, Opus, WMA, APE, WavPack, TTA and Matroska audio when a book is imported.

FFmpeg is licensed under the GNU Lesser General Public License, version 2.1 or later. This repository holds
everything needed to rebuild that framework exactly as it ships in the app:

| Path | What it is |
| --- | --- |
| `ffmpeg-9.0.2.tar.xz` | The unmodified FFmpeg 9.0.2 release, as downloaded from <https://ffmpeg.org/releases/> |
| `ffmpeg-9.0.2.tar.xz.asc` | FFmpeg's signature for that release |
| `Scripts/build-ffmpeg.sh` | The build script, including every `configure` option used |
| `Vendor/FFmpegDecode/shim/` | The C shim compiled into the framework (`fdg_*` functions), under the same licence |
| `Vendor/FFmpegDecode/LGPL-2.1.txt` | The licence text |
| `Vendor/FFmpegDecode/PrivacyInfo.xcprivacy` | The framework's privacy manifest |

SHA-256 of the tarball:
`8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`

FFmpeg's source is not modified. Only LGPL components are enabled: the build uses no `--enable-gpl` and no
`--enable-nonfree`.

## Rebuilding

On a Mac with Xcode:

```bash
mkdir -p ~/Library/Caches/susurrus-ffmpeg Packages/SusurrusKit/Vendor
cp ffmpeg-9.0.2.tar.xz ~/Library/Caches/susurrus-ffmpeg/
./Scripts/build-ffmpeg.sh
```

The script writes `Packages/SusurrusKit/Vendor/FFmpegDecode.xcframework` with iOS device, iOS Simulator and
macOS slices. If the tarball isn't in the cache folder, the script downloads the same release from ffmpeg.org.
It takes about 15 minutes.

## Replacing the library

The framework is linked dynamically, as `Frameworks/FFmpegDecode.framework` inside `Susurrus.app`. You may
replace it with your own build, from this source or a modified version, and re-sign the app with your own
certificate to run it.

## Contact

Questions about this source: open an issue on this repository.
