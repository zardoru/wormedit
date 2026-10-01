#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
namespace wormedit {
struct atomic_file_save_result_t {
    bool published{};
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return published; }
};
// Exclusive same-directory temporary; complete write, file flush, close, atomic rename.
// Before successful publication, failures preserve the destination and remove only our temporary.
[[nodiscard]] auto save_file_atomic(const std::filesystem::path& path, std::string_view bytes,
                                    std::size_t maximum_bytes) -> atomic_file_save_result_t;
} // namespace wormedit
