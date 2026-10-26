#pragma once
#include "rendering/image.hpp"
#include <array>
#include <memory>

namespace lifeline {
struct StaticModel;
struct GraphicsQuality {
    unsigned texture_quality=2; // Quarter, half, or full source dimensions.
    unsigned shadow_quality=2; // Off, 256, 512, 1024, or 2048 pixels.
    unsigned anti_aliasing=0; // Off or FXAA.
    // Keep 0/1 compatible with existing Off/On settings; 2 is Low.
    unsigned reflections=1;
    unsigned visual_style=0; // Original, Faithful, Balanced, Modern, Enhanced.
    bool operator==(const GraphicsQuality&)const=default;
};
const GraphicsQuality& scene_graphics_quality();
unsigned shadow_resolution(unsigned quality);
// Entries verify all source bytes; model indices are lookup hints, not identity.
class SceneTextureCache {
    struct Entry {AssetImage source{};std::array<AssetImage,2> levels{};std::size_t bytes=0;std::uint64_t age=0;bool pinned=false;};
    std::vector<std::unique_ptr<Entry>> entries_;
    std::vector<std::size_t> hints_;
    std::size_t bytes_=0,budget_;
    std::uint64_t age_=0;
public:
    explicit SceneTextureCache(std::size_t budget=64*1024*1024):budget_(budget){}
    // Scratch copies start cold; prepared scene and image values stay independent.
    SceneTextureCache(const SceneTextureCache&);
    SceneTextureCache& operator=(const SceneTextureCache&);
    SceneTextureCache(SceneTextureCache&&)noexcept;
    SceneTextureCache& operator=(SceneTextureCache&&)noexcept;
    // Begin one synchronous submission. Returned images live until its end.
    void begin(std::size_t count);
    const AssetImage* reduced(const AssetImage&,unsigned index,unsigned quality);
    std::size_t bytes()const{return bytes_;}
};
// UI, effects and scene captures retain their authored texture resolution.
class SceneTextures {
    const StaticModel& model_;
    std::vector<AssetImage> reduced_;
    std::vector<const AssetImage*> cached_;
public:
    explicit SceneTextures(const StaticModel&,SceneTextureCache* cache=nullptr);
    const AssetImage& at(unsigned)const;
};
void apply_scene_anti_aliasing(AssetImage&);
// Source is the immutable pre-AA scene; image holds the finished AA pixels.
void apply_scene_visual_style(AssetImage& image,const AssetImage& source,std::span<const unsigned char> eligible={});
}
