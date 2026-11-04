#include <huxerui/lottie.h>
#include <huxerui/huxerui.h>

#include "backend.h"
#include "frame_presenter.h"
#include "timeline.h"

namespace huxerui::lottie {
namespace {

struct Player {
  class Extension;
  LottieSource source;
  detail::LoadSource load_source;
  LottieControl control;
  std::string resource_error;
  std::shared_ptr<detail::Backend> backend;
  std::shared_ptr<Resources> resources;
  TaskScope tasks;
  EventEmitter events;
  bool foreground;
};

class Player::Extension final : public NodeExtension {
public:
  Extension(ViewNode& node, const Player& player) : connection_(std::make_shared<Connection>()) {
    connection_->owner = this;
    Update(node, player);
  }

  ~Extension() override {
    connection_->owner = nullptr;
    Retire();
  }

  void Update(ViewNode&, const Player& player) {
    const bool source_changed = !player_ || player_->source != player.source;
    if (source_changed) {
      Retire();
      timeline_.Reset(player.control);
      pending_load_ = true;
      failed_ = false;
      loaded_ = false;
      completed_emitted_ = false;
      last_frame_.reset();
      ++revision_;
      InvalidatePaint();
    } else if (timeline_.Apply(player.control)) {
      ++revision_;
      if (!timeline_.Complete()) completed_emitted_ = false;
      skip_tick_ = true;
    }
    if (!player_ || player_->foreground != player.foreground) skip_tick_ = true;
    player_ = player;
  }

  FrameResult OnFrame(ViewNode&, const FrameInfo& frame) override {
    if (pending_load_) StartLoad();
    const bool running = loaded_ && !failed_ && player_->foreground && !frame.reduced_motion && timeline_.Running();
    if (running && !skip_tick_) timeline_.Advance(frame.delta_time, metadata_.duration);
    skip_tick_ = !running;
    if (player_->foreground) RequestRender();
    NotifyCompletion();
    return {.needs_frame = running};
  }

  PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
    const auto bounds = node.ContentBounds();
    const auto scale = player_->resources->Configuration().display_scale;
    const double width = std::max(0.0, static_cast<double>(bounds.width) * scale);
    const double height = std::max(0.0, static_cast<double>(bounds.height) * scale);
    const double reduction = std::max({1.0, width / 2048.0, height / 2048.0});
    const auto pixels_w = static_cast<std::uint32_t>(std::ceil(width / reduction));
    const auto pixels_h = static_cast<std::uint32_t>(std::ceil(height / reduction));
    const bool changed = bounds != bounds_ || pixels_w != width_ || pixels_h != height_;
    if (changed) {
      bounds_ = bounds;
      width_ = pixels_w;
      height_ = pixels_h;
      ++revision_;
      if (player_->foreground) RequestRender();
    }
    return changed ? PaintInvalidation::Foreground : PaintInvalidation::None;
  }

  void PaintAboveContent(const ViewNode&, PaintContext& context) const override {
    if (presenter_) context.DrawImage(presenter_->Texture(), bounds_);
  }

private:
  struct Connection {
    // Only UI callbacks access owner; worker jobs see the separate atomic cancellation flag.
    Extension* owner = nullptr;
  };

  void Retire() {
    if (alive_) *alive_ = false;
    if (id_ && player_) player_->backend->Release(id_);
    id_ = 0;
    busy_ = false;
    if (presenter_) presenter_->Finish();
    presenter_.reset();
  }

  void StartLoad() {
    pending_load_ = false;
    skip_tick_ = true;
    if (!player_->resource_error.empty()) {
      failed_ = true;
      player_->events.Emit<LottieEvents::Failed>(LottieError{LottieError::Stage::Resource, player_->resource_error});
      return;
    }
    id_ = player_->backend->Allocate();
    alive_ = std::make_shared<std::atomic_bool>(true);
    const std::weak_ptr connection = connection_;
    player_->backend->Load(id_, player_->load_source, alive_, player_->tasks,
        [connection, alive = alive_](detail::LoadResult result) {
          if (!alive->load()) return;
          if (const auto current = connection.lock(); current && current->owner) {
            current->owner->AcceptLoad(std::move(result));
          }
        });
  }

  void AcceptLoad(detail::LoadResult result) {
    if (result.failure) {
      failed_ = true;
      player_->events.Emit<LottieEvents::Failed>(*result.failure);
      return;
    }
    metadata_ = result.metadata;
    loaded_ = true;
    skip_tick_ = true;
    try {
      presenter_ = detail::CreateFramePresenter(metadata_.size);
    } catch (const std::exception& error) {
      failed_ = true;
      player_->events.Emit<LottieEvents::Failed>(LottieError{LottieError::Stage::Render, error.what()});
      return;
    }
    InvalidatePaint();
    RequestRender();
    player_->events.Emit<LottieEvents::Loaded>(metadata_);
  }

  void RequestRender() {
    if (!loaded_ || failed_ || busy_ || !width_ || !height_ || !player_->foreground) return;
    const detail::RenderRequest request{timeline_.Frame(metadata_.frames), width_, height_, revision_, presenter_->Alpha()};
    if (last_frame_ == request) return;
    busy_ = true;
    const std::weak_ptr connection = connection_;
    player_->backend->Render(id_, request, alive_, player_->tasks,
        [connection, alive = alive_](detail::RenderResult result) {
          if (!alive->load()) return;
          if (const auto current = connection.lock(); current && current->owner) {
            current->owner->AcceptRender(std::move(result));
          }
        });
  }

  void AcceptRender(detail::RenderResult result) {
    busy_ = false;
    if (result.request.revision == revision_) {
      if (result.failure) {
        failed_ = true;
        player_->events.Emit<LottieEvents::Failed>(*result.failure);
        return;
      }
      try {
        presenter_->Publish(result.pixels);
      } catch (const std::exception& error) {
        failed_ = true;
        player_->events.Emit<LottieEvents::Failed>(LottieError{LottieError::Stage::Render, error.what()});
        return;
      }
      last_frame_ = result.request;
      NotifyCompletion();
    }
    RequestRender();
  }

  void NotifyCompletion() {
    if (!completed_emitted_ && timeline_.Complete() && last_frame_ &&
        last_frame_->revision == revision_ && last_frame_->frame == metadata_.frames - 1) {
      completed_emitted_ = true;
      player_->events.Emit<LottieEvents::Completed>();
    }
  }

  std::optional<Player> player_;
  std::shared_ptr<Connection> connection_;
  std::shared_ptr<std::atomic_bool> alive_;
  std::unique_ptr<detail::FramePresenter> presenter_;
  detail::Timeline timeline_;
  LottieMetadata metadata_;
  huxerui::Rect bounds_;
  std::optional<detail::RenderRequest> last_frame_;
  std::uint64_t id_ = 0;
  std::uint64_t revision_ = 0;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool pending_load_ = false;
  bool loaded_ = false;
  bool failed_ = false;
  bool busy_ = false;
  bool skip_tick_ = true;
  bool completed_emitted_ = false;
};

} // namespace

void Install(ApplicationContext& context) {
  context.Provide(std::make_shared<detail::Backend>());
  detail::InstallFramePresentation(context);
}

[[huxerui::composable]] View Lottie(LottieSource source, LottieControl control) {
  auto backend = UseService<detail::Backend>();
  if (!backend) throw std::logic_error("Lottie requires lottie::Install in application_hooks");
  detail::LoadSource load_source;
  std::string resource_error;
  try {
    if (const auto* resource = std::get_if<RawResource>(&source)) {
      load_source = UseRawResource(*resource);
    } else if (const auto* asset = std::get_if<RawAsset>(&source)) {
      load_source = *asset;
    } else {
      load_source = std::get<File>(source);
    }
  } catch (const std::exception& error) {
    resource_error = error.what();
  }
  return Canvas([](PaintContext&, huxerui::Size) {}).With(Player{
      source, load_source, detail::Validate(control), resource_error, backend, UseService<Resources>(),
      UseTaskScope(), UseEvents(), UseWindow().LifecycleState() != WindowLifecycleState::Background});
}

} // namespace huxerui::lottie
