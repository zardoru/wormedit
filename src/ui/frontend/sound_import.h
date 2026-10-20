#pragma once

#include <cstdint>

namespace wormedit::ui {
// Which section initiated an import; individual sounds use sound_kind_t.
enum class sound_import_kind_t : std::uint8_t {
    generic = 0,
    stem = 1,
    keysound = 2,
};
} // namespace wormedit::ui
