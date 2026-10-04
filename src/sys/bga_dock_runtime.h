#pragma once
#include "sys/bga_list_projection.h"
#include "ui/frontend.h"
#include <filesystem>
#include <memory>
namespace wormedit::sys {
// Owns the borrowed rows and thumbnail bytes through the frontend compose call.
class bga_dock_runtime_t {
public:
    bga_dock_runtime_t();
    ~bga_dock_runtime_t();
    void project(const song_document_t&, const std::filesystem::path&,
                 const WormUiBgaDockRequest&, WormUiComposeView&);
private:
    struct state_t;
    std::unique_ptr<state_t> state_;
};
}
