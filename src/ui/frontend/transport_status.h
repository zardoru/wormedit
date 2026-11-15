#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit::ui {

// Source coordinates count every chart row; compose coordinates are the same position on the
// axis with export cuts collapsed. The beat within the measure is the same on both.
struct transport_position_view_t {
    std::int64_t source_row{};
    std::uint64_t source_measure{};
    std::int64_t beat{};
    // Set only when export cuts collapse part of the chart. The compose measure leaves out the
    // measures the cuts hide entirely.
    std::optional<std::int64_t> compose_row;
    std::optional<std::uint64_t> compose_measure;
    auto operator==(const transport_position_view_t&) const -> bool = default;
};

struct transport_clock_t {
    std::uint64_t seconds{};
    std::uint32_t milliseconds{};
    auto operator==(const transport_clock_t&) const -> bool = default;
};

struct transport_status_input_t {
    std::string_view status;
    std::string_view artist;
    std::string_view difficulty;
    std::size_t selected{};
    bool playing{};
    bool immediate{};
    // The playhead on the source clock and on the compose clock (export cuts removed). They are
    // the same without cuts; `clocks_differ` says when they are not.
    transport_clock_t source_clock;
    transport_clock_t compose_clock;
    bool clocks_differ{};
    // Which clock the readout shows; the tooltip shows the other.
    bool time_is_source{};
    std::optional<transport_position_view_t> position;
};

// Text alone refreshes at 12Hz during uninterrupted playback. Changes to its
// authored/user inputs invalidate immediately; drawing and transport positions
// remain at the display cadence.
class transport_status_text_t {
  public:
    auto refresh(const transport_status_input_t& input, std::uint64_t now_ns) -> bool {
        const auto& shown = input.time_is_source ? input.source_clock : input.compose_clock;
        const bool changed = input.status != input_status_ || input.artist != input_artist_ ||
            input.difficulty != input_difficulty_ || input.selected != selected_;
        const bool seeked = shown.seconds < seconds_ || shown.seconds - seconds_ > 1;
        if (initialized_ && input.playing && playing_ && !input.immediate && !changed &&
            !seeked && now_ns >= refreshed_ns_ && now_ns - refreshed_ns_ < interval_ns)
            return false;
        input_status_ = input.status;
        input_artist_ = input.artist;
        input_difficulty_ = input.difficulty;
        selected_ = input.selected;
        seconds_ = shown.seconds;
        playing_ = input.playing;
        refreshed_ns_ = now_ns;
        initialized_ = true;
        status_ = input_status_;
        if (selected_ != 0)
            status_ = std::to_string(selected_) +
                (selected_ == 1 ? " object selected · " : " objects selected · ") + status_;
        details_ = input_artist_ + " · " + input_difficulty_;
        refresh_clocks(input);
        refresh_position(input.position);
        ++refreshes_;
        return true;
    }
    [[nodiscard]] auto status() const noexcept -> const std::string& { return status_; }
    [[nodiscard]] auto details() const noexcept -> const std::string& { return details_; }
    // Both clocks are always formatted; the compose clock is empty when it equals the source one.
    [[nodiscard]] auto source_elapsed() const noexcept -> const std::string& { return source_elapsed_; }
    [[nodiscard]] auto compose_elapsed() const noexcept -> const std::string& { return compose_elapsed_; }
    [[nodiscard]] auto clocks_differ() const noexcept -> bool { return clocks_differ_; }
    // The clock the readout shows.
    [[nodiscard]] auto elapsed() const noexcept -> const std::string& {
        return time_is_source_ ? source_elapsed_ : compose_elapsed_;
    }
    // The other clock, empty when the clocks agree.
    [[nodiscard]] auto other_elapsed() const noexcept -> const std::string& {
        return !clocks_differ_ ? empty_ : time_is_source_ ? compose_elapsed_ : source_elapsed_;
    }
    [[nodiscard]] auto source_row() const noexcept -> const std::string& { return source_row_; }
    [[nodiscard]] auto compose_row() const noexcept -> const std::string& { return compose_row_; }
    [[nodiscard]] auto source_musical_position() const noexcept -> const std::string& {
        return source_musical_position_;
    }
    [[nodiscard]] auto compose_musical_position() const noexcept -> const std::string& {
        return compose_musical_position_;
    }
    [[nodiscard]] auto refreshes() const noexcept -> std::uint64_t { return refreshes_; }

  private:
    static auto format_clock(transport_clock_t clock) -> std::string {
        char text[48]{};
        std::snprintf(text, sizeof(text), "%llu:%02llu.%03u",
            static_cast<unsigned long long>(clock.seconds / 60),
            static_cast<unsigned long long>(clock.seconds % 60), clock.milliseconds);
        return text;
    }
    void refresh_clocks(const transport_status_input_t& input) {
        time_is_source_ = input.time_is_source;
        clocks_differ_ = input.clocks_differ;
        source_elapsed_ = format_clock(input.source_clock);
        compose_elapsed_ = format_clock(input.compose_clock);
    }
    void refresh_position(const std::optional<transport_position_view_t>& position) {
        const auto musical = [&](std::uint64_t measure) {
            return std::to_string(measure) + ":" + std::to_string(position->beat);
        };
        source_row_ = position ? std::to_string(position->source_row) : "—";
        source_musical_position_ = position ? musical(position->source_measure) : "—";
        compose_row_ = position && position->compose_row ? std::to_string(*position->compose_row) : std::string{};
        compose_musical_position_ = position && position->compose_measure
            ? musical(*position->compose_measure) : std::string{};
    }

    static constexpr std::uint64_t interval_ns = 1'000'000'000 / 12;
    inline static const std::string empty_{};
    std::string input_status_, input_artist_, input_difficulty_;
    std::string status_, details_, source_elapsed_, compose_elapsed_, source_row_, compose_row_,
        source_musical_position_, compose_musical_position_;
    std::size_t selected_{};
    std::uint64_t seconds_{}, refreshed_ns_{}, refreshes_{};
    bool initialized_{}, playing_{}, time_is_source_{}, clocks_differ_{};
};

} // namespace wormedit::ui
