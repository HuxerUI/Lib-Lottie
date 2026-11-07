#include <huxerui/huxerui.h>
#include <huxerui/lottie.h>
#include <huxerui/testing/ui_test.h>
#include <fixtures_resources.h>
#include "backend.h"
#include "timeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>
#include <thread>

using namespace huxerui;
using namespace huxerui::testing;
using namespace std::chrono_literals;
namespace motion = huxerui::lottie;
namespace engine = huxerui::lottie::detail;

namespace {

std::function<View()> root;
UiWindow* host = nullptr;
void InstallHost(ApplicationContext& context) {
  context.RegisterPlatformModule<UiWindow*>("lottie/test-host", [](UiWindow& window) { return &window; });
}
void OpenHost(WindowContext&) { host = OpenPlatformModule<UiWindow*>("lottie/test-host"); }
View App() { return root(); }

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

RawAsset Asset(std::string_view text) {
  return RawAsset::CopyBytes(std::as_bytes(std::span(text.data(), text.size())), "application/json");
}

RawAsset Read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read fixture: " + path.string());
  return Asset(std::string(std::istreambuf_iterator<char>(file), {}));
}

void Await(UiTest& ui, const std::function<bool()>& ready,
           std::source_location location = std::source_location::current()) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!ready()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error("Worker timed out at line " + std::to_string(location.line()));
    }
    ui.Pump();
    std::this_thread::sleep_for(1ms);
  }
}

void Advance(UiTest& ui, int frames) {
  for (int i = 0; i < frames; ++i) {
    ui.Pump(16ms);
    std::this_thread::sleep_for(1ms);
  }
}

class Package final : public PlatformResources {
public:
  ResourceConfiguration Configuration() const override { return {}; }
  std::optional<InputStream> OpenRead(std::string_view path) override {
    if (path.find("raw/") != std::string_view::npos) {
      ++opens;
      worker_read = std::this_thread::get_id() != ui_thread;
      const auto deadline = std::chrono::steady_clock::now() + 2s;
      while (blocked && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    }
    const auto file = std::filesystem::path(TEST_PACKAGE_DIR) / path;
    if (!std::filesystem::exists(file)) return std::nullopt;
    return Read(file).OpenRead();
  }
  std::thread::id ui_thread = std::this_thread::get_id();
  std::atomic<int> opens = 0;
  std::atomic<bool> blocked = false;
  std::atomic<bool> worker_read = false;
};

void TimelineTest() {
  engine::Timeline clock;
  clock.Reset(motion::LottiePlayback{});
  clock.Advance(0.25, 1.0);
  Require(clock.Frame(60) == 15, "Playback timing");
  clock.Apply(motion::LottiePlayback{.playing = false});
  clock.Advance(100, 1.0);
  Require(clock.Frame(60) == 15, "Pause must preserve position");
  Require(!clock.Apply(motion::LottiePlayback{.playing = false}), "Recomposition must preserve state");
  clock.Apply(motion::LottiePlayback{});
  clock.Advance(0.75, 1.0);
  Require(clock.Complete() && clock.Frame(60) == 59 && !clock.Running(), "Finite completion endpoint");
  clock.Apply(motion::LottiePlayback{.playing = false});
  clock.Apply(motion::LottiePlayback{});
  Require(clock.Complete(), "Playing alone must not restart a completed run");
  clock.Apply(motion::LottiePlayback{.playing = false, .restart = 1});
  Require(!clock.Complete() && clock.Frame(60) == 0, "Paused restart");
  clock.Apply(motion::LottieProgress{0.75});
  clock.Apply(motion::LottiePlayback{.restart = 1});
  Require(clock.Frame(60) == 45, "Progress to playback must continue");
  clock.Apply(motion::LottieProgress{0.75});
  clock.Apply(motion::LottiePlayback{.restart = 2});
  Require(clock.Frame(60) == 0, "Replay from progress must reset");
  clock.Reset(motion::LottiePlayback{.iterations = 3, .speed = 2});
  clock.Advance(1.25, 1.0);
  Require(!clock.Complete() && clock.Frame(60) == 30, "Finite iteration count");
  clock.Advance(0.25, 1.0);
  Require(clock.Complete(), "Final iteration completion");
  clock.Reset(motion::LottiePlayback{.iterations = 0});
  clock.Advance(1000.5, 1.0);
  Require(!clock.Complete() && clock.Frame(60) == 30, "Infinite playback wraps");
  clock.Reset(engine::Validate(motion::LottieProgress{2}));
  Require(clock.Frame(60) == 59 && !clock.Running() && !clock.Complete(), "Progress endpoint clamping");
  clock.Apply(engine::Validate(motion::LottieProgress{-1}));
  Require(clock.Frame(60) == 0, "Negative progress clamping");
  for (auto value : {0.0, -1.0, 1001.0, std::numeric_limits<double>::infinity()}) {
    bool rejected = false;
    try { engine::Validate(motion::LottiePlayback{.speed = value}); }
    catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "Invalid playback speed accepted");
  }
  bool rejected = false;
  try { engine::Validate(motion::LottieProgress{std::numeric_limits<double>::quiet_NaN()}); }
  catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected, "NaN progress accepted");
}

void ComponentTest() {
  State<motion::LottieSource> source;
  State<motion::LottieControl> control;
  State<bool> mounted;
  State<int> unrelated;
  int loaded = 0;
  int completed = 0;
  std::vector<motion::LottieError> errors;
  auto package = std::make_shared<Package>();
  root = [&] {
    return Column {
      mounted ? motion::Lottie(source, control)
          .On<motion::LottieEvents::Loaded>([&](const motion::LottieMetadata& metadata) {
            Require(metadata.frames == 60 && metadata.duration == 1.0, "Loaded metadata");
            ++loaded;
          })
          .On<motion::LottieEvents::Completed>([&] { ++completed; })
          .On<motion::LottieEvents::Failed>([&](const motion::LottieError& failure) { errors.push_back(failure); })
          .With(Frame{.width = 200, .height = 120}).Key("player") : Text("Unmounted"),
      Text(std::to_string(unrelated)),
    };
  };
  Application app(App, {.application_hooks = {motion::Install, InstallHost, [&](ApplicationContext&) {
    source = State<motion::LottieSource>(fixtures::raw::motion_json);
    control = State<motion::LottieControl>(motion::LottiePlayback{.playing = false});
    mounted = State<bool>(true);
    unrelated = State<int>(0);
  }}, .window_hooks = {OpenHost}});
  UiTest ui(app, {.resource_provider = package});
  host->UpdateWindowLifecycleState(WindowLifecycleState::Active);
  ui.Pump();
  Await(ui, [&] { return loaded == 1 || !errors.empty(); });
  Require(errors.empty() && package->worker_read, "Packaged load must succeed off UI thread");
  Advance(ui, 80);
  Require(completed == 0, "Paused component must not finish");
  unrelated = 1;
  ui.Pump();
  Require(loaded == 1 && package->opens == 1, "Recomposition must not reload the resource");
  control = motion::LottiePlayback{};
  Advance(ui, 20);
  control = motion::LottiePlayback{.playing = false};
  Advance(ui, 100);
  Require(completed == 0, "Paused elapsed time must not advance");
  control = motion::LottiePlayback{};
  Advance(ui, 55);
  Await(ui, [&] { return completed == 1; });
  Advance(ui, 90);
  Require(completed == 1, "Completion must be emitted once");
  control = motion::LottiePlayback{.restart = 1};
  Advance(ui, 70);
  Await(ui, [&] { return completed == 2; });
  control = motion::LottieProgress{1.0};
  Advance(ui, 80);
  Require(completed == 2, "Controlled progress must not emit completion");
  control = motion::LottieProgress{0.75};
  ui.Pump();
  control = motion::LottiePlayback{.restart = 1};
  Advance(ui, 25);
  Await(ui, [&] { return completed == 3; });
  ui.UpdateResourceConfiguration({.display_scale = 2});
  ui.Pump();
  Require(loaded == 1 && errors.empty(), "Density change must not reload or fail");
  source = Asset("{invalid json");
  ui.Pump();
  Await(ui, [&] { return errors.size() == 1; });
  Require(errors.back().stage == motion::LottieError::Stage::Decode, "Decode error stage");
  source = RawResource("fixtures", "raw/missing.json");
  ui.Pump();
  Await(ui, [&] { return errors.size() == 2; });
  Require(errors.back().stage == motion::LottieError::Stage::Resource, "Missing resource error stage");
  source = RawAsset{};
  ui.Pump();
  Await(ui, [&] { return errors.size() == 3; });
  Require(errors.back().stage == motion::LottieError::Stage::Resource, "Empty resource error stage");
  control = motion::LottieProgress{0};
  source = Read(TEST_FIXTURE);
  ui.Pump();
  Await(ui, [&] { return loaded == 2; });
  mounted = false;
  ui.Pump();
  Require(!ui.Find(UiSelector::Key("player")).Exists(), "Player must unmount");
  mounted = true;
  ui.Pump();
  Await(ui, [&] { return loaded == 3; });
  Require(completed == 3 && errors.size() == 3, "Remount must have independent state");
}

void FileSourceTest() {
  struct ScratchDirectory {
    std::filesystem::path path;
    ~ScratchDirectory() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  };
  const auto prefix = "lib-lottie-files-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::path created;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto candidate = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(attempt));
    if (std::filesystem::create_directory(candidate)) {
      created = candidate;
      break;
    }
  }
  Require(!created.empty(), "Cannot create file test directory");
  const ScratchDirectory scratch{created};
  const File directory(scratch.path.u8string());
  const auto file = directory.Child("animation.json");
  const auto replacement = directory.Child("replacement.json");
  const auto json = Read(TEST_FIXTURE).ReadString();
  Require(file.WriteString(json) && replacement.WriteString(json), "Cannot write file fixtures");

  State<motion::LottieSource> source;
  State<motion::LottieControl> control;
  State<bool> mounted;
  State<int> unrelated;
  int loaded = 0;
  std::vector<motion::LottieError> errors;
  const auto ui_thread = std::this_thread::get_id();
  root = [&] {
    return Column {
      mounted ? motion::Lottie(source, control)
          .On<motion::LottieEvents::Loaded>([&](const motion::LottieMetadata& metadata) {
            Require(std::this_thread::get_id() == ui_thread, "File load event must run on UI thread");
            Require(metadata.frames == 60 && metadata.duration == 1.0, "File metadata");
            ++loaded;
          })
          .On<motion::LottieEvents::Failed>([&](const motion::LottieError& failure) {
            Require(std::this_thread::get_id() == ui_thread, "File error event must run on UI thread");
            errors.push_back(failure);
          })
          .With(Frame{.width = 100, .height = 100}).Key("file-player") : Text("Unmounted"),
      Text(std::to_string(unrelated)),
    };
  };
  Application app(App, {.application_hooks = {motion::Install, InstallHost, [&](ApplicationContext&) {
    source = State<motion::LottieSource>(file);
    control = State<motion::LottieControl>(motion::LottiePlayback{.playing = false});
    mounted = State<bool>(true);
    unrelated = State<int>(0);
  }}, .window_hooks = {OpenHost}});
  UiTest ui(app);
  host->UpdateWindowLifecycleState(WindowLifecycleState::Active);
  ui.Pump();
  Await(ui, [&] { return loaded == 1 || !errors.empty(); });
  Require(errors.empty(), "Local file must load");

  Require(file.WriteString("{invalid json"), "Cannot replace fixture contents");
  source = File(directory.Path() + "/./animation.json");
  control = motion::LottiePlayback{.playing = false, .restart = 1};
  unrelated = 1;
  Advance(ui, 10);
  Require(loaded == 1 && errors.empty(), "Equal paths, replay, and recomposition must reuse loaded contents");
  mounted = false;
  ui.Pump();
  mounted = true;
  ui.Pump();
  Await(ui, [&] { return errors.size() == 1; });
  Require(errors.back().stage == motion::LottieError::Stage::Decode, "Remount must decode current file contents");

  Require(file.WriteString(json), "Cannot restore fixture contents");
  source = file;
  unrelated = 2;
  Advance(ui, 10);
  Require(loaded == 1 && errors.size() == 1, "Equal paths must not retry a failed load");
  source = replacement;
  ui.Pump();
  Await(ui, [&] { return loaded == 2; });
  source = directory.Child("missing.json");
  ui.Pump();
  Await(ui, [&] { return errors.size() == 2; });
  Require(errors.back().stage == motion::LottieError::Stage::Resource, "Missing file error stage");
  source = directory;
  ui.Pump();
  Await(ui, [&] { return errors.size() == 3; });
  Require(errors.back().stage == motion::LottieError::Stage::Resource, "Directory source error stage");
  Require(file.WriteString(""), "Cannot empty fixture");
  source = file;
  ui.Pump();
  Await(ui, [&] { return errors.size() == 4; });
  Require(errors.back().stage == motion::LottieError::Stage::Decode, "Empty file error stage");
}

void CancellationTest() {
  State<motion::LottieSource> source;
  State<bool> mounted;
  State<int> handler;
  auto package = std::make_shared<Package>();
  package->blocked = true;
  int loaded = 0;
  int errors = 0;
  int handler_seen = -1;
  root = [&] {
    const int current_handler = handler;
    return mounted ? motion::Lottie(source, motion::LottieProgress{0.5})
        .On<motion::LottieEvents::Loaded>([&, current_handler](const motion::LottieMetadata&) {
          ++loaded;
          handler_seen = current_handler;
        })
        .On<motion::LottieEvents::Failed>([&](const motion::LottieError&) { ++errors; })
        .With(Frame{.width = 128, .height = 128}).Key("player") : Text("Unmounted");
  };
  Application app(App, {.application_hooks = {motion::Install, InstallHost, [&](ApplicationContext&) {
    source = State<motion::LottieSource>(fixtures::raw::motion_json);
    mounted = State<bool>(true);
    handler = State<int>(0);
  }}, .window_hooks = {OpenHost}});
  UiTest ui(app, {.resource_provider = package});
  host->UpdateWindowLifecycleState(WindowLifecycleState::Active);
  ui.Pump();
  Await(ui, [&] { return package->opens == 1; });
  source = File(TEST_FIXTURE);
  handler = 7;
  ui.Pump();
  package->blocked = false;
  Await(ui, [&] { return loaded == 1; });
  Advance(ui, 10);
  Require(loaded == 1 && errors == 0 && handler_seen == 7, "Stale source result or stale handler delivered");
  package->blocked = true;
  source = fixtures::raw::motion_json;
  ui.Pump();
  Await(ui, [&] { return package->opens == 2; });
  mounted = false;
  ui.Pump();
  package->blocked = false;
  Advance(ui, 10);
  Require(loaded == 1 && errors == 0, "Unmounted load must not emit events");
  mounted = true;
  ui.Pump();
  Await(ui, [&] { return loaded == 2; });
  package->blocked = true;
  mounted = false;
  ui.Pump();
  mounted = true;
  ui.Pump();
  Await(ui, [&] { return package->opens == 4; });
  source = File(std::string(TEST_FIXTURE) + ".missing");
  ui.Pump();
  source = Read(TEST_FIXTURE);
  ui.Pump();
  package->blocked = false;
  Await(ui, [&] { return loaded == 3; });
  Advance(ui, 10);
  Require(loaded == 3 && errors == 0, "Replaced file load must not emit a stale error");
}


std::vector<std::shared_ptr<ExternalTexture>> Textures() {
  std::vector<std::shared_ptr<ExternalTexture>> textures;
  std::function<void(const RenderNode*)> visit = [&](const RenderNode* node) {
    if (!node) return;
    for (const auto* sequence : {&node->content, &node->foreground}) {
      for (const auto& command : sequence->Commands()) {
        if (const auto* image = std::get_if<DrawExternalTextureCommand>(&command)) textures.push_back(image->texture);
      }
    }
    for (const auto* child : node->children) visit(child);
  };
  visit(host->BuildFrame().render_frame.scene.root);
  return textures;
}

void PresentationTest() {
  State<motion::LottieControl> control;
  State<bool> reduced;
  const auto asset = Read(TEST_FIXTURE);
  int loaded = 0;
  int errors = 0;
  root = [&] {
    auto theme = FlatLightThemeSpec();
    theme.motion.reduced_motion = reduced;
    return FlatTheme(theme, Row {
      motion::Lottie(asset, control)
          .On<motion::LottieEvents::Loaded>([&](const motion::LottieMetadata&) { ++loaded; })
          .On<motion::LottieEvents::Failed>([&](const motion::LottieError&) { ++errors; })
          .With(Frame{.width = 120, .height = 120}).Key("playing"),
      motion::Lottie(asset, motion::LottieProgress{0.5})
          .On<motion::LottieEvents::Loaded>([&](const motion::LottieMetadata&) { ++loaded; })
          .With(Frame{.width = 120, .height = 120}).Key("still"),
    });
  };
  Application app(App, {.application_hooks = {motion::Install, InstallHost, [&](ApplicationContext&) {
    control = State<motion::LottieControl>(motion::LottiePlayback{.iterations = 0});
    reduced = State<bool>(false);
  }}, .window_hooks = {OpenHost}});
  UiTest ui(app);
  host->UpdateWindowLifecycleState(WindowLifecycleState::Active);
  ui.Pump();
  Await(ui, [&] { return loaded == 2; });
  Advance(ui, 10);
  auto textures = Textures();
  Require(textures.size() == 2 && textures[0] != textures[1], "Players must own independent textures");
  Require(textures[0]->Revision() > 0 && textures[1]->Revision() > 0, "Frames must be published");
  auto first_revision = textures[0]->Revision();
  const auto still_revision = textures[1]->Revision();
  Advance(ui, 10);
  Require(textures[0]->Revision() > first_revision && textures[1]->Revision() == still_revision,
          "Only the playing instance should produce frames");
  host->UpdateWindowLifecycleState(WindowLifecycleState::Background);
  Advance(ui, 5);
  first_revision = textures[0]->Revision();
  Advance(ui, 80);
  Require(textures[0]->Revision() == first_revision, "Background playback must stop producing frames");
  host->UpdateWindowLifecycleState(WindowLifecycleState::Active);
  Advance(ui, 10);
  Require(textures[0]->Revision() > first_revision, "Foreground playback must resume");
  reduced = true;
  Advance(ui, 5);
  first_revision = textures[0]->Revision();
  Advance(ui, 80);
  Require(textures[0]->Revision() == first_revision, "Reduced motion must freeze automatic playback");
  control = motion::LottieProgress{0.8};
  ui.Pump();
  Await(ui, [&] { return textures[0]->Revision() > first_revision; });
  first_revision = textures[0]->Revision();
  Advance(ui, 10);
  Require(textures[0]->Revision() == first_revision, "Controlled progress must render once and settle");
  ui.UpdateResourceConfiguration({.display_scale = 2});
  ui.Pump();
  Await(ui, [&] { return textures[0]->Revision() > first_revision && textures[1]->Revision() > still_revision; });
  Require(loaded == 2 && errors == 0, "Retina transition must redraw both players without reloading");
  control = motion::LottiePlayback{.iterations = 0, .restart = 1};
  reduced = false;
  Advance(ui, 5);
}

std::uint64_t Hash(const engine::PixelBuffer& buffer) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto word : buffer->Pixels()) {
    for (int shift : {0, 8, 16, 24}) hash = (hash ^ ((word >> shift) & 255)) * 1099511628211ULL;
  }
  return hash;
}

std::uint32_t Pixel(const engine::PixelBuffer& buffer, int x, int y) {
  const auto rgba = buffer->Pixels()[std::size_t(y) * buffer->width + x];
  return (rgba & 0xff00ff00) | ((rgba & 255) << 16) | ((rgba >> 16) & 255);
}

void RasterTest() {
  std::shared_ptr<engine::Backend> backend;
  TaskScope tasks;
  root = [&] {
    backend = UseService<engine::Backend>();
    tasks = UseApplicationTaskScope();
    return Text("Raster test");
  };
  Application app(App, {.application_hooks = {motion::Install, InstallHost}, .window_hooks = {OpenHost}});
  UiTest ui(app);
  const auto alive = std::make_shared<std::atomic_bool>(true);
  const auto id = backend->Allocate();
  std::optional<engine::LoadResult> loaded;
  const auto ui_thread = std::this_thread::get_id();
  backend->Load(id, File(TEST_FIXTURE), alive, tasks, [&](auto result) {
    Require(std::this_thread::get_id() == ui_thread, "Load callback must run on UI thread");
    loaded = result;
  });
  Await(ui, [&] { return loaded.has_value(); });
  Require(!loaded->failure && loaded->metadata.frames == 60, "Raster fixture load");
  auto render = [&](std::uint32_t frame, std::uint32_t width = 100, std::uint32_t height = 100,
                    engine::PixelAlpha alpha = engine::PixelAlpha::Straight) {
    std::optional<engine::RenderResult> output;
    backend->Render(id, {frame, width, height, 0, alpha}, alive, tasks, [&](auto result) {
      Require(std::this_thread::get_id() == ui_thread, "Render callback must run on UI thread");
      output = result;
    });
    Await(ui, [&] { return output.has_value(); });
    Require(!output->failure, "Raster render failed");
    Require(output->pixels->alpha == alpha, "Raster alpha representation mismatch");
    return output->pixels;
  };
  const auto first = render(0);
  const auto first_hash = Hash(first);
  Require(Pixel(first, 20, 50) == 0xffff0000 && Pixel(first, 50, 50) == 0, "RGBA colors or transparency incorrect");
  const auto middle = render(30);
  Require(Pixel(middle, 50, 50) == 0xffff0000 && Pixel(middle, 20, 50) == 0, "Seek did not update pixels");
  const auto last = render(59);
  Require(Pixel(last, 79, 50) == 0xffff0000, "Last frame incorrect");
  Require(Hash(render(0)) == first_hash && Hash(first) == first_hash, "Backward seek or immutable frame ownership failed");
  const auto wide = render(0, 200, 100);
  Require(Pixel(wide, 70, 50) == 0xffff0000 && Pixel(wide, 20, 50) == 0, "Aspect-preserving centered fit failed");
  const auto retina = render(0, 200, 200);
  Require(Pixel(retina, 40, 100) == 0xffff0000, "Retina raster size failed");
  Require(Hash(render(0)) == first_hash, "Resize must not accumulate transforms");
  auto translucent = Read(TEST_FIXTURE).ReadString();
  const auto color = translucent.find("[1,0,0,1]");
  Require(color != std::string::npos, "Missing fixture fill color");
  translucent.replace(color, 9, "[1,0.5,0.25,1]");
  const std::string opacity = "\"o\":{\"a\":0,\"k\":100},\"r\":1,\"nm\":\"Red\"";
  const auto fill = translucent.find(opacity);
  Require(fill != std::string::npos, "Missing fixture fill opacity");
  translucent.replace(fill, opacity.size(), "\"o\":{\"a\":0,\"k\":50},\"r\":1,\"nm\":\"Red\"");
  loaded.reset();
  backend->Load(id, Asset(translucent), alive, tasks, [&](auto result) { loaded = result; });
  Await(ui, [&] { return loaded.has_value(); });
  Require(!loaded->failure, "Translucent fixture load");
  const auto rgba = Pixel(render(0), 20, 50);
  Require((rgba >> 24) >= 127 && (rgba >> 24) <= 128 && ((rgba >> 16) & 255) >= 253 &&
          ((rgba >> 8) & 255) >= 126 && ((rgba >> 8) & 255) <= 129 && (rgba & 255) >= 62 && (rgba & 255) <= 65,
          "CPU frames must preserve straight-alpha colors");
  const auto premultiplied = render(0, 100, 100, engine::PixelAlpha::Premultiplied);
  const auto premultiplied_rgba = Pixel(premultiplied, 20, 50);
  Require((premultiplied_rgba >> 24) >= 127 && (premultiplied_rgba >> 24) <= 128 &&
          ((premultiplied_rgba >> 16) & 255) >= 126 && ((premultiplied_rgba >> 16) & 255) <= 128 &&
          ((premultiplied_rgba >> 8) & 255) >= 63 && ((premultiplied_rgba >> 8) & 255) <= 65 &&
          (premultiplied_rgba & 255) >= 31 && (premultiplied_rgba & 255) <= 33,
          "Premultiplied output must retain alpha and multiply each color channel exactly once");
  Require(Pixel(premultiplied, 50, 50) == 0, "Premultiplied output must preserve transparent pixels");
  Require(Pixel(render(0), 20, 50) == rgba, "Switching back to straight alpha must restore original colors");
  const std::string half_opacity = "\"o\":{\"a\":0,\"k\":50},\"r\":1,\"nm\":\"Red\"";
  translucent.replace(translucent.find(half_opacity), half_opacity.size(),
      "\"o\":{\"a\":0,\"k\":0},\"r\":1,\"nm\":\"Red\"");
  loaded.reset();
  backend->Load(id, Asset(translucent), alive, tasks, [&](auto result) { loaded = result; });
  Await(ui, [&] { return loaded.has_value(); });
  Require(!loaded->failure, "Transparent fixture load");
  for (const auto alpha : {engine::PixelAlpha::Straight, engine::PixelAlpha::Premultiplied}) {
    const auto empty = render(0, 200, 100, alpha);
    Require(std::ranges::all_of(empty->Pixels(), [](auto pixel) { return pixel == 0; }),
        "Drawing must initialize every pixel, including the transparent border");
  }
  *alive = false;
  backend->Release(id);
  backend.reset();
}

} // namespace

int main() {
  try {
    TimelineTest();
    std::cout << "PASS timeline: pause, resume, replay, loops, speed, controlled progress\n";
    ComponentTest();
    std::cout << "PASS component: packaged/memory resources, completion, errors, density, remount\n";
    FileSourceTest();
    std::cout << "PASS files: local loading, path identity, replay, remount, replacement, errors\n";
    CancellationTest();
    std::cout << "PASS lifecycle: resource replacement, stale callbacks, unmount during loading\n";
    PresentationTest();
    std::cout << "PASS presentation: independent players, publication, background, reduced motion, density\n";
    RasterTest();
    std::cout << "PASS pixels: RGBA, transparency, seek, immutable buffers, fit, Retina, resize\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
