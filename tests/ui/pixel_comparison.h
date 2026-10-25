#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstdlib>

namespace wormedit::ui::testing {
using field_pixels_t = std::vector<std::array<std::uint8_t,4>>;
inline auto has_field_pixel_edge(const field_pixels_t& pixels,int x,int y,int width,int height) -> bool {
    const auto center=pixels[static_cast<std::size_t>(y*width+x)];
    for (int row=std::max(0,y-1); row<=std::min(height-1,y+1); ++row)
        for (int column=std::max(0,x-1); column<=std::min(width-1,x+1); ++column)
            if (pixels[static_cast<std::size_t>(row*width+column)]!=center) return true;
    return false;
}
inline auto is_field_color_different(const std::array<std::uint8_t,4>& left,
                         const std::array<std::uint8_t,4>& right) -> bool {
    return std::abs(int(left[0])-int(right[0]))>1 ||
        std::abs(int(left[1])-int(right[1]))>1 || std::abs(int(left[2])-int(right[2]))>1;
}
using field_boundaries_t = std::vector<bool>;
inline auto field_boundaries(const field_pixels_t& pixels,int width,int height) -> field_boundaries_t {
    field_boundaries_t boundaries(static_cast<std::size_t>(width*height*4));
    for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
        const auto& color=pixels[static_cast<std::size_t>(y*width+x)];
        if (x<width-1 && is_field_color_different(color,pixels[static_cast<std::size_t>(y*width+x+1)]))
            boundaries[static_cast<std::size_t>((2*y)*(2*width)+2*x+1)]=true;
        if (y<height-1 && is_field_color_different(color,pixels[static_cast<std::size_t>((y+1)*width+x)]))
            boundaries[static_cast<std::size_t>((2*y+1)*(2*width)+2*x)]=true;
    }
    return boundaries;
}
inline auto has_near_field_boundary(const field_boundaries_t& boundaries,int x,int y,int width,int height,int radius) -> bool {
    for (int row=std::max(0,y-radius); row<=std::min(2*height-1,y+radius); ++row)
        for (int column=std::max(0,x-radius); column<=std::min(2*width-1,x+radius); ++column)
            if (boundaries[static_cast<std::size_t>(row*(2*width)+column)]) return true;
    return false;
}
inline auto field_boundary_difference(const field_boundaries_t& left,const field_boundaries_t& right,int width,int height,int radius)
    -> std::size_t {
    std::size_t differences{};
    for (int y=0; y<2*height; ++y) for (int x=0; x<2*width; ++x)
        if (left[static_cast<std::size_t>(y*(2*width)+x)] && !has_near_field_boundary(right,x,y,width,height,radius)) ++differences;
    return differences;
}
struct static_pixel_difference_t { std::size_t interior{}, coverage{}; };
inline auto static_pixel_difference(const field_pixels_t& direct,const field_pixels_t& cached,int width=128,int height=128,float scale=1)
    -> static_pixel_difference_t {
    static_pixel_difference_t difference;
    for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
        const auto index=static_cast<std::size_t>(y*width+x);
        if (is_field_color_different(direct[index],cached[index]) &&
            !has_field_pixel_edge(direct,x,y,width,height) && !has_field_pixel_edge(cached,x,y,width,height)) ++difference.interior;
    }
    // Boundary positions lie halfway between pixel centers. The doubled grid
    // measures one logical pixel without accidentally accepting a two-pixel shift.
    const auto direct_edges=field_boundaries(direct,width,height), cached_edges=field_boundaries(cached,width,height);
    difference.coverage=field_boundary_difference(direct_edges,cached_edges,width,height,static_cast<int>(2*scale))+
        field_boundary_difference(cached_edges,direct_edges,width,height,static_cast<int>(2*scale));
    return difference;
}
}
