#pragma once
#include <wormedit/core/document.h>
#include <wormedit/core/diagnostic.h>
#include <unordered_map>
#include <utility>
#include <vector>
namespace wormedit {
inline auto rehome_measure_objects(chart_document_t& chart, diagnostic_list_t& diagnostics) -> bool {
    std::unordered_map<std::uint64_t, std::size_t> destinations;
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        destinations.emplace(chart.measures[index].id.value, index);
    }

    std::vector<std::vector<note_t>> notes(chart.measures.size());
    std::vector<std::vector<bgm_event_t>> bgm_events(chart.measures.size());
    bool valid = true;
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            const auto destination = destinations.find(note.position.measure.value);
            if (destination == destinations.end()) {
                diagnostics.push_back(error(diagnostic_code_t::unknown_measure,
                      "quantized note refers to an unknown measure", note.id));
                valid = false;
            } else {
                notes[destination->second].push_back(std::move(note));
            }
        }
        for (auto& bgm : measure.bgm_events) {
            const auto destination = destinations.find(bgm.position.measure.value);
            if (destination == destinations.end()) {
                diagnostics.push_back(error(diagnostic_code_t::unknown_measure,
                      "quantized BGM event refers to an unknown measure", bgm.id));
                valid = false;
            } else {
                bgm_events[destination->second].push_back(std::move(bgm));
            }
        }
    }
    if (!valid) {
        return false;
    }
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        chart.measures[index].notes = std::move(notes[index]);
        chart.measures[index].bgm_events = std::move(bgm_events[index]);
    }
    return true;
}

}
