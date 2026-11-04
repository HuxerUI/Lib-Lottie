# Lib-Lottie

A Lottie animation library for HuxerUI, powered by ThorVG. Supports asynchronous loading, playback control, and progress seeking.

## Requirements

- HuxerUI SDK 0.4.0
- A C++20 compiler and the HuxerUI toolchain for the target platform
- CMake 3.20 or later, Git, and a compatible build tool such as Ninja

Supports macOS, iOS, Android, Windows, Linux, and Web through the same ThorVG CPU renderer. ThorVG is fetched at a pinned revision and built with the application's CMake toolchain; Meson is not required.

Web requires a browser with WebCodecs `VideoFrame` support in a secure context, such as HTTPS or localhost. File sources use HuxerUI's virtual filesystem on Web.

ThorVG is optimized in every build configuration, including Debug, while application and playback code retain their normal debug settings. Set `LOTTIE_THORVG_OPTIMIZE=OFF` when debugging the renderer itself; unoptimized rasterization can substantially reduce playback frame rates.

## Installation

Add the dependency after creating your application target with `huxerui_add_app`:

```cmake
huxerui_use_library(my_app
    TARGET HuxerUI::Lottie
    URL "https://github.com/HuxerUI/Lib-Lottie.git"
    COMMIT "<full-40-character-commit-sha>"
)
```

Replace `my_app` with your application target and the commit placeholder with the full SHA of the revision to use. `huxerui_use_library` requires either `COMMIT` or `TAG` for Git dependencies; use `TAG` instead of `COMMIT` when selecting a release tag.

## Quick start

Place an animation at `resources/raw/motion.json`. HuxerUI generates the `app::raw::motion_json` identifier in `<app_resources.h>`.

Register `huxerui::lottie::Install` once in the application's `application_hooks`, then add `Lottie` during composition:

```cpp
#include <huxerui/lottie.h>
#include <app_resources.h>

using namespace huxerui;

lottie::Lottie(app::raw::motion_json)
    .With(Frame{.width = 240.0F, .height = 240.0F});
```

## API

The public API is declared in [huxerui/lottie.h](include/huxerui/lottie.h), under the `huxerui::lottie` namespace.

### Sources

`Lottie` accepts these source types:

| Type | Use |
| --- | --- |
| `RawResource` | Generated resource keys for packaged animations |
| `RawAsset` | Application-owned JSON bytes, created with `RawAsset::FromBytes` or `RawAsset::CopyBytes` |
| `File` | JSON files in application-accessible local storage |

```cpp
lottie::Lottie(File("/path/to/animation.json"), lottie::LottiePlayback{.iterations = 0})
    .With(Frame{.width = 240.0F, .height = 240.0F});
```

Payload reads and decoding use the backend's serial execution queue. Native platforms use a dedicated worker thread; Web schedules work on the browser event loop without requiring pthreads. Changing the source clears the previous animation and queues the replacement. Preserve the source's identity across recomposition to avoid unnecessary reloads.

File identity follows `File` equality. Recomposition with the same path does not reload its contents, and files are not watched. Changing `LottiePlayback::restart` replays the loaded animation; change the source or remount the component to reread a file. Missing or inaccessible files emit `LottieError::Stage::Resource`; empty files and invalid Lottie JSON emit `LottieError::Stage::Decode`.

`File` does not retain a `FileReference` access grant. Keep any required grant alive during loading. Network fetching remains the application's responsibility.

### Playback

Pass `LottiePlayback` to control automatic playback:

| Field | Default | Description |
| --- | --- | --- |
| `playing` | `true` | Pauses or resumes without resetting the playhead |
| `iterations` | `1` | Total number of passes; `0` repeats indefinitely |
| `speed` | `1.0` | Playback rate; must be finite and in `(0, 1000]` |
| `restart` | `0` | Change this token to reset the playhead, pass count, and completion state |

Finite playback holds the last frame after completion. Setting `playing = true` does not restart a completed animation; change `restart` to replay. A restart also works while paused, with `playing` determining whether the new run advances.

### Progress

Pass `LottieProgress` to select a frame without advancing time:

```cpp
lottie::Lottie(app::raw::motion_json, lottie::LottieProgress{0.5})
    .With(Frame{.width = 240.0F, .height = 240.0F});
```

Progress must be finite and is clamped to `[0, 1]`. Zero selects the first frame; one selects the last. Switching back to `LottiePlayback` resumes from the selected position unless the restart token changes.

### Events

Attach handlers with `.On<lottie::LottieEvents::Loaded>(handler)`, `.On<lottie::LottieEvents::Completed>(handler)`, or `.On<lottie::LottieEvents::Failed>(handler)`.

| Event | Handler argument | Description |
| --- | --- | --- |
| `LottieEvents::Loaded` | `const LottieMetadata&` | Decoding succeeded; provides duration in seconds, frame count, and source dimensions |
| `LottieEvents::Completed` | None | Finite playback finished and its last frame was published; emitted once per playback run |
| `LottieEvents::Failed` | `const LottieError&` | Resource access, decoding, or rendering failed; provides a stage and diagnostic message |

Handlers run on the UI thread. Copy event arguments if retaining them after the callback. `LottieEvents::Loaded` does not guarantee that the first frame has been displayed. `LottieProgress` and infinite playback do not emit `LottieEvents::Completed`.

Invalid speed or progress values throw `std::invalid_argument`. Operational failures emit `LottieEvents::Failed`; change the source or remount the component to retry.

## Rendering and format support

Animations are centered and scaled to fit while preserving their aspect ratio and transparency. Provide layout constraints through the parent or a `Frame` modifier. Raster dimensions follow display density, with the longest edge capped at 2048 pixels.

Automatic playback pauses in background windows and under HuxerUI's reduced-motion policy. Explicit progress changes remain available under reduced motion; background rendering is deferred until the window returns to the foreground.

The library supports vector keyframe Lottie JSON. Expressions, external images, fonts, and `.lottie` archives are not supported. Unsupported animation features may be ignored by the decoder rather than reported as errors.

## Example

The [preview application](examples/preview) demonstrates playback, replay, looping, seeking, and source switching. Set `HUXERUI_HOME` to your installed SDK directory, then run from the preview directory:

```sh
cd examples/preview
huxerui build <platform> --profile release
huxerui run <platform> --profile release
```

Replace `<platform>` with `macos`, `ios`, `android`, `windows`, `linux`, or `web`. Use `huxerui devices` and `--device <id>` when selecting a device or simulator.

## Tests

The native desktop tests require the HuxerUI SDK testing component:

```sh
cmake -S . -B build/tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DLOTTIE_BUILD_TESTS=ON \
  -DHUXERUI_HOME="$HUXERUI_HOME"
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

## License

Lib-Lottie is licensed under the [MIT License](LICENSE). Third-party license information is available in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
