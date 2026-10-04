#pragma once
#include <wormedit/core/document.h>
#include <cstddef>
#include <string>
#include <vector>
namespace wormedit::sys {
struct bga_list_row_t {
    resource_id_t resource;
    std::string slot, name;
    std::uint32_t use_count{};
    bool missing{}, video{};
};
struct bga_list_request_t {
    std::string search;
    std::size_t first_row{}, row_limit{64};
};
struct bga_list_t {
    std::vector<bga_list_row_t> rows;
    std::vector<resource_id_t> matching_ids;
    std::size_t total_matching{}, first_row{};
};
[[nodiscard]] auto project_bga_list(const song_document_t& document,
                                   const bga_list_request_t& request) -> bga_list_t;
}
