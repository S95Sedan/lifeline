#pragma once
#include "core/bytes.hpp"
#include <span>

namespace lifeline {
// Indexed atlas carried by 64-pixel-wide 32-bit upload rows. Supported native
// importers validate the 256- or 512-pixel atlas width before using this mapping.
inline unsigned indexed_sheet_offset(unsigned x,unsigned y,unsigned width=512) {
    const unsigned block=((x>>4)&1)|(((y>>4)&1)<<1)|(((x>>5)&1)<<2)|(((y>>5)&1)<<3)|(((x>>6)&1)<<4);
    const unsigned column=(y>>2)&3;
    const unsigned word=((x&1)|((y&1)<<1)|((x&6)<<1))^((((y>>1)^(y>>2))&1)<<3);
    const unsigned page=(y/64)*(width/128)+x/128;
    const unsigned sx=((block&1)<<3)+((block&4)<<2)+((block&16)<<1)+(word&1)+((word&12)>>1);
    const unsigned sy=page*32+((block&2)<<2)+((block&8)<<1)+column*2+((word&2)>>1);
    return (sy*64+sx)*4+((y>>1)&1)+((x>>3)&1)*2;
}
struct AssetImage {
    std::string name;
    unsigned width, height;
    Bytes rgba;
};

// Authored screen coordinates remain 640x448 at every rendering resolution.
inline float frame_scale(const AssetImage& image) {
    const float scale=std::min(float(image.width)/640,float(image.height)/448);
    if(!image.width||!image.height||image.width>7680||image.height>7680||
       image.rgba.size()!=std::size_t(image.width)*image.height*4)
        throw std::runtime_error("Invalid scene frame dimensions");
    return scale;
}

inline AssetImage decode_cf_texture_record(std::span<const unsigned char> b, std::string name) {
    if (b.size() < 32 || le32(b.data()) != 1)
        throw std::runtime_error("Unsupported CF texture record");
    const auto format=le32(b.data()+20);
    if(format!=0 && format!=2) throw std::runtime_error("Unsupported CF texture format");
    const unsigned data_offset=format==0?32:1056, pixel_bytes=format==0?4:1;
    const auto stride=le16(b.data()+16), stored_height=le16(b.data()+18);
    const auto width=le16(b.data()+24), height=le16(b.data()+26);
    if (!width || !height || width>stride || height>stored_height || stride>4096 || stored_height>4096 ||
        data_offset+std::uint64_t(stride)*height*pixel_bytes != b.size() || le32(b.data()+8) != b.size())
        throw std::runtime_error("Invalid CF indexed texture extent");
    AssetImage out{std::move(name),width,height,Bytes(std::size_t(width)*height*4)};
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
        const unsigned index=format==2?b[1056+std::size_t(y)*stride+x]:0;
        const unsigned palette=(index&0xe7)|((index&8)<<1)|((index&16)>>1);
        const auto* color=format==0?b.data()+32+(std::size_t(y)*stride+x)*4:b.data()+32+palette*4;
        auto* pixel=out.rgba.data()+(std::size_t(y)*width+x)*4;
        std::copy_n(color,3,pixel); pixel[3]=static_cast<unsigned char>(std::min(unsigned(color[3])*2,255u));
    }
    return out;
}

// Decode the first indexed texture in the observed CF v1 texture-record layout.
// The remaining CF timeline/draw commands are not executed by this milestone.
inline AssetImage decode_cf_texture(std::span<const unsigned char> b, std::string name) {
    if (b.size() < 1104 || std::string(b.begin(), b.begin() + 4) != "  fc" || le32(b.data() + 4) != 0x10000)
        throw std::runtime_error("Unsupported CF header");
    if (le32(b.data() + 48) != 1 || le32(b.data() + 52) != 1 || le32(b.data() + 68) != 2)
        throw std::runtime_error("CF first record is not a supported indexed texture");
    const auto block_size = le32(b.data() + 56);
    const auto stride = le16(b.data() + 64);
    const auto storage_height = le16(b.data() + 66);
    const auto width = le16(b.data() + 72), height = le16(b.data() + 74);
    if (!width || !height || width > stride || height > storage_height || stride > 4096 || storage_height > 4096 ||
        32ULL + 1024 + std::uint64_t(stride) * height != block_size || 48ULL + block_size > b.size())
        throw std::runtime_error("CF texture dimensions or extent mismatch");
    AssetImage out{std::move(name), width, height, Bytes(std::size_t(width) * height * 4)};
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const unsigned index = b[1104 + std::size_t(y) * stride + x];
            // PS2 CSM1 palette ordering swaps index bits 3 and 4.
            const unsigned palette_index = (index & 0xe7) | ((index & 8) << 1) | ((index & 16) >> 1);
            const auto* color = b.data() + 80 + palette_index * 4;
            auto* pixel = out.rgba.data() + (std::size_t(y) * width + x) * 4;
            std::copy_n(color, 3, pixel);
            pixel[3] = static_cast<unsigned char>(std::min(unsigned(color[3]) * 2, 255u));
        }
    }
    return out;
}
}
