#pragma once
#include <wormedit/core/result.h>
#include <filesystem>
#include <cstdint>
#include <stop_token>
#include <utility>
namespace wormedit::sys {
[[nodiscard]] auto probe_resource_image(const std::filesystem::path&, std::stop_token = {})
    -> result_t<std::pair<std::uint32_t, std::uint32_t>, error_t<failure_code_t>>;
}
