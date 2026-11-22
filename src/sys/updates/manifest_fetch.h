#pragma once

#include <wormedit/core/result.h>

#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace wormedit::sys {

// Downloads a small text document over https. Seam for the update check: production uses
// libcurl, tests use a fake. Must return promptly once `stop` is requested.
using manifest_fetch_t =
    std::function<result_t<std::string, std::string>(std::string_view url, std::stop_token stop)>;

// True when this build links libcurl; otherwise the update check is compiled out.
[[nodiscard]] auto has_libcurl_fetch() -> bool;
// Follows no redirects off https, caps the body at `maximum_bytes`, 10 s total timeout.
// Sends only a generic User-Agent: no version, platform or identifier.
[[nodiscard]] auto curl_fetch_manifest(std::string_view url, std::stop_token stop, std::size_t maximum_bytes)
    -> result_t<std::string, std::string>;

} // namespace wormedit::sys
