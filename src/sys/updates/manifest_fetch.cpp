#include "sys/updates/manifest_fetch.h"

#if defined(WORMEDIT_HAS_LIBCURL)
#include <curl/curl.h>
#endif

namespace wormedit::sys {

#if defined(WORMEDIT_HAS_LIBCURL)
namespace {

struct download_t {
    std::string body;
    std::size_t maximum{};
    const std::stop_token* stop{};
};

auto write_body(char* data, std::size_t size, std::size_t count, void* user) -> std::size_t {
    auto& download = *static_cast<download_t*>(user);
    const auto bytes = size * count;
    if (download.body.size() + bytes > download.maximum) return 0; // Aborts the transfer.
    download.body.append(data, bytes);
    return bytes;
}

auto on_progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
    return static_cast<download_t*>(user)->stop->stop_requested() ? 1 : 0;
}

struct curl_handle_t {
    CURL* handle{curl_easy_init()};
    ~curl_handle_t() { if (handle) curl_easy_cleanup(handle); }
};

} // namespace

auto has_libcurl_fetch() -> bool { return true; }

auto curl_fetch_manifest(std::string_view url, std::stop_token stop, std::size_t maximum_bytes)
    -> result_t<std::string, std::string> {
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    curl_handle_t curl;
    if (!initialized || !curl.handle) return std::unexpected{"libcurl could not be initialized."};
    const std::string address{url};
    download_t download{.maximum = maximum_bytes, .stop = &stop};
    curl_easy_setopt(curl.handle, CURLOPT_URL, address.c_str());
    curl_easy_setopt(curl.handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl.handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl.handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_USERAGENT, "wormedit-update-check");
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(curl.handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.handle, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(curl.handle, CURLOPT_XFERINFODATA, &download);
    if (const auto code = curl_easy_perform(curl.handle); code != CURLE_OK)
        return std::unexpected{std::string{"Download failed: "} + curl_easy_strerror(code)};
    return std::move(download.body);
}
#else
auto has_libcurl_fetch() -> bool { return false; }

auto curl_fetch_manifest(std::string_view, std::stop_token, std::size_t) -> result_t<std::string, std::string> {
    return std::unexpected{"This build has no network support."};
}
#endif

} // namespace wormedit::sys
