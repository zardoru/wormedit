//
// Created by Diego Ahumada on 29-06-26.
//

#ifndef WORMEDIT_CHART_H
#define WORMEDIT_CHART_H
#include <cstdint>
#include <map>
#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>


// 4 oct + 2 special
constexpr size_t MAX_CHANNELS = 24 * 2 + 2;

// lines per beat 16 * 3 * 5
constexpr size_t DEFAULT_RESOLUTION = 240;

// time is given by the map, so we only need to store other info
struct Note {
    float duration;
    uint16_t wav;
    uint16_t layer;
    uint8_t volume;
};

struct Speed {
    float duration;
    float end_speed;
};

typedef double Timestamp;

enum Timebase {
    PULSE,
    TIME
};

enum ChartClass {
    CC_BMS,
    CC_STEPMANIA,
    CC_OSUMANIA,
    CC_WORM
};

struct SupportLevel {
    bool warps_supported;
    bool stops_supported;
    bool speeds_supported;
    bool scroll_supported;
    bool bpm_supported;
};

inline const std::unordered_map<ChartClass, SupportLevel> SUPPORT_LEVELS = {
    {CC_WORM, {true, true, true, true, true}},
    {CC_STEPMANIA, {true, true, true, true, true}},
    {CC_OSUMANIA, {false, false, false, true, true}},
    // targetting beatoraja
   {CC_BMS, {false, true, false, true, true}}
};

template <typename T>
using TimeEvent = std::map<Timestamp, T>;

class ChartSegment {
public:
    TimeEvent<std::array<Note, MAX_CHANNELS> > notes;
    TimeEvent<double> bpm;
    TimeEvent<double> warps;
    TimeEvent<double> stops;
    TimeEvent<Speed> speeds;
};

class Chart {
public:
    // ifdef conditionals
    ChartSegment segment;

    Timebase timebase;
    ChartClass chart_class;
    uint8_t channel_count;
    std::string title;
    std::string artist;
};

bool LoadChartFromFile(const std::filesystem::path& filename, Chart& out);


#endif //WORMEDIT_CHART_H
