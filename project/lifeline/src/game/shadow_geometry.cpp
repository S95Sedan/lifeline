#include "game/shadow_geometry.hpp"
#include <bit>
#include <stdexcept>
namespace lifeline {
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
std::optional<ShadowGeometry> decode_shadow_geometry(std::span<const unsigned char> bytes){
    const auto word=[&](std::size_t at){require(at<=bytes.size()&&bytes.size()-at>=4,"Shadow record outside model");return le32(bytes.data()+at);};
    const auto number=[&](std::size_t at){const float v=std::bit_cast<float>(word(at));require(std::isfinite(v)&&std::abs(v)<1e7f,"Invalid shadow coordinate");return v;};
    const auto vector=[&](std::size_t at,float w){return math::Vector4{number(at),number(at+4),number(at+8),w};};
    require(bytes.size()>=64,"Truncated model shadow header");
    const std::size_t upload=word(28),base=upload+(std::size_t(word(upload)&65535)+1)*16;
    if(base>bytes.size()||bytes.size()-base<10||std::string(bytes.begin()+base,bytes.begin()+base+4)!="KAGE")return {};
    require(std::string(bytes.begin()+base,bytes.begin()+base+10)=="KAGE031500","Unsupported shadow geometry version");
    const auto count=word(base+32)&65535;const std::size_t size=word(base+28);
    require(count>0&&count<=(word(32)&65535)&&size>=64+count*256&&size<=bytes.size()-base,"Invalid shadow section bounds");
    const auto end_section=base+size;ShadowGeometry result;
    for(unsigned i=0;i<count;++i){
        const auto record=base+64+i*256;ShadowGeometry::Bone bone;bone.parent=std::bit_cast<int>(word(record));
        for(unsigned j=0;j<4;++j)bone.inverse_bind.columns[j]=vector(record+64+j*16,j==3?1.f:0.f);
        result.bones.push_back(bone);
    }
    const auto offset=word(base+16);if(!offset)return result;
    require(offset>=64+count*256&&offset%16==0,"Invalid shadow packet offset");std::size_t p=base+offset;
    while(p+4<=end_section&&word(p)!=0xffffffff){
        const auto tag=word(p);const auto end=p+16+std::size_t(tag&65535)*16;
        require((tag&0xffff0000)==0x30000000&&end<=end_section&&end>p+16,"Invalid shadow packet");p+=12;
        while(p<end){
            const auto command=word(p);p+=4;if(command>>16==0)continue;
            require((command&0xff00ffff)==0x6c008000,"Unsupported shadow vertex batch");const auto vectors=(command>>16)&255;
            require(vectors>=9&&vectors%3==0,"Invalid shadow vertex count");const unsigned n=vectors/3,start=unsigned(result.vertices.size());
            require(p+std::size_t(n)*48+4<=end,"Shadow batch exceeds packet");
            for(unsigned i=0;i<n;++i){
                const auto at=p+i*48;const auto bone=word(at)&65535,flags=word(at+8);
                require(bone%4==0&&bone/4<count,"Invalid primary shadow bone");
                require((flags&~0xc000u)==(i==0?n:0)&&(i>=2||(flags&0x8000)),"Invalid shadow strip header");
                // VU 700..707 reads only the primary bone and position XYZ.
                // Packed spare words and the normal are not skin weights.
                result.vertices.push_back({vector(at+16,1),bone/4});
                if(!(flags&0x8000))result.indices.insert(result.indices.end(),{start+i-2,start+i-1,start+i});
            }
            p+=std::size_t(n)*48;require(word(p)==0x17000000,"Missing shadow batch boundary");p+=4;
        }
        require(p==end,"Invalid shadow packet end");
    }
    require(p+4<=end_section&&word(p)==0xffffffff,"Missing shadow packet terminator");return result;
}
std::vector<math::Vector4> pose_shadow_geometry(const ShadowGeometry& geometry,std::span<const math::Matrix4> pose){
    require(pose.size()>=geometry.bones.size(),"Shadow pose is missing bones");std::vector<math::Matrix4> matrices;
    for(unsigned i=0;i<geometry.bones.size();++i)matrices.push_back(geometry.bones[i].parent< -1?pose[i]:math::multiply(pose[i],geometry.bones[i].inverse_bind));
    std::vector<math::Vector4> out;out.reserve(geometry.vertices.size());
    for(const auto& v:geometry.vertices){require(v.bone<matrices.size(),"Shadow vertex references missing bone");out.push_back(math::transform(matrices[v.bone],v.position));}
    return out;
}
}
