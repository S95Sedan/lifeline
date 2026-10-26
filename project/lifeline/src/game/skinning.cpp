#include "game/animation.hpp"
#include <bit>
namespace lifeline {
namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
struct Reader {
    std::span<const unsigned char> b;
    unsigned word(std::size_t p) const {require(p<=b.size() && b.size()-p>=4,"Skin record outside model");return le32(b.data()+p);}
    float number(std::size_t p) const {auto f=std::bit_cast<float>(word(p));require(std::isfinite(f) && std::abs(f)<1e7f,"Invalid skin coordinate");return f;}
    math::Vector4 vector(std::size_t p,float w) const {return {number(p),number(p+4),number(p+8),w};}
};
}
SkinnedGeometry decode_skinned_geometry(std::span<const unsigned char> bytes,const Skeleton& skeleton) {
    Reader r{bytes};require(bytes.size()>=64,"Truncated skin header");
    const std::string version(bytes.begin(),bytes.begin()+10);require(version=="SKICOM0830" || version=="SKICOM0828" || version=="SKICOM0829","Unsupported skin format");
    require(skeleton.bones.size()==(r.word(32)&65535),"Skin and skeleton counts disagree");
    const auto table=r.word(44),textures=r.word(table);
    require(textures>0 && textures<=256 && table>=64+skeleton.bones.size()*256 && r.word(24)==0,"Unsupported skin section layout");
    SkinnedGeometry result;result.planar_reflection=version=="SKICOM0830";result.shadow=decode_shadow_geometry(bytes);
    for(unsigned stream=0;stream<2;++stream) {
        std::size_t p=r.word(16+stream*4);if(!p) continue;
        require(p>=64+skeleton.bones.size()*256 && p<table && p%16==0,"Invalid skin packet pointer");
        while(r.word(p)!=0xffffffff) {
            const auto tag=r.word(p),material=r.word(p+4);const std::size_t end=p+16+(tag&65535)*16;
            require((tag&0xffff0000)==0x30000000 && (material&255)<textures && (r.word(p+8)==0 || r.word(p+8)==0x1c0) && end<=table && end>p+16,"Unsupported skin packet");
            SkinMesh mesh;mesh.texture=material&255;mesh.flags=material&0xffffff00;mesh.material_parameter=r.word(p+8);p+=12;
            while(p<end) {
                const auto cmd=r.word(p);p+=4;
                // Exporter leaves low 16-bit padding values in these NOP slots.
                if((cmd>>16)==0) continue;
                require((cmd&0xff00ffff)==0x6c008000,"Unsupported skin attribute batch");
                const auto vectors=(cmd>>16)&255;require(vectors>=12 && vectors%4==0,"Invalid skin batch count");
                const unsigned count=vectors/4,base=unsigned(mesh.vertices.size());
                require(p+std::size_t(count)*64+4<=end,"Skin batch crosses packet boundary");
                for(unsigned i=0;i<count;++i) {
                    const auto v=p+i*64;SkinVertex vertex;
                    vertex.position=r.vector(v+16,1);vertex.normal=r.vector(v+32,0);
                    vertex.u=r.number(v);vertex.v=r.number(v+4);vertex.strip_flags=r.word(v+8);vertex.light=r.number(v+12);vertex.alpha=r.number(v+60)/128;
                    require((vertex.strip_flags&~0xc000u)==(i==0?count:0) && (i>=2 || (vertex.strip_flags&0x8000)),"Invalid skin strip header");
                    vertex.weights={r.number(v+28),r.number(v+44),0};vertex.weights[2]=1-vertex.weights[0]-vertex.weights[1];
                    for(unsigned k=0;k<3;++k) {
                        const int packed=static_cast<std::int16_t>(r.word(v+48+k*4)&65535);
                        // Older exporters scale the absent -1 bone by four
                        // as well. Both encodings are valid only at zero weight.
                        require(packed==-1 || packed==-4 || (packed>=0 && packed%4==0 && unsigned(packed/4)<skeleton.bones.size()),"Invalid skin bone index");
                        vertex.bones[k]=packed<0?-1:packed/4;
                        require(vertex.weights[k]>=-1e-6f && vertex.weights[k]<=1.000001f,"Invalid skin weight");
                        require(packed>=0 || std::abs(vertex.weights[k])<1e-6f,"Missing weighted skin bone");
                    }
                    mesh.vertices.push_back(vertex);
                    if(!(vertex.strip_flags&0x8000)) mesh.indices.insert(mesh.indices.end(),{base+i-2,base+i-1,base+i});
                }
                p+=std::size_t(count)*64;require(r.word(p)==0x17000000,"Missing skin batch boundary");p+=4;
            }
            require(p==end,"Invalid skin packet end");result.meshes.push_back(std::move(mesh));
        }
    }
    require(!result.meshes.empty(),"Empty skinned geometry");return result;
}
std::vector<math::Vector4> skin_positions(const SkinMesh& mesh,const Skeleton& skeleton,std::span<const math::Matrix4> pose) {
    require(pose.size()==skeleton.bones.size(),"Skin pose size mismatch");
    std::vector<math::Matrix4> skin;skin.reserve(pose.size());
    for(std::size_t i=0;i<pose.size();++i) skin.push_back(math::multiply(pose[i],skeleton.bones[i].inverse_bind));
    std::vector<math::Vector4> result;result.reserve(mesh.vertices.size());
    for(const auto& vertex:mesh.vertices) {
        math::Vector4 position{};
        for(unsigned k=0;k<3;++k) if(vertex.bones[k]>=0) {
            require(std::size_t(vertex.bones[k])<skin.size(),"Skin bone outside pose");
            position=position+math::transform(skin[std::size_t(vertex.bones[k])],vertex.position)*vertex.weights[k];
        }
        position.w=1;result.push_back(position);
    }
    return result;
}
}

namespace lifeline {
StaticModel decode_skin_surface(std::span<const unsigned char> bytes,const SkinnedGeometry& geometry) {
    require(bytes.size()>=64,"Truncated skin surface header");
    const std::string_view version(reinterpret_cast<const char*>(bytes.data()),10);
    require(version=="SKICOM0828"||version=="SKICOM0829"||version=="SKICOM0830","Unsupported skin surface format");
    Reader r{bytes};const auto table=r.word(44),upload=r.word(28);const auto count=r.word(table);
    const auto material_table=r.word(52);
    const bool tableless=(version=="SKICOM0828"||version=="SKICOM0829")&&material_table==0;
    // 1deaDC..1deaE8 preserves a null pointer; the original loader selects
    // distinct 0828/0829 renderers (1dc11c..1dc178), sharing loader
    // 1dea60 and the bone/packet/texture layout. It is not an 0830
    // material table with missing records.
    if(!tableless)require(material_table>=table+16+std::uint64_t(count)*32&&material_table<upload,"Invalid skin material table section");
    require(count>0 && count<=256 && upload>=table+16+count*32,"Unsupported skin texture layout");
    const auto palette_height=r.word(upload+68);
    // Transfer count is derived from the authored palette height, not from
    // the two heights seen in Rio's and the first enemy's models.
    require(palette_height>=32 && palette_height<=1024 && palette_height%32==0 && r.word(upload+64)==64 &&
        r.word(upload+96)==palette_height*16,"Unsupported skin palette transfer");
    const std::size_t palette=std::size_t(upload)+112,second=palette+64*palette_height*4,sheet=second+96;
    const auto tex0=std::uint64_t(r.word(second+32))|(std::uint64_t(r.word(second+36))<<32);
    const unsigned width=1u<<((tex0>>26)&15),height=1u<<((tex0>>30)&15);
    require(r.word(upload)==12+palette_height*16+width*height/16,"Skin upload count differs from palette and atlas extents");
    require(((tex0>>20)&63)==19 && (width==256 || width==512) && height==width &&
        r.word(second+48)==64 && r.word(second+52)==width*height/256 && r.word(second+80)==width*height/16 &&
        sheet<=bytes.size() && width*height<=bytes.size()-sheet,"Unsupported skin indexed atlas transfer");
    StaticModel model;model.nodes=1;std::vector<std::array<unsigned,2>> origins;
    for(unsigned i=0;i<count;++i) {
        const auto p=table+16+i*32;const auto* d=bytes.data()+p+16;
        const unsigned unit=width/32,x=d[2]*unit,y=d[3]*unit,w=d[4]*unit,h=d[5]*unit,bank=d[1];
        require(d[0]==0 && bank<palette_height/4 && w && h && x+w<=width && y+h<=height,"Invalid skin texture rectangle");
        const auto last=std::find(bytes.begin()+p,bytes.begin()+p+16,0);AssetImage image{std::string(bytes.begin()+p,last),w,h,Bytes(std::size_t(w)*h*4)};
        for(unsigned yy=0;yy<h;++yy) for(unsigned xx=0;xx<w;++xx) {
            const unsigned index=bytes[sheet+indexed_sheet_offset(x+xx,y+yy,width)],j=(index&0xe7)|((index&8)<<1)|((index&16)>>1);
            const unsigned px=(bank&1)*16+(bank&4)*8+j%16,py=(bank/8)*32+(bank&2)*8+j/16;
            const auto* color=bytes.data()+palette+(py*64+px)*4;auto* out=image.rgba.data()+(yy*w+xx)*4;
            std::copy_n(color,3,out);out[3]=static_cast<unsigned char>(std::min(unsigned(color[3])*2,255u));
        }
        origins.push_back({x,y});model.textures.push_back(std::move(image));
    }
    for(const auto& source:geometry.meshes) {
        require(source.texture<count,"Skin texture outside surface table");
        ModelMesh mesh;mesh.texture=source.texture;mesh.flags=source.flags;mesh.indices=source.indices;mesh.opaque_alpha_depth=true;
        const auto material_index=r.word(table+16+source.texture*32+28);
        const std::size_t material=std::size_t(material_table)+std::size_t(material_index)*112;
        // The 0828/0829 files may omit the material table at header +52.
        // Its packet/texture colors remain valid with neutral coefficients.
        // Do not interpret the file header as a material when that pointer is 0.
        if(tableless){
            require(material_index==0,"Legacy skin references an absent material table");
        }else{
            require(material+104<=upload,"Skin material outside material table");
            mesh.skin_material={r.vector(material+16,0),r.vector(material+48,0),r.vector(material+64,0),r.vector(material+80,0),r.number(material+96),r.number(material+100)};
        }
        const auto& texture=model.textures[mesh.texture];const auto origin=origins[mesh.texture];
        for(const auto& vertex:source.vertices) mesh.vertices.push_back({vertex.position,vertex.normal,{1,1,1,vertex.alpha},
            (vertex.u*width-float(origin[0]))/float(texture.width),(vertex.v*height-float(origin[1]))/float(texture.height)});
        model.meshes.push_back(std::move(mesh));
    }
    return model;
}
void pose_skin_surface(StaticModel& surface,const SkinnedGeometry& geometry,const Skeleton& skeleton,std::span<const math::Matrix4> pose) {
    require(surface.meshes.size()==geometry.meshes.size(),"Skin surface mesh mismatch");
    require(pose.size()==skeleton.bones.size(),"Skin pose size mismatch");
    std::vector<math::Matrix4> skin;skin.reserve(pose.size());
    for(std::size_t i=0;i<pose.size();++i)skin.push_back(math::multiply(pose[i],skeleton.bones[i].inverse_bind));
    for(std::size_t i=0;i<surface.meshes.size();++i) {
        const auto positions=skin_positions(geometry.meshes[i],skeleton,pose);require(positions.size()==surface.meshes[i].vertices.size(),"Skin surface vertex mismatch");
        for(std::size_t v=0;v<positions.size();++v) {
            auto p=positions[v];p.y=-p.y; // Preview converts asset Y-up to room/view Y-down.
            surface.meshes[i].vertices[v].position=p;
            // SKICOM0830's original reflection pass transforms the normal by
            // the first bone only; position uses all three weighted bones.
            const auto& source=geometry.meshes[i].vertices[v];
            require(source.bones[0]>=0 && std::size_t(source.bones[0])<skin.size(),"Missing primary skin bone");
            auto normal=math::transform(skin[std::size_t(source.bones[0])],source.normal);normal.y=-normal.y;normal.w=0;
            surface.meshes[i].vertices[v].normal=normal;
        }
    }
}
}
