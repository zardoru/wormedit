#include <wormedit/audio/metronome.h>
#include <wormedit/audio/decoder.h>
#include <cmath>
namespace wormedit::audio {
namespace {
struct click_parameters_t {
    double frequency;
    float gain;
    auto operator==(const click_parameters_t&) const -> bool = default;
};
auto make_metronome_click(double frequency, float gain) -> std::vector<float> {
    constexpr auto frames = output_sample_rate * 45U / 1'000U;
    constexpr auto pi = 3.14159265358979323846;
    std::vector<float> pcm(frames * output_channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto seconds = static_cast<double>(frame) / static_cast<double>(output_sample_rate);
        const auto envelope = std::exp(-90.0 * seconds);
        const auto sample = gain * static_cast<float>(envelope * std::sin(2.0 * pi * frequency * seconds));
        const auto first = frame * output_channel_count;
        pcm[first] = sample;
        pcm[first + 1] = sample;
    }
    return pcm;
}
}
auto make_metronome_clicks() -> metronome_clicks_t {
    constexpr std::array parameters{
        click_parameters_t{1'100.0, 0.16F}, click_parameters_t{1'500.0, 0.23F},
        click_parameters_t{1'900.0, 0.31F}};
    metronome_clicks_t clicks;
    for (std::size_t accent = 0; accent < clicks.size(); ++accent)
        clicks[accent] = make_metronome_click(parameters[accent].frequency, parameters[accent].gain);
    return clicks;
}
}
