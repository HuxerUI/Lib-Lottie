#include <huxerui/huxerui.h>
#include <huxerui/lottie.h>
#include <app_resources.h>

using namespace huxerui;
namespace motion = huxerui::lottie;

View App() {
  auto playback = UseState(motion::LottiePlayback{.iterations = 0});
  auto progress = UseState(0.0F);
  auto seeking = UseState(false);
  auto mounted = UseState(true);
  auto bloom = UseState(false);
  auto status = UseState(std::string("正在加载"));
  const motion::LottiePlayback options = playback;
  const motion::LottieControl control =
      seeking ? motion::LottieControl(motion::LottieProgress{progress}) : options;
  return FlatTheme(Column {
    Text(bloom ? "Bloom · Lottie" : "Orbit · Lottie", TextRole::Title),
    Text(status),
    mounted ? motion::Lottie(bloom ? app::raw::bloom_json : app::raw::motion_json, control)
        .On<motion::LottieEvents::Loaded>([status](const motion::LottieMetadata& info) {
          status = std::to_string(info.frames) + " 帧 · " + std::to_string(info.duration) + " 秒";
        })
        .On<motion::LottieEvents::Completed>([status] { status = "播放完成，点击重播可重新开始"; })
        .On<motion::LottieEvents::Failed>([status](const motion::LottieError& failure) { status = failure.message; })
        .With(Frame{.width = 360.0F, .height = 360.0F}, Background(Color::Rgb(249, 248, 244)))
        .Key("player") : Text("播放器已卸载").With(Frame{.width = 360.0F, .height = 360.0F}),
    Row {
      Button(!seeking && options.playing ? "暂停" : "播放").OnClick([playback, seeking] {
        motion::LottiePlayback next = playback;
        next.playing = seeking || !next.playing;
        playback = next;
        seeking = false;
      }),
      Button("重播").OnClick([playback, seeking] {
        motion::LottiePlayback next = playback;
        ++next.restart;
        next.playing = true;
        playback = next;
        seeking = false;
      }),
      Button(options.iterations == 0 ? "循环：开" : "循环：关").OnClick([playback] {
        motion::LottiePlayback next = playback;
        next.iterations = next.iterations == 0 ? 1 : 0;
        playback = next;
      }),
    }.With(Spacing(8.0F)),
    Slider(progress).Range(0.0F, 1.0F).OnChanged([progress, seeking](float value) {
      progress = value;
      seeking = true;
    }).With(Frame{.width = 360.0F}),
    Text("拖动定位；播放从当前位置继续。滑块只控制进度，不显示自动播放进度。"),
    Row {
      Button("切换资源").OnClick([bloom, status] { bloom = !bloom; status = "正在加载"; }),
      Button(mounted ? "卸载" : "挂载").OnClick([mounted] { mounted = !mounted; }),
    }.With(Spacing(8.0F)),
  }.With(Padding(24.0F), Spacing(16.0F), CrossAlign(CrossAxisAlignment::Start)));
}

const Application application{
    App,
    {
        .window = {
            .title = "Lib-Lottie Preview",
            .initial_size = {680.0F, 680.0F},
        },
        .application_hooks = {motion::Install},
    }
};
