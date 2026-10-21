#pragma once

#include <wormedit/core/vocabulary.h>
#include <array>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

namespace wormedit::test {

inline auto explicit_fixture_encoding(const std::filesystem::path& path) -> std::optional<text_encoding_t> {
    static constexpr std::array choices{
        std::pair{std::string_view{"cp932-shift-jis.bms"}, text_encoding_t::cp932},
        std::pair{std::string_view{"export-lr2-encoding.bms"}, text_encoding_t::cp932},
        std::pair{std::string_view{"euc-kr.bms"}, text_encoding_t::euc_kr},
        std::pair{std::string_view{"cp949-extension.bms"}, text_encoding_t::cp949},
        std::pair{std::string_view{"windows-1252.bms"}, text_encoding_t::windows_1252}};
    const auto name = path.filename().string();
    for (const auto& [fixture, encoding] : choices)
        if (fixture == name) return encoding;
    return std::nullopt;
}
} // namespace wormedit::test
