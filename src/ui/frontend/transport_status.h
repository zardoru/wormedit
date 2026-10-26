#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit::ui {

struct transport_position_view_t {
    std::int64_t row{};
    std::uint64_t measure{};
    std::int64_t beat{};
    auto operator==(const transport_position_view_t&) const -> bool = default;
};

struct transport_status_input_t {
    std::string_view status;
    std::string_view artist;
    std::string_view difficulty;
    std::size_t selected{};
    std::uint64_t seconds{};
    bool playing{};
    bool immediate{};
    std::uint32_t milliseconds{};
    std::optional<transport_position_view_t> position;
};

// Text alone refreshes at 12Hz during uninterrupted playback. Changes to its
// authored/user inputs invalidate immediately; drawing and transport positions
// remain at the display cadence.
class transport_status_text_t {
  public:
    auto refresh(const transport_status_input_t& input, std::uint64_t now_ns) -> bool {
        const bool changed = input.status != source_status_ || input.artist != source_artist_ ||
            input.difficulty != source_difficulty_ || input.selected != selected_;
        const bool seeked = input.seconds < seconds_ || input.seconds - seconds_ > 1;
        if (initialized_ && input.playing && playing_ && !input.immediate && !changed &&
            !seeked && now_ns >= refreshed_ns_ && now_ns - refreshed_ns_ < interval_ns)
            return false;
        source_status_ = input.status;
        source_artist_ = input.artist;
        source_difficulty_ = input.difficulty;
        selected_ = input.selected;
        seconds_ = input.seconds;
        playing_ = input.playing;
        refreshed_ns_ = now_ns;
        initialized_ = true;
        status_ = source_status_;
        if (selected_ != 0)
            status_ = std::to_string(selected_) +
                (selected_ == 1 ? " object selected · " : " objects selected · ") + status_;
        details_ = source_artist_ + " · " + source_difficulty_;
        char time[48]{};
        std::snprintf(time, sizeof(time), "%llu:%02llu.%03u",
            static_cast<unsigned long long>(seconds_ / 60),
            static_cast<unsigned long long>(seconds_ % 60), input.milliseconds);
        elapsed_ = time;
        row_ = input.position ? std::to_string(input.position->row) : "—";
        musical_position_ = input.position ? std::to_string(input.position->measure) + ":" +
            std::to_string(input.position->beat) : "—";
        ++refreshes_;
        return true;
    }
    [[nodiscard]] auto status() const noexcept -> const std::string& { return status_; }
    [[nodiscard]] auto details() const noexcept -> const std::string& { return details_; }
    [[nodiscard]] auto elapsed() const noexcept -> const std::string& { return elapsed_; }
    [[nodiscard]] auto row() const noexcept -> const std::string& { return row_; }
    [[nodiscard]] auto musical_position() const noexcept -> const std::string& { return musical_position_; }
    [[nodiscard]] auto refreshes() const noexcept -> std::uint64_t { return refreshes_; }

  private:
    static constexpr std::uint64_t interval_ns = 1'000'000'000 / 12;
    std::string source_status_, source_artist_, source_difficulty_;
    std::string status_, details_, elapsed_, row_, musical_position_;
    std::size_t selected_{};
    std::uint64_t seconds_{}, refreshed_ns_{}, refreshes_{};
    bool initialized_{}, playing_{};
};

} // namespace wormedit::ui
