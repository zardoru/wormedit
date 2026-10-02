#include "win32_codepage.h"

#if defined(_WIN32)

#include <windows.h>

#include <algorithm>
#include <limits>
#include <string>

namespace wormedit::win32_codepage {
namespace {

auto fits_int(std::size_t size) -> bool {
    return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

auto multibyte_to_wide(unsigned code_page, std::string_view input) -> std::optional<std::wstring> {
    if (input.empty()) {
        return std::wstring{};
    }
    if (!fits_int(input.size())) {
        return std::nullopt;
    }
    const auto count = static_cast<int>(input.size());
    const auto needed =
        MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, input.data(), count, nullptr, 0);
    if (needed <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, input.data(), count, wide.data(),
                            needed) != needed) {
        return std::nullopt;
    }
    return wide;
}

auto wide_to_multibyte(unsigned code_page, std::wstring_view wide, bool& used_default)
    -> std::optional<std::string> {
    used_default = false;
    if (wide.empty()) {
        return std::string{};
    }
    if (!fits_int(wide.size())) {
        return std::nullopt;
    }
    const auto count = static_cast<int>(wide.size());
    const auto utf8 = code_page == CP_UTF8;
    const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL used = FALSE;
    BOOL* used_pointer = utf8 ? nullptr : &used;
    const auto needed = WideCharToMultiByte(code_page, flags, wide.data(), count, nullptr, 0,
                                            nullptr, used_pointer);
    if (needed <= 0) {
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(code_page, flags, wide.data(), count, bytes.data(), needed, nullptr,
                            used_pointer) != needed) {
        return std::nullopt;
    }
    used_default = used != FALSE;
    return bytes;
}

auto utf8_sequence_width(unsigned char lead) -> std::size_t {
    if (lead >= 0xf0U) {
        return 4;
    }
    if (lead >= 0xe0U) {
        return 3;
    }
    if (lead >= 0xc0U) {
        return 2;
    }
    return 1;
}

auto first_undecodable_offset(unsigned code_page, std::string_view input) -> std::size_t {
    std::size_t index = 0;
    while (index < input.size()) {
        const auto lead = static_cast<unsigned char>(input[index]);
        const std::size_t width = IsDBCSLeadByteEx(code_page, lead) != FALSE ? 2U : 1U;
        if (width > input.size() - index ||
            !multibyte_to_wide(code_page, input.substr(index, width)).has_value()) {
            return index;
        }
        index += width;
    }
    return input.size();
}

auto first_unencodable_offset(unsigned code_page, std::string_view utf8) -> std::size_t {
    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto width = std::min(utf8_sequence_width(static_cast<unsigned char>(utf8[index])),
                                    utf8.size() - index);
        const auto wide = multibyte_to_wide(CP_UTF8, utf8.substr(index, width));
        bool used_default = false;
        if (!wide.has_value() ||
            !wide_to_multibyte(code_page, *wide, used_default).has_value() || used_default) {
            return index;
        }
        index += width;
    }
    return utf8.size();
}

} // namespace

auto code_page_for(text_encoding_t encoding) -> std::optional<unsigned> {
    switch (encoding) {
    case text_encoding_t::cp932:
        return 932U;
    case text_encoding_t::cp949:
        return 949U;
    case text_encoding_t::euc_kr:
        // CP949 (UHC) is a superset of EUC-KR. Code page 51949 is not guaranteed to be installed
        // (Wine lacks it), and a missing table must not change which files can be opened.
        return 949U;
    case text_encoding_t::windows_1252:
        return 1252U;
    case text_encoding_t::unknown:
    case text_encoding_t::ascii:
    case text_encoding_t::utf8:
        return std::nullopt;
    }
    return std::nullopt;
}

auto to_utf8(unsigned code_page, std::string_view input) -> convert_result_t {
    const auto wide = multibyte_to_wide(code_page, input);
    if (!wide.has_value()) {
        return {.error_offset = first_undecodable_offset(code_page, input)};
    }
    bool used_default = false;
    auto utf8 = wide_to_multibyte(CP_UTF8, *wide, used_default);
    if (!utf8.has_value()) {
        return {.error_offset = input.size()};
    }
    return {.text = std::move(utf8)};
}

auto from_utf8(unsigned code_page, std::string_view utf8) -> convert_result_t {
    const auto wide = multibyte_to_wide(CP_UTF8, utf8);
    if (!wide.has_value()) {
        return {.error_offset = first_unencodable_offset(code_page, utf8)};
    }
    bool used_default = false;
    auto bytes = wide_to_multibyte(code_page, *wide, used_default);
    if (!bytes.has_value() || used_default) {
        return {.error_offset = first_unencodable_offset(code_page, utf8)};
    }
    return {.text = std::move(bytes)};
}

} // namespace wormedit::win32_codepage

#endif
