#include "sys/bga_dock_runtime.h"
#include "sys/thread_priority.h"
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#include <wormedit/visual/visual_asset.h>
#endif
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
namespace wormedit::sys {
namespace {
struct thumbnail_t {
    asset_id_t asset;
    std::filesystem::path path;
    std::vector<std::uint8_t> pixels;
    std::uint32_t width{}, height{};
    std::uint64_t content{};
};
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
auto make_thumbnail(const visual::rgba_image_t& source, std::uint64_t content) -> thumbnail_t {
    const auto scale = 32.0 / std::max(source.width, source.height);
    thumbnail_t result{source.asset, source.path, {},
        std::max(1U, static_cast<unsigned>(source.width * scale)),
        std::max(1U, static_cast<unsigned>(source.height * scale)), content};
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    for (std::uint32_t y = 0; y < result.height; ++y)
        for (std::uint32_t x = 0; x < result.width; ++x) {
            const auto from = (static_cast<std::size_t>(y) * source.height / result.height * source.width +
                static_cast<std::size_t>(x) * source.width / result.width) * 4;
            const auto to = (static_cast<std::size_t>(y) * result.width + x) * 4;
            std::copy_n(source.pixels.data() + from, 4, result.pixels.data() + to);
        }
    return result;
}
#endif
}
struct bga_dock_runtime_t::state_t {
    bga_list_t list;
    std::vector<WormUiComposeBgaRowView> rows;
    std::vector<std::uint64_t> ids;
    std::string search;
    song_id_t project;
    std::uint64_t revision{}, next_content{1};
    std::unordered_map<std::uint64_t, thumbnail_t> thumbnails;
    std::unordered_set<std::uint64_t> failed;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_cache_t cache{128U * 1024U * 1024U, 32};
    task_scheduler_t scheduler{1, 2, background_scheduler_options()};
    visual::image_job_t job{cache, scheduler, 4};
#endif
    void update_images(const song_document_t&, const std::filesystem::path&);
};
void bga_dock_runtime_t::state_t::update_images(const song_document_t& document,
                                              const std::filesystem::path& base) {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    if (auto completion = job.poll(); completion && *completion &&
        completion->project == document.id && completion->document_revision == document.revision) {
        for (const auto& item : completion->result->items) {
            if (item) {
                if (thumbnails.size() >= 512) thumbnails.erase(thumbnails.begin());
                thumbnails[item.source.resource.value] = make_thumbnail(*item.image, next_content++);
            }
            else failed.insert(item.source.resource.value);
        }
    }
    std::vector<visual::resolved_visual_asset_t> request;
    for (const auto& row : list.rows) {
        if (row.video || row.missing || failed.contains(row.resource.value)) continue;
        const auto source = visual::resolve_visual_asset(document, row.resource, base);
        if (!source) { failed.insert(row.resource.value); continue; }
        const auto cached = thumbnails.find(row.resource.value);
        if (cached != thumbnails.end() && cached->second.asset == source.source->asset &&
            cached->second.path == source.source->path) continue;
        thumbnails.erase(row.resource.value);
        request.push_back(*source.source);
        if (request.size() == 4) break;
    }
    if (!job.busy() && !request.empty()) (void)job.request(document.id, document.revision, request);
#else
    (void)document; (void)base;
#endif
}
bga_dock_runtime_t::bga_dock_runtime_t() : state_(std::make_unique<state_t>()) {}
bga_dock_runtime_t::~bga_dock_runtime_t() = default;
void bga_dock_runtime_t::project(const song_document_t& document, const std::filesystem::path& base,
    const WormUiBgaDockRequest& request, WormUiComposeView& view) {
    auto& state = *state_;
    if (state.project != document.id) {
        state.thumbnails.clear(); state.failed.clear(); state.project = document.id;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        state.job.cancel(); state.cache.clear();
#endif
    }
    if (state.revision != document.revision) { state.failed.clear(); state.revision = document.revision; }
    if (!request.visible) return;
    state.search.assign(request.search, std::find(request.search, request.search + sizeof(request.search), '\0'));
    state.list = project_bga_list(document, {state.search, request.first_row,
        std::min<std::size_t>(64, request.row_limit),
        request.sort_by_id ? dock_sort_t::bms_id : dock_sort_t::natural_name,
        request.ids_available});
    state.update_images(document, base);
    state.ids.clear(); state.rows.clear();
    for (const auto id : state.list.matching_ids) state.ids.push_back(id.value);
    for (const auto& source : state.list.rows) {
        WormUiComposeBgaRowView row{.resource_id = source.resource.value,
            .slot = source.slot.c_str(), .name = source.name.c_str(), .use_count = source.use_count,
            .missing = source.missing, .video = source.video};
        const auto image = state.thumbnails.find(source.resource.value);
        if (image != state.thumbnails.end()) {
            row.image_content_id = image->second.content;
            row.image_width = image->second.width; row.image_height = image->second.height;
            row.rgba_pixels = image->second.pixels.data(); row.rgba_byte_count = image->second.pixels.size();
        }
        state.rows.push_back(row);
    }
    view.bga_search = state.search.c_str();
    view.bga_rows = state.rows.data(); view.bga_row_count = state.rows.size();
    view.bga_first_row = state.list.first_row; view.bga_total_matching = state.list.total_matching;
    view.bga_matching_ids = state.ids.data();
    view.bga_ids_available = request.ids_available;
    view.bga_ids_used = state.list.bms_capacity.used;
    view.bga_ids_total = state.list.bms_capacity.total;
    view.bga_largest_free_run = state.list.bms_capacity.largest_free_run;
}
}
