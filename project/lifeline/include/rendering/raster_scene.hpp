#pragma once
#include "rendering/renderer.hpp"
#include <functional>

namespace lifeline {
// Clipped, ordered native scene geometry. This layout is also the GPU shader ABI.
struct RasterVertex {math::Vector4 screen,uv,color,shadow;};
struct RasterTriangle {
    RasterVertex a,b,c;
    std::array<unsigned,4> texture{},blend{};
    math::Vector4 fog{},parameters{};
};
static_assert(sizeof(RasterTriangle)==256);
struct RasterBatch {
    bool capture=false;
    std::vector<RasterTriangle> triangles;
    // Screen tiles: (offset,count) pairs followed by ordered triangle indices.
    std::vector<unsigned> tiles;
};
struct RasterScene {
    unsigned width=640,height=448,tile_columns=40,tile_rows=28;
    unsigned clear=0,shadow_offset=0;
    // RGBA words, then byte-packed shadow mask and float depth words.
    std::vector<unsigned> texels;
    std::vector<RasterBatch> batches;
    SceneTextureCache texture_cache;
};
RasterScene prepare_raster_scene(const SceneSubmission&,float scale=1,unsigned width=0,unsigned height=0);
// Optional destination is requested once after geometry/binning. Return exactly
// the requested writable words; keep submission/batches unchanged during it.
// The span is used only within this call; external packing leaves texels empty.
using RasterTexelDestination=std::function<std::span<unsigned>(std::size_t)>;
void prepare_raster_scene(RasterScene&,const SceneSubmission&,float scale=1,unsigned width=0,unsigned height=0,const RasterTexelDestination& destination={});
}
