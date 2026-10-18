#pragma once

// Windows has no iconv; the legacy BMS code pages are converted with the Win32 NLS API instead.
// Both directions report the byte offset of the first unconvertible sequence, like iconv does.

#if defined(_WIN32)

#include "text_codec_result.h"

#include <wormedit/core/document.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit::win32_codepage {

[[nodiscard]] auto code_page_for(text_encoding_t encoding) -> std::optional<unsigned>;
[[nodiscard]] auto to_utf8(unsigned code_page, std::string_view input) -> text_codec_result_t;
[[nodiscard]] auto from_utf8(unsigned code_page, std::string_view utf8) -> text_codec_result_t;

} // namespace wormedit::win32_codepage

#endif
