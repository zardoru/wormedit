#include "sys/resources/image_import_probe.h"
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_decoder.h>
#endif
namespace wormedit::sys {
auto probe_resource_image(const std::filesystem::path& path, std::stop_token stop)
    -> result_t<std::pair<std::uint32_t, std::uint32_t>, error_t<failure_code_t>> {
    if (stop.stop_requested()) return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "Image import cancelled."});
    if (!path.is_absolute()) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Image import requires an absolute source."});
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    const auto image = visual::open_image_rgba({.path = path});
    if (stop.stop_requested()) return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "Image import cancelled."});
    if (image) return std::pair{image->width, image->height};
    return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "File is not a supported image."});
#else
    return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable, "Image decoding is unavailable in this build."});
#endif
}
}
