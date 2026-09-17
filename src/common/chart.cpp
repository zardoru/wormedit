//
// Created by Diego Ahumada on 29-06-26.
//

#include "Chart.h"

#include <algorithm>
#include <exception>
#include <memory>

#include <ChartGroup.h>
#include <note_loader.h>

namespace {
void StoreNote(Chart& chart, uint8_t lane, const otoworm::NoteData& note)
{
    if (lane >= MAX_CHANNELS) {
        return;
    }

    auto& row = chart.segment.notes[note.start];
    row[lane] = Note{
        static_cast<float>(note.end_time > note.start ? note.end_time - note.start : 0.0),
        static_cast<uint16_t>(std::min<uint32_t>(note.sound, UINT16_MAX)),
        0,
        255,
    };
}

ChartClass ChartClassFromTimingInfo(const otoworm::ChartTransient& data)
{
    if (!data.specialized_info) {
        return CC_WORM;
    }

    switch (data.specialized_info->get_class()) {
    case otoworm::CC_BMS:
        return CC_BMS;
    case otoworm::CC_OSUMANIA:
        return CC_OSUMANIA;
    case otoworm::CC_STEPMANIA:
        return CC_STEPMANIA;
    default:
        return CC_WORM;
    }
}
}

bool LoadChartFromFile(const std::filesystem::path& filename, Chart& out)
{
    std::shared_ptr<otoworm::ChartGroup> song;
    try {
        song = otoworm::load_song_from_file(filename);
    } catch (const std::exception&) {
        return false;
    }

    if (!song || song->charts.empty() || !song->charts.front() || !song->charts.front()->transient) {
        return false;
    }

    Chart converted = {};
    converted.title = song->title;
    converted.artist = song->artist;

    auto& diff = *song->charts.front();
    auto& data = *diff.transient;
    converted.channel_count = diff.channels;
    converted.timebase = TIME;
    converted.chart_class = ChartClassFromTimingInfo(data);

    for (const auto& bps : data.bps) {
        if (bps.value <= 0) {
            continue;
        }
        converted.segment.bpm[bps.time] = bps.value * 60;
    }

    for (const auto& warp : data.warps) {
        converted.segment.warps[warp.time] = warp.value;
    }

    for (const auto& stop : data.stops) {
        converted.segment.stops[stop.time] = stop.value;
    }

    for (const auto& speed : data.interpolated_speed_multipliers) {
        converted.segment.speeds[speed.time] = Speed{
            static_cast<float>(speed.duration),
            static_cast<float>(speed.value),
        };
    }

    for (const auto& measure : data.measures) {
        const auto lanes = std::min<size_t>(diff.channels, MAX_CHANNELS);
        for (size_t lane = 0; lane < lanes; ++lane) {
            for (const auto& note : measure.notes[lane]) {
                StoreNote(converted, static_cast<uint8_t>(lane), note);
            }
        }
    }

    out = std::move(converted);
    return true;
}
