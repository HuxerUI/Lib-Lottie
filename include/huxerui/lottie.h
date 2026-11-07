#pragma once

#include <huxerui/app.h>
#include <huxerui/event.h>
#include <huxerui/file.h>
#include <huxerui/resource.h>
#include <huxerui/view.h>
#include <cstdint>
#include <string>
#include <variant>

namespace huxerui::lottie {

/// Controls automatic playback while retaining the mounted player's position across recomposition.
/// The default plays once at normal speed and holds the last frame after completion.
struct LottiePlayback {
  /// Whether the playhead advances. False pauses without resetting position or the completed-pass count.
  /// Resuming excludes time spent paused; setting true after completion does not restart the animation.
  bool playing = true;
  /// Total passes, including the first. Zero repeats indefinitely.
  /// LottieEvents::Completed is emitted after the final pass, not after each pass.
  /// Changing this value does not restart playback.
  std::uint32_t iterations = 1;
  /// Playback rate relative to the source timeline; 1.0 is normal speed and 2.0 is twice as fast.
  /// Must be finite and in (0, 1000]. Changing speed preserves the current position.
  double speed = 1.0;
  /// Change this token to reset position, pass count, and completion state, even while paused.
  /// The token need not increase monotonically. The playing flag still determines whether the new run advances.
  /// Restarting reuses the loaded animation; it does not reload the source or retry a failed load.
  std::uint64_t restart = 0;
  /// Compares the requested controls, not the mounted player's current position or completion state.
  bool operator==(const LottiePlayback&) const = default;
};

/// Selects a frame directly without advancing time or emitting LottieEvents::Completed.
/// Rendering is asynchronous; rapid updates may be coalesced to the latest requested position.
struct LottieProgress {
  /// Normalized position. Finite values are clamped to [0, 1]; zero selects the first frame and one the last.
  /// Intermediate positions select integer frames without interpolation. NaN and infinity are invalid.
  double value = 0.0;
  /// Compares the supplied values before Lottie clamps them to the supported range.
  bool operator==(const LottieProgress&) const = default;
};

/// Selects automatic playback or caller-controlled progress without reloading an unchanged source.
/// Entering LottieProgress clears the playback completion state. Returning to LottiePlayback starts a new pass count
/// at the selected position, unless restart differs from the last LottiePlayback token for this source, in which case
/// it resets to the beginning. Before the first LottiePlayback value, the remembered restart token is zero.
using LottieControl = std::variant<LottiePlayback, LottieProgress>;

/// A packaged JSON resource key, an owned raw asset, or an application-accessible local JSON file.
/// RawResource is resolved in the current HuxerUI resource context; payload reads use the backend's execution queue.
/// Retain RawAsset values across recomposition: creating a new asset can trigger a reload even for identical bytes.
/// File identity follows File equality. Recomposition with an equal File does not reread it, even if its contents
/// changed. Files are not watched; change the source or remount to reload.
/// LottiePlayback::restart only resets playback.
/// File does not retain a FileReference access grant; the caller must keep any required grant alive during loading.
/// Network access belongs to the caller; strings and URLs are not interpreted as sources.
using LottieSource = std::variant<RawResource, RawAsset, File>;

/// Source timeline and composition dimensions reported after successful decoding.
/// Values delivered by LottieEvents::Loaded are positive and finite, with at least one frame.
struct LottieMetadata {
  /// Duration of one complete pass in seconds at normal speed, excluding repeats and pauses.
  double duration = 0.0;
  /// Number of selectable integer frames. Indices range from zero through frames - 1.
  std::uint32_t frames = 0;
  /// Original composition dimensions in source units, independent of layout size and display density.
  Size size;
};

/// Describes an operational failure delivered through LottieEvents::Failed on the owning UI thread.
struct LottieError {
  /// Identifies the operation that failed; message provides additional diagnostic context.
  enum class Stage {
    /// Resource lookup, an absent asset, file access, or reading the source payload failed.
    Resource,
    /// Backend initialization, JSON decoding, or animation metadata validation failed.
    Decode,
    /// Pixel-buffer allocation, frame rendering, or texture publication failed.
    Render
  };
  /// Failure category suitable for application error handling.
  Stage stage;
  /// Human-readable diagnostic text. Its wording is not a stable machine-readable error code.
  std::string message;
};

/// Event keys for Lottie views. Bind a key with On<Event>(handler), such as On<LottieEvents::Loaded>(handler).
/// Handlers run on the owning UI thread.
struct LottieEvents {
  /// Emitted once for each successful load, before the first rendered frame is necessarily available.
  /// The LottieMetadata reference is borrowed for the handler call; copy it to retain the information.
  /// Decoding success does not guarantee support for every feature used by the source animation.
  struct Loaded : Event<void(const LottieMetadata&)> {};

  /// Emitted at most once per finite playback run after its final frame has been published to the texture.
  /// Publication does not guarantee that the platform window has presented that frame yet.
  /// Infinite playback and LottieProgress do not emit this event; ordinary pause/resume does not rearm it.
  struct Completed : Event<void()> {};

  /// Emitted when loading or rendering fails. The player stops producing frames for that source.
  /// A load failure leaves the view empty; a render failure preserves the last published frame, if any.
  /// Change the source or remount the component to retry. Invalid control arguments throw instead of emitting Failed.
  /// The LottieError reference is borrowed for the handler call.
  /// Copy it before retaining or forwarding it asynchronously.
  struct Failed : Event<void(const LottieError&)> {};
};

/// Installs the application's shared ThorVG backend and manages its lifetime through application shutdown.
/// Add this function once to Application's application_hooks before composing any Lottie views.
/// Players share a serial execution queue but retain independent animation and playback state.
/// Non-Web builds use a dedicated worker thread; Web schedules work on the browser event loop without pthreads.
/// @param context Application installation context supplied by HuxerUI; it is not retained.
/// @throws std::logic_error If the service has already been installed in this application.
void Install(ApplicationContext& context);

/// Composes a vector Lottie player with centered, aspect-preserving fit and a transparent background.
///
/// Call during composition after Install. Supply layout constraints through the parent or a Frame modifier;
/// source dimensions do not automatically determine the view's layout size. Rasterization follows display density
/// with a maximum pixel-buffer dimension of 2048 on either axis.
///
/// Source identity follows LottieSource equality. Changing it clears the old picture and loads the replacement
/// asynchronously, starting at the beginning for LottiePlayback or at the requested LottieProgress. Recomposition with
/// the same source and controls preserves playback. Unmounting or replacing the source suppresses obsolete results
/// and events; an operation already in progress may finish before its resources are released.
///
/// Hidden or minimized windows and HuxerUI's reduced-motion policy suspend automatic time advancement without
/// catching up later. LottieProgress can still select frames under reduced motion; rendering while in the background is
/// deferred until the window returns to the foreground.
///
/// Bind LottieEvents keys with On<Event>(). Handlers run on the owning UI thread. Loading and rendering are queued
/// through the backend; ordinary frame publication does not require component recomposition.
///
/// Supports vector keyframe JSON. Expressions, external images, fonts, and .lottie archives are not supported;
/// unsupported features may be ignored by the decoder rather than reported as errors.
/// @param source Packaged resource, raw JSON asset, or local File. Keep its identity stable to avoid redundant reloads.
/// @param control LottiePlayback or LottieProgress request. The default plays once at normal speed.
/// @return A View whose mounted instance owns the player state and asynchronous result lifetime.
/// @throws std::invalid_argument If speed is non-finite or outside (0, 1000], or progress is non-finite.
/// @throws std::logic_error If Install is missing or the call has no valid HuxerUI composition context.
/// @code
/// Lottie(source, LottiePlayback{.iterations = 0})
///     .With(Frame{.width = 240.0F, .height = 240.0F});
/// @endcode
View Lottie(LottieSource source, LottieControl control = LottiePlayback{});

} // namespace huxerui::lottie
