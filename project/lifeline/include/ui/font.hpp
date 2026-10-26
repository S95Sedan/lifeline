#pragma once
#include "assets/asset_directory.hpp"
#include "rendering/image.hpp"
#include <array>
#include <map>

namespace lifeline {
class Font {
    AssetImage atlas_;
    std::map<unsigned,std::array<unsigned,2>> glyphs_;
public:
    explicit Font(const AssetDirectory&);
    void draw(AssetImage&,unsigned code,float x,float y,float size,
              const std::array<unsigned,4>& color,const std::array<int,4>& clip)const;
};
}
