#pragma once

// Windows has no iconv; the legacy BMS code pages are converted with the Win32 NLS API instead.
// Both directions report the byte offset of the first unconvertible sequence, like iconv does.

#if defined(_WIN32)

#include "text_codec_result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit::win32_codepage {

using convert_result_t = text_codec_result_t;

[[nodiscard]] auto code_page_for(text_encoding_t encoding) -> std::optional<unsigned>;
[[nodiscard]] auto to_utf8(unsigned code_page, std::string_view input) -> convert_result_t;
[[nodiscard]] auto from_utf8(unsigned code_page, std::string_view utf8) -> convert_result_t;

} // namespace wormedit::win32_codepage

#endif
