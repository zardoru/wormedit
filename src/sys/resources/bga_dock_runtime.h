#pragma once
#include "sys/resources/bga_list_view.h"
#include "ui/frontend.h"
#include <filesystem>
#include <memory>
namespace wormedit::sys {
// Owns cached catalog queries and thumbnail bytes borrowed through the frontend call.
class bga_dock_runtime_t {
public:
    bga_dock_runtime_t();
    ~bga_dock_runtime_t();
    void project(const song_document_t&, const std::filesystem::path&,
                 const wormedit::ui::bga_dock_request_t&, wormedit::ui::compose_view_t&,
                 std::uint64_t session_generation = 0, const chart_document_t* chart = nullptr);
private:
    struct state_t;
    std::unique_ptr<state_t> state_;
};
}
