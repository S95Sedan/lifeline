#include "rendering/model.hpp"
#include <bit>
#include <limits>

namespace lifeline {
namespace {
struct Reader {
    std::span<const unsigned char> b;
    void check(std::size_t p,std::size_t n) const {if(p>b.size() || n>b.size()-p) throw std::runtime_error("SOLCOM extent outside asset");}
    unsigned u32(std::size_t p) const {check(p,4);return le32(b.data()+p);}
    unsigned u16(std::size_t p) const {check(p,2);return le16(b.data()+p);}
    float number(std::size_t p) const {const auto f=std::bit_cast<float>(u32(p));if(!std::isfinite(f) || std::abs(f)>1e7f) throw std::runtime_error("Invalid SOLCOM coordinate");return f;}
    math::Vector4 vector(std::size_t p,float w) const {return {number(p),number(p+4),number(p+8),w};}
};
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
// Native overlay classification tolerances, not recovered SOLCOM flag semantics.
constexpr double layer_planarity=.001,layer_parallel=1e-5;
constexpr double layer_min_offset=.005,layer_max_offset=.1;
struct LayerVector {
    double x{},y{},z{};
    LayerVector operator-(LayerVector b)const{return {x-b.x,y-b.y,z-b.z};}
    LayerVector operator*(double s)const{return {x*s,y*s,z*s};}
};
LayerVector layer_position(const ModelVertex& v){return {v.position.x,v.position.y,v.position.z};}
double layer_dot(LayerVector a,LayerVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
LayerVector layer_cross(LayerVector a,LayerVector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
struct LayerPlane {LayerVector normal,point;};
std::optional<LayerPlane> layer_plane(const ModelMesh& mesh){
    if(mesh.stream!=1 || ((mesh.flags>>24)!=0x44&&(mesh.flags>>24)!=0x48))return {};
    // Use the largest emitted triangle, independent of authored vertex normals
    // and strip winding. Degenerate packets cannot establish a plane.
    LayerPlane plane{};double largest=0;
    for(std::size_t i=0;i+2<mesh.indices.size();i+=3){
        const auto a=layer_position(mesh.vertices[mesh.indices[i]]);
        const auto b=layer_position(mesh.vertices[mesh.indices[i+1]]);
        const auto c=layer_position(mesh.vertices[mesh.indices[i+2]]);
        const auto normal=layer_cross(b-a,c-a);const double area=layer_dot(normal,normal);
        if(area>largest){largest=area;plane={normal,a};}
    }
    if(!(largest>0) || !std::isfinite(largest))return {};
    plane.normal=plane.normal*(1/std::sqrt(largest));
    // Quantized near-flat floor strips can have very narrow triangles. Their
    // cross product magnifies 0.0001-unit height noise; a thin axis-aligned
    // extent establishes the same plane without amplifying that noise.
    LayerVector lo=plane.point,hi=plane.point;
    for(const auto& vertex:mesh.vertices){const auto p=layer_position(vertex);lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    const bool fitted=std::all_of(mesh.vertices.begin(),mesh.vertices.end(),[&](const auto& v){return std::abs(layer_dot(plane.normal,layer_position(v)-plane.point))<=layer_planarity;});
    if(!fitted){
        if(hi.x-lo.x<=layer_planarity){plane.normal={1,0,0};plane.point.x=(lo.x+hi.x)*.5;}
        else if(hi.y-lo.y<=layer_planarity){plane.normal={0,1,0};plane.point.y=(lo.y+hi.y)*.5;}
        else if(hi.z-lo.z<=layer_planarity){plane.normal={0,0,1};plane.point.z=(lo.z+hi.z)*.5;}
    }
    const auto n=plane.normal;
    const double dominant=std::abs(n.x)>=std::abs(n.y) && std::abs(n.x)>=std::abs(n.z)?n.x:
        (std::abs(n.y)>=std::abs(n.z)?n.y:n.z);
    if(dominant<0)plane.normal=plane.normal*(-1);
    for(const auto& v:mesh.vertices){
        const double distance=layer_dot(plane.normal,layer_position(v)-plane.point);
        if(!std::isfinite(distance) || std::abs(distance)>layer_planarity)return {};
    }
    return plane;
}
struct LayerBounds {
    double low_u=std::numeric_limits<double>::infinity(),high_u=-std::numeric_limits<double>::infinity();
    double low_v=std::numeric_limits<double>::infinity(),high_v=-std::numeric_limits<double>::infinity();
};
bool layer_overlap(const LayerBounds& a,const LayerBounds& b){
    // A shared edge alone is not evidence of an overlay.
    return std::max(a.low_u,b.low_u)<std::min(a.high_u,b.high_u) &&
        std::max(a.low_v,b.low_v)<std::min(a.high_v,b.high_v);
}
void classify_static_layers(std::span<ModelMesh> meshes){
    // Called separately for each node. Packet order makes the IDs deterministic.
    struct Candidate {std::size_t mesh;LayerPlane plane;};
    std::vector<Candidate> candidates;
    for(std::size_t i=0;i<meshes.size();++i)if(const auto plane=layer_plane(meshes[i]))candidates.push_back({i,*plane});
    unsigned group=0;
    for(std::size_t seed=0;seed<candidates.size();++seed){
        if(meshes[candidates[seed].mesh].layer_group)continue;
        const auto anchor=candidates[seed].plane;const auto n=anchor.normal;
        const LayerVector axis=std::abs(n.x)<=std::abs(n.y) && std::abs(n.x)<=std::abs(n.z)?LayerVector{1,0,0}:
            (std::abs(n.y)<=std::abs(n.z)?LayerVector{0,1,0}:LayerVector{0,0,1});
        auto u=layer_cross(n,axis);u=u*(1/std::sqrt(layer_dot(u,u)));const auto v=layer_cross(n,u);
        std::vector<bool> eligible(candidates.size()),included(candidates.size());
        std::vector<double> offsets(candidates.size());std::vector<LayerBounds> bounds(candidates.size());
        double eligible_low=0,eligible_high=0;
        for(std::size_t i=0;i<candidates.size();++i){
            const auto& c=candidates[i];if(meshes[c.mesh].layer_group)continue;
            const auto cross=layer_cross(n,c.plane.normal);
            if(layer_dot(cross,cross)>layer_parallel*layer_parallel)continue;
            offsets[i]=layer_dot(n,c.plane.point-anchor.point);
            if(std::abs(offsets[i])>layer_max_offset)continue;
            eligible[i]=true;
            eligible_low=std::min(eligible_low,offsets[i]);eligible_high=std::max(eligible_high,offsets[i]);
            for(const auto& vertex:meshes[c.mesh].vertices){
                const auto p=layer_position(vertex)-anchor.point;const auto x=layer_dot(u,p),y=layer_dot(v,p);
                auto& b=bounds[i];b.low_u=std::min(b.low_u,x);b.high_u=std::max(b.high_u,x);
                b.low_v=std::min(b.low_v,y);b.high_v=std::max(b.high_v,y);
            }
        }
        // Ordinary single-plane glass cannot establish an overlay stack.
        if(eligible_high-eligible_low<layer_min_offset)continue;
        std::vector<std::size_t> connected{seed};included[seed]=true;
        double low=0,high=0;bool separated_overlap=false;
        for(std::size_t next=0;next<connected.size();++next){
            const auto a=connected[next];
            for(std::size_t b=0;b<candidates.size();++b){
                if(!eligible[b] || !layer_overlap(bounds[a],bounds[b]))continue;
                const auto new_low=std::min(low,offsets[b]),new_high=std::max(high,offsets[b]);
                // Both the anchor bound and total span are fixed: a chain must
                // not bridge successively farther planes, even on opposite sides.
                if(new_high-new_low>layer_max_offset)continue;
                if(std::abs(offsets[a]-offsets[b])>=layer_min_offset&&
                   (meshes[candidates[a].mesh].flags>>24)==0x44&&
                   (meshes[candidates[b].mesh].flags>>24)==0x44)separated_overlap=true;
                if(!included[b]){included[b]=true;connected.push_back(b);low=new_low;high=new_high;}
            }
        }
        if(connected.size()<2 || !separated_overlap)continue;
        ++group;for(const auto i:connected)meshes[candidates[i].mesh].layer_group=group;
    }
}
// Invert the observed 64x1024 RGBA upload into its 512x512 indexed sheet.
// This is a byte permutation within this file, not a graphics memory model.

}
StaticModel decode_static_model(std::span<const unsigned char> bytes) {
    Reader r{bytes};r.check(0,64);
    const auto version=std::string(bytes.begin(),bytes.begin()+10);
    const bool headset=version=="SOLCOM0418";
    require(headset || version=="SOLCOM0419","Unsupported static model version");
    StaticModel model;model.nodes=r.u32(16);model.lights=decode_scene_lights(bytes);
    const unsigned page_count=r.u32(20);
    require(model.nodes && model.nodes<=1024 && page_count && page_count<=256,"Invalid SOLCOM model layout");
    r.check(64,model.nodes*256);
    const auto table=r.u32(44),upload=r.u32(24),count=r.u32(table);
    require(count && count<=8192 && table>=64+model.nodes*256,"Invalid SOLCOM texture table");r.check(table+16,count*32);
    require(upload>=table+16+count*32,"Overlapping SOLCOM texture records");
    struct Page {unsigned palette,sheet,palette_height,width,height;};std::vector<Page> pages;
    unsigned cursor=upload;
    for(unsigned i=0;i<page_count;++i){
        const unsigned ph=r.u32(cursor+68);require(ph && ph<=1024 && ph%32==0,"Invalid SOLCOM palette height");
        const unsigned palette_bytes=64*ph*4,second=cursor+112+palette_bytes,sh=r.u32(second+52)/2;
        require(sh && sh<=1024 && sh%128==0,"Invalid SOLCOM indexed sheet height");
        const unsigned sheet_bytes=512*sh,total=208+palette_bytes+sheet_bytes;r.check(cursor,total);
        require(r.u32(cursor)*16+16==total && r.u32(cursor+16)==4 && r.u32(cursor+64)==64 &&
            r.u32(cursor+96)==palette_bytes/16 && r.u32(cursor+100)==0x18000000 &&
            r.u32(second)==4 && r.u32(second+48)==64 && r.u32(second+52)==sh*2 &&
            r.u32(second+80)==sheet_bytes/16 && r.u32(second+84)==0x18000000,"Unsupported SOLCOM texture transfer layout");
        const auto tex0=std::uint64_t(r.u32(second+32))|(std::uint64_t(r.u32(second+36))<<32);
        const unsigned width=1u<<((tex0>>26)&15),height=1u<<((tex0>>30)&15);
        require(((tex0>>20)&63)==19 && (width==256 || width==512) && width*height==sheet_bytes,"Unsupported SOLCOM atlas dimensions");
        pages.push_back({cursor+112,second+96,ph,width,height});cursor+=total;
    }
    std::vector<std::array<unsigned,2>> origins;
    std::vector<std::vector<unsigned>> page_textures(page_count);
    for(unsigned i=0;i<count;++i) {
        const auto p=table+16+i*32;const auto* d=bytes.data()+p+16;
        require(d[0]<pages.size(),"SOLCOM texture references missing page");const auto page=pages[d[0]];
        const unsigned unit=page.width/32,x=d[2]*unit,y=d[3]*unit,w=d[4]*unit,h=d[5]*unit;
        require(d[1]<page.palette_height/4 && w && h && x+w<=page.width && y+h<=page.height,"Invalid SOLCOM texture rectangle");
        // The descriptor retains exporter grid addresses; UVs and rectangles
        // are normalized to the actual TEX0 atlas width. Do not mistake the
        // exported block code for a byte offset into the installed sheet.
        const unsigned gx=d[2]*16,gy=d[3]*16;
        const unsigned block=((gx>>4)&1)|(((gy>>4)&1)<<1)|(((gx>>5)&1)<<2)|(((gy>>5)&1)<<3)|(((gx>>6)&1)<<4);
        require(r.u16(p+22)==(((gy/64)*(page.width/128))|(gx/128))*32+block,"SOLCOM texture block and rectangle disagree");
        const auto end=std::find(bytes.begin()+p,bytes.begin()+p+16,0);
        AssetImage image{std::string(bytes.begin()+p,end),w,h,Bytes(std::size_t(w)*h*4)};
        for(unsigned yy=0;yy<h;++yy) for(unsigned xx=0;xx<w;++xx) {
            const unsigned offset=indexed_sheet_offset(x+xx,y+yy,page.width);require(offset<page.width*page.height,"SOLCOM texture pixel outside atlas");
            const unsigned index=bytes[page.sheet+offset];
            const unsigned j=(index&0xe7)|((index&8)<<1)|((index&16)>>1),bank=d[1];
            const unsigned px=(bank&1)*16+(bank&4)*8+j%16,py=(bank&2)*8+(bank/8)*32+j/16;
            const auto* color=bytes.data()+page.palette+(py*64+px)*4;auto* out=image.rgba.data()+(yy*w+xx)*4;
            std::copy_n(color,3,out);out[3]=static_cast<unsigned char>(std::min(unsigned(color[3])*2,255u));
        }
        page_textures[d[0]].push_back(unsigned(model.textures.size()));
        origins.push_back({x,y});model.textures.push_back(std::move(image));
    }
    for(unsigned node=0;node<model.nodes;++node) {
        const auto first_mesh=model.meshes.size();
        const auto record=64+node*256;require(r.u32(record+8)==0,"Unsupported third SOLCOM geometry stream");
        math::Matrix4 transform{};
        for(unsigned i=0;i<4;++i) transform.columns[i]=r.vector(record+64+i*16,i==3?1.f:0.f);
        for(unsigned stream=0;stream<2;++stream) {
            std::size_t p=r.u32(record+stream*4);if(!p) continue;
            require(p>=64+model.nodes*256 && p<table && p%16==0,"Invalid SOLCOM geometry pointer");
            while(r.u32(p)!=0xffffffff) {
                require(model.meshes.size()<4096,"Too many SOLCOM packets");
                const auto tag=r.u32(p),material=r.u32(p+4),local_texture=material&0xffff,page=(material>>16)&255;
                require(page<page_textures.size() && local_texture<page_textures[page].size(),"SOLCOM material references missing page texture");
                const auto texture=page_textures[page][local_texture];
                require((tag&0xfffe0000)==0x30000000 && ((material>>24)==0 || (stream==1 && ((material>>24)==0x42 || (material>>24)==0x44 || (material>>24)==0x48))) && r.u32(p+8)==0 && (r.u32(p+12)==0 || r.u32(p+12)==0x01000104),"Unsupported SOLCOM geometry packet");
                const auto end=p+16+(tag&65535)*16;require(end<=table && end>p+16,"Invalid SOLCOM packet size");p+=16;
                ModelMesh mesh{node,texture,stream,((tag>>16)&1)|(material&0xff000000)};
                mesh.receives_shadow=true;
                while(p<end) {
                    const auto command=r.u32(p);p+=4;
                    if(command==0 || command==0x01000404 || command==0x01000104) continue;
                    const unsigned n=(command>>16)&255;
                    require((command&0xff00ffff)==0x6d00c000 && n>=3 && n<=64,"Unsupported SOLCOM vertex batch");
                    const auto uv=p;p+=n*8;
                    require(r.u32(p)==(0x68008001|(n<<16)),"Missing SOLCOM positions");p+=4;const auto positions=p;p+=n*12;
                    require(r.u32(p)==(0x6d008002|(n<<16)),"Missing SOLCOM normals");p+=4;const auto normals=p;p+=n*8;
                    require(r.u32(p)==(0x6e00c003|(n<<16)),"Missing SOLCOM colors");p+=4;const auto colors=p;p+=n*4;
                    require(p+4<=end && r.u32(p)==0x17000000,"Missing SOLCOM batch boundary");p+=4;
                    require((r.u16(uv+4)&0x7fff)==n,"SOLCOM vertex count disagrees with strip header");
                    const auto base=unsigned(mesh.vertices.size());
                    for(unsigned i=0;i<n;++i) {
                        const auto flags=r.u16(uv+i*8+4);
                        require((flags&0x7fff)==(i==0?n:0) && (i>=2 || (flags&0x8000)),"Invalid SOLCOM triangle strip flags");
                        const auto component=[&](unsigned a){return float(static_cast<std::int16_t>(r.u16(normals+i*8+a*2)))/4096.f;};
                        math::Vector4 normal{component(0),component(1),component(2),0};
                        const auto* c=bytes.data()+colors+i*4;
                        const auto& tex=model.textures[texture];const auto origin=origins[texture];
                        mesh.vertices.push_back({math::transform(transform,r.vector(positions+i*12,1)),math::transform(transform,normal),
                            {c[0]/128.f,c[1]/128.f,c[2]/128.f,c[3]/128.f},
                            (r.u16(uv+i*8)*(pages[page].width/32768.f)-float(origin[0]))/float(tex.width),
                            (r.u16(uv+i*8+2)*(pages[page].width/32768.f)-float(origin[1]))/float(tex.height)});
                        if(!(flags&0x8000)) {
                            const auto a=base+i-2,b=base+i-1,cindex=base+i;
                            mesh.indices.insert(mesh.indices.end(),{(i&1)?b:a,(i&1)?a:b,cindex});
                        }
                    }
                }
                require(p==end,"SOLCOM batch crosses packet boundary");model.meshes.push_back(std::move(mesh));
            }
        }
        classify_static_layers(std::span<ModelMesh>(model.meshes).subspan(first_mesh));
        // Node +12 is exporter polygon metadata, not a packet extent. The
        // original loader (1e22b8..1e22f8) relocates the streams independently;
        // ADC strip flags above determine which triangles actually draw.
        // Some authored rooms retain more polygons here than their packets emit.
    }
    return model;
}
}


#include "rendering/model.hpp"
#include <stdexcept>
namespace lifeline {
math::Matrix4 reflection_matrix(math::Vector4 normal,math::Vector4 point){
    const float length=math::dot_xyz(normal,normal);
    if(!std::isfinite(length)||length<=0||!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))
        throw std::runtime_error("Invalid character reflection plane");
    normal=math::normalize_xyz(normal);auto matrix=math::identity();
    for(unsigned i=0;i<3;++i)matrix.columns[i]=matrix.columns[i]+normal*(-2*math::dot_xyz(matrix.columns[i],normal));
    matrix.columns[3]=matrix.columns[3]+normal*(2*math::dot_xyz(point,normal));return matrix;
}
ModelMesh reflect_skin_mesh(const ModelMesh& source,const math::Matrix4& plane){
    auto mesh=source;
    for(auto& v:mesh.vertices){
        auto position=v.position,normal=v.normal;position.y=-position.y;normal.y=-normal.y;
        position.w=1;normal.w=0;
        v.position=math::transform(plane,position);v.normal=math::transform(plane,normal);
        v.position.y=-v.position.y;v.normal.y=-v.normal.y;
    }
    // The additional original skeletal pass uses the ordinary surface packets,
    // not a captured-frame refraction texture.
    mesh.refraction={};mesh.captured_scene=false;mesh.reflection_pass=true;return mesh;
}
}
