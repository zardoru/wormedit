#pragma once

#include <cstdint>
#include <filesystem>

namespace wormedit {
struct document_history_t;

namespace platform {
void show_browser_manual();
void service_browser_assets(document_history_t& history, const std::filesystem::path& asset_base,
                            std::uint64_t session, bool loaded, bool busy);
} // namespace platform
} // namespace wormedit
