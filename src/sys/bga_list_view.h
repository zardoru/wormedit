#pragma once
#include <wormedit/core/document.h>
#include <wormedit/core/resource_assignment.h>
#include "sys/sound_list_view.h"
#include <cstddef>
#include <string>
#include <vector>
namespace wormedit::sys {
struct bga_list_row_t {
    resource_id_t resource;
    std::string slot, name;
    std::uint64_t use_count{};
    bool missing{}, video{};
    std::string source_path;
};
struct bga_list_request_t {
    std::string search;
    std::size_t first_row{}, row_limit{64};
    dock_sort_t sort{dock_sort_t::natural_name};
    bool ids_available{true};
    resource_use_filter_t use_filter{resource_use_filter_t::all};
};
struct bga_list_t {
    std::vector<bga_list_row_t> rows;
    std::vector<resource_id_t> matching_ids;
    std::vector<resource_id_t> all_resource_ids;
    // Placeable image/video resources in the same complete filtered/sorted order.
    std::vector<resource_id_t> advance_ids;
    std::size_t total_matching{}, first_row{};
    bms_identifier_capacity_t bms_capacity{};
};
[[nodiscard]] auto build_bga_list_view(const song_document_t& document,
                                   const bga_list_request_t& request) -> bga_list_t;
}
