#include "script/drawing.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <bit>
#include <execution>

namespace lifeline::script {
namespace {
int integer(float value){if(!std::isfinite(value) || double(value)<INT32_MIN || double(value)>INT32_MAX)throw std::runtime_error("Invalid drawing integer");return int(std::nearbyint(value));}
bool compare(int mode,float a,float b){switch(mode){case 0:return false;case 1:return true;case 2:return a<b;case 3:return a<=b;case 4:return a==b;case 5:return a>=b;case 6:return a>b;case 7:return a!=b;}return false;}
std::array<unsigned,4> read_vector(const Invocation& c){
    const int address=integer(c.arguments[0]);
    if(address<0 || (address&3))throw std::runtime_error("Unaligned drawing vector");
    const unsigned word=unsigned(address)/4;
    if(c.state?!c.state->data_contains(c.program,word,4):std::uint64_t(word)+4>c.program.size())throw std::runtime_error("Drawing vector exceeds live script allocation");
    std::array<unsigned,4> out;
    for(unsigned i=0;i<4;++i){if(c.state){const auto it=c.state->data->find(word+i);if(it!=c.state->data->end()){out[i]=std::bit_cast<unsigned>(it->second);continue;}}out[i]=c.program.word(word+i);}return out;
}
}
Bindings Drawing::bindings(){
    Bindings out=matrix_bindings();out.merge(texture_bindings());
    out.emplace(973,Binding{1,0,[this](const Invocation& c){style_.front_face=integer(c.arguments[0]);return Reply{false,0.f};}});
    out.emplace(974,Binding{1,0,[this](const Invocation& c){style_.cull_face=integer(c.arguments[0]);return Reply{false,0.f};}});
    out.emplace(957,Binding{1,0,[this](const Invocation& c){const int mode=integer(c.arguments[0]);if(mode>=0&&mode<3)matrix_mode_=unsigned(mode);return Reply{false,0.f};}});
    out.emplace(958,Binding{0,0,[this](const Invocation&){matrices_[matrix_mode_].current=math::identity();return Reply{false,0.f};}});
    out.emplace(968,Binding{0,0,[this](const Invocation&){auto& stack=matrices_[matrix_mode_];if(stack.saved.size()<31)stack.saved.push_back(stack.current);return Reply{false,0.f};}});
    out.emplace(969,Binding{0,0,[this](const Invocation&){auto& stack=matrices_[matrix_mode_];if(!stack.saved.empty()){stack.current=stack.saved.back();stack.saved.pop_back();}return Reply{false,0.f};}});
    out.emplace(576,Binding{1,0,[this](const Invocation& c){const float value=std::trunc(c.arguments[0]);if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>INT32_MAX)throw std::runtime_error("Invalid presentation mode");presentation_mode_=int(value);return Reply{false,value};}});
    for(unsigned op:{921u,922u})out.emplace(op,Binding{1,0,[this,op](const Invocation& c){const int flag=integer(c.arguments[0]);if(flag>=0 && flag<11)style_.enabled[flag]=op==921;return Reply{false,0.f};}});
    out.emplace(923,Binding{3,0,[this](const Invocation& c){style_.alpha_test=integer(c.arguments[0])&7;style_.alpha_reference=integer(c.arguments[1])&255;style_.alpha_fail=integer(c.arguments[2])&3;return Reply{false,0.f};}});
    out.emplace(924,Binding{1,0,[this](const Invocation& c){style_.destination_alpha=integer(c.arguments[0])&1;return Reply{false,0.f};}});
    out.emplace(925,Binding{1,0,[this](const Invocation& c){const int value=integer(c.arguments[0]);style_.depth_test=(value>=5?value>>1:value)&3;return Reply{false,0.f};}});
    out.emplace(926,Binding{5,0,[this](const Invocation& c){for(unsigned i=0;i<5;++i)style_.blend[i]=integer(c.arguments[i])&(i==4?255:3);return Reply{false,0.f};}});
    out.emplace(927,Binding{4,0,[this](const Invocation& c){for(unsigned i=0;i<4;++i)style_.write[i]=integer(c.arguments[i])!=0;return Reply{false,0.f};}});
    out.emplace(928,Binding{1,0,[this](const Invocation& c){style_.depth_write=integer(c.arguments[0])!=0;return Reply{false,0.f};}});
    out.emplace(929,Binding{1,0,[this](const Invocation& c){style_.enabled[0]=integer(c.arguments[0])!=1;return Reply{false,0.f};}});
    out.emplace(930,Binding{2,0,[this](const Invocation& c){projection_=integer(c.arguments[0]);coordinates_=integer(c.arguments[1]);return Reply{false,0.f};}});
    out.emplace(972,Binding{1,0,[this](const Invocation& c){order_=integer(c.arguments[0]);return Reply{false,0.f};}});
    out.emplace(933,Binding{2,0,[this](const Invocation& c){const int slot=integer(c.arguments[1]);if(integer(c.arguments[0])==1 && slot>=0 && slot<32)texture_=slot;return Reply{false,0.f};}});
    out.emplace(936,Binding{3,0,[this](const Invocation& c){if(texture_>=0 && integer(c.arguments[0])==0 && integer(c.arguments[1])==0)textures_[texture_].function=integer(c.arguments[2])&3;return Reply{false,0.f};}});
    out.emplace(937,Binding{3,0,[this](const Invocation& c){if(texture_<0 || integer(c.arguments[0])!=1)return Reply{false,0.f};auto& texture=textures_[texture_];const int value=integer(c.arguments[2]);switch(integer(c.arguments[1])){case 0:texture.min_filter=value&7;break;case 1:texture.mag_filter=value&1;break;case 2:texture.wrap_u=value&3;break;case 3:texture.wrap_v=value&3;break;}return Reply{false,0.f};}});
    out.emplace(940,Binding{4,0,[this](const Invocation& c){if(texture_>=0)for(unsigned i=0;i<4;++i)textures_[texture_].region[i]=integer(c.arguments[i])&1023;return Reply{false,0.f};}});
    out.emplace(935,Binding{9,0,[this](const Invocation& c){
        if(integer(c.arguments[0])!=1 || texture_<0)return Reply{false,0.f};
        const int level=integer(c.arguments[1]),x=integer(c.arguments[2]),y=integer(c.arguments[3]),width=integer(c.arguments[4]),height=integer(c.arguments[5]);
        if(level!=0 || x!=0 || y!=0 || width<=0 || height<=0)throw std::runtime_error("Drawing texture upload layout not recovered");
        if(c.arguments[8]>=65536){auto& texture=textures_[texture_];texture.source=unsigned(integer(c.arguments[8]));texture.width=unsigned(width);texture.height=unsigned(height);texture.format=unsigned(integer(c.arguments[6]));texture.image.reset();return Reply{false,0.f};}
        const auto& image=image_(unsigned(integer(c.arguments[8])));
        if(width>int(image.width) || height>int(image.height))throw std::runtime_error("Drawing texture upload exceeds decoded image");
        auto uploaded=std::make_shared<AssetImage>(AssetImage{"script texture",unsigned(width),unsigned(height),Bytes(std::size_t(width)*height*4)});
        for(int row=0;row<height;++row)std::copy_n(image.rgba.data()+(std::size_t(y+row)*image.width+x)*4,std::size_t(width)*4,uploaded->rgba.data()+std::size_t(row)*width*4);
        textures_[texture_].source=0;textures_[texture_].image=std::move(uploaded);return Reply{false,0.f};
    }});
    out.emplace(943,Binding{4,0,[this](const Invocation& c){for(unsigned i=0;i<4;++i){if(!std::isfinite(c.arguments[i]))throw std::runtime_error("Invalid drawing color");current_.color[i]=std::trunc(std::clamp(c.arguments[i]*128,0.f,255.f));}return Reply{false,0.f};}});
    out.emplace(945,Binding{4,0,[this](const Invocation& c){for(unsigned i=0;i<4;++i)current_.color[i]=float(integer(c.arguments[i])&255);return Reply{false,0.f};}});
    for(unsigned op:{947u,948u})out.emplace(op,Binding{2,0,[this,op](const Invocation& c){current_.u=op==947?c.arguments[0]:float(integer(c.arguments[0]));current_.v=op==947?c.arguments[1]:float(integer(c.arguments[1]));return Reply{false,0.f};}});
    out.emplace(941,Binding{1,0,[this](const Invocation& c){primitive_=unsigned(integer(c.arguments[0]))&7;vertices_.clear();return Reply{false,0.f};}});
    out.emplace(949,Binding{3,0,[this](const Invocation& c){if(vertices_.size()<84){auto vertex=current_;vertex.x=c.arguments[0];vertex.y=c.arguments[1];vertex.z=c.arguments[2];vertices_.push_back(vertex);}return Reply{false,0.f};}});
    out.emplace(942,Binding{0,0,[this](const Invocation&){finish();return Reply{false,0.f};}});
    out.emplace(944,Binding{1,0,[this](const Invocation& c){const auto words=read_vector(c);for(unsigned i=0;i<4;++i){const float value=std::bit_cast<float>(words[i]);if(!std::isfinite(value))throw std::runtime_error("Invalid drawing color vector");current_.color[i]=std::trunc(std::clamp(value*128,0.f,255.f));}return Reply{false,0.f};}});
    out.emplace(946,Binding{1,0,[this](const Invocation& c){const auto words=read_vector(c);for(unsigned i=0;i<4;++i)current_.color[i]=float(words[i]&255);return Reply{false,0.f};}});
    out.emplace(950,Binding{1,0,[this](const Invocation& c){const auto words=read_vector(c);if(vertices_.size()<84){auto vertex=current_;vertex.x=std::bit_cast<float>(words[0]);vertex.y=std::bit_cast<float>(words[1]);vertex.z=std::bit_cast<float>(words[2]);vertices_.push_back(vertex);}return Reply{false,0.f};}});
    // These three exported handlers return immediately in this disc executable.
    for(const auto [op,count]:{std::pair{951u,3u},std::pair{952u,1u},std::pair{956u,6u}})out.emplace(op,Binding{count,0,[](const Invocation&){return Reply{false,0.f};}});
    return out;
}
void Drawing::finish(){
    if(vertices_.empty())return;
    auto texture=style_.enabled[1] && texture_>=0?textures_[texture_]:Texture{};
    if(texture.source)texture.image=std::make_shared<AssetImage>(upload(texture.source,texture.width,texture.height,texture.format));
    if(style_.enabled[1] && !texture.image)throw std::runtime_error("Script drawing has no uploaded texture");
    auto vertices=vertices_;
    for(const auto& vertex:vertices)if(!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) || !std::isfinite(vertex.u) || !std::isfinite(vertex.v))throw std::runtime_error("Nonfinite drawing vertex");
    if(coordinates_!=3)for(auto& vertex:vertices){vertex.u/=16;vertex.v/=16;}
    // Normalized ST addresses the power-of-two TEX0 extent, not just the
    // populated image. A 256x192 movie uses V=0..0.75 to sample all 192 rows.
    else if(texture.image)for(auto& vertex:vertices){vertex.u*=std::bit_ceil(texture.image->width);vertex.v*=std::bit_ceil(texture.image->height);}
    if(projection_==1)for(auto& vertex:vertices){vertex.x=(vertex.x-27648)/16;vertex.y=(vertex.y-29184)/16;}
    else if(projection_==0){
        const auto matrix=math::multiply(matrices_[1].current,matrices_[0].current);
        for(auto& vertex:vertices){const auto clip=math::transform(matrix,{vertex.x,vertex.y,vertex.z,1});vertex.x=clip.x;vertex.y=clip.y;vertex.z=clip.z;vertex.q=clip.w;}
    }else if(projection_!=2)throw std::runtime_error("Invalid drawing coordinate mode");
    const auto project=[&](Vertex vertex){if(projection_==0){vertex.q=1/vertex.q;vertex.x=viewport_[0]+(vertex.x*vertex.q+1)*viewport_[2]*.5f;vertex.y=viewport_[1]+(vertex.y*vertex.q+1)*viewport_[3]*.5f;vertex.z=(1-vertex.z*vertex.q)*32767.5f;}return vertex;};
    if(primitive_==6){
        vertices.resize(vertices.size()/2*2);
        if(projection_==0){std::vector<Vertex> visible;for(unsigned i=0;i<vertices.size();i+=2)if(vertices[i].q>0&&vertices[i+1].q>0){visible.push_back(project(vertices[i]));visible.push_back(project(vertices[i+1]));}vertices=std::move(visible);}
        if(!vertices.empty())batches_.push_back({primitive_,order_,style_,std::move(vertices),texture});return;
    }
    if(primitive_<=2){
        std::vector<Vertex> visible;
        if(primitive_==0){for(auto vertex:vertices){if(projection_==0&&(vertex.q<=0||std::abs(vertex.x)>vertex.q||std::abs(vertex.y)>vertex.q||std::abs(vertex.z)>vertex.q))continue;visible.push_back(project(vertex));}}
        else for(unsigned i=1;i<vertices.size();i+=primitive_==1?2:1){
            auto a=vertices[i-1],b=vertices[i];float begin=0,end=1;
            if(projection_==0)for(unsigned plane=0;plane<6;++plane){
                const auto distance=[&](const Vertex& v){return v.q+((plane&1)?-1.f:1.f)*(plane/2==0?v.x:plane/2==1?v.y:v.z);};
                const float da=distance(a),db=distance(b);if(da<0&&db<0){end=-1;break;}
                if((da<0)!=(db<0)){const float t=da/(da-db);if(da<0)begin=std::max(begin,t);else end=std::min(end,t);}
            }
            if(begin>end)continue;
            const auto interpolate=[&](float t){Vertex v;v.x=a.x+(b.x-a.x)*t;v.y=a.y+(b.y-a.y)*t;v.z=a.z+(b.z-a.z)*t;v.q=a.q+(b.q-a.q)*t;v.u=a.u+(b.u-a.u)*t;v.v=a.v+(b.v-a.v)*t;for(unsigned c=0;c<4;++c)v.color[c]=a.color[c]+(b.color[c]-a.color[c])*t;return project(v);};
            visible.push_back(interpolate(begin));visible.push_back(interpolate(end));
        }
        if(!visible.empty())batches_.push_back({primitive_==0?0u:1u,order_,style_,std::move(visible),texture});return;
    }
    const bool world=projection_==0&&bool(texture.image)&&style_.enabled[8]&&style_.depth_test>=2;
    std::vector<Vertex> triangles;
    const auto triangle=[&](Vertex a,Vertex b,Vertex c){
        // Clip in homogeneous space before perspective division. This also
        // prevents camera-plane crossings from producing unbounded geometry.
        std::vector<Vertex> polygon{a,b,c};
        // World surfaces are clipped later against the actual drawable's
        // frustum. Keep near/far clipping here for safe perspective division,
        // but do not discard the geometry in the expanded widescreen margins.
        if(projection_==0)for(unsigned plane=world?4:0;plane<6 && !polygon.empty();++plane){
            const auto distance=[&](const Vertex& v){return v.q+((plane&1)?-1.f:1.f)*(plane/2==0?v.x:plane/2==1?v.y:v.z);};
            std::vector<Vertex> clipped;auto before=polygon.back();float d0=distance(before);
            for(const auto& after:polygon){const float d1=distance(after);if((d0>=0)!=(d1>=0)){const float t=d0/(d0-d1);Vertex v;v.x=before.x+(after.x-before.x)*t;v.y=before.y+(after.y-before.y)*t;v.z=before.z+(after.z-before.z)*t;v.q=before.q+(after.q-before.q)*t;v.u=before.u+(after.u-before.u)*t;v.v=before.v+(after.v-before.v)*t;for(unsigned i=0;i<4;++i)v.color[i]=before.color[i]+(after.color[i]-before.color[i])*t;clipped.push_back(v);}if(d1>=0)clipped.push_back(after);before=after;d0=d1;}polygon=std::move(clipped);
        }
        for(unsigned i=2;i<polygon.size();++i){if(projection_==0&&(polygon[0].q<=0||polygon[i-1].q<=0||polygon[i].q<=0))continue;triangles.push_back(project(polygon[0]));triangles.push_back(project(polygon[i-1]));triangles.push_back(project(polygon[i]));}
    };
    if(primitive_==3)for(unsigned i=2;i<vertices.size();i+=3)triangle(vertices[i-2],vertices[i-1],vertices[i]);
    else if(primitive_==4)for(unsigned i=2;i<vertices.size();++i)triangle(vertices[i-(i&1?1:2)],vertices[i-(i&1?2:1)],vertices[i]);
    else if(primitive_==5)for(unsigned i=2;i<vertices.size();++i)triangle(vertices[0],vertices[i-1],vertices[i]);
    else throw std::runtime_error("Unsupported drawing primitive");
    // Projected, depth-tested surfaces belong to the scene depth buffer, not
    // the post-scene overlay. Keep intentionally depth-independent UI there.
    if(!triangles.empty())batches_.push_back({3,order_,style_,std::move(triangles),texture,world});
}
void Drawing::append_world(StaticModel& model,const SceneCamera& camera)const{
    const auto forward=math::normalize_xyz(camera.target+camera.eye*-1);
    const auto right=math::normalize_xyz(math::cross_xyz(forward,camera.up));
    const auto up=math::cross_xyz(right,forward);
    std::vector<const Batch*> ordered;
    for(const auto& batch:batches_)if(batch.world)ordered.push_back(&batch);
    std::stable_sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->order<b->order;});
    for(const auto* batch:ordered){
        ModelMesh mesh;mesh.node=model.nodes++;mesh.texture=unsigned(model.textures.size());
        model.textures.push_back(*batch->texture.image);
        mesh.fog_mode=0;mesh.depth_write=batch->style.depth_write;
        mesh.world_surface=true;
        mesh.linear_filter=batch->texture.mag_filter!=0;
        mesh.repeat_u=batch->texture.wrap_u==0;mesh.repeat_v=batch->texture.wrap_v==0;
        if(batch->style.enabled[3])mesh.effect_blend=std::array<unsigned,4>{unsigned(batch->style.blend[0]),unsigned(batch->style.blend[1]),unsigned(batch->style.blend[2]),unsigned(batch->style.blend[3])};
        for(const auto& v:batch->vertices){
            // Recover camera-space distance from homogeneous W, not the GS
            // integer Z value. ActorScene applies its common Y conversion.
            const float depth=1/v.q;
            auto position=camera.eye+forward*depth+right*((v.x-320)*depth/camera.focal_pixels)+up*((224-v.y)*depth/camera.focal_pixels);
            math::Vector4 color{v.color[0]/128,v.color[1]/128,v.color[2]/128,v.color[3]/128};
            if(batch->texture.function==1)color={1,1,1,1};
            mesh.indices.push_back(unsigned(mesh.vertices.size()));
            mesh.vertices.push_back({position,{},color,v.u/batch->texture.image->width,v.v/batch->texture.image->height});
        }
        model.meshes.push_back(std::move(mesh));
    }
}
void Drawing::render(AssetImage& image,bool world_submitted)const{
    if(std::none_of(batches_.begin(),batches_.end(),[&](const auto& batch){return !world_submitted||!batch.world;}))return;
    const float scale=frame_scale(image),offset_x=(image.width-640*scale)*.5f,offset_y=(image.height-448*scale)*.5f;
    std::vector<std::size_t> order(batches_.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return batches_[a].order<batches_[b].order;});
    std::vector<float> depth(std::size_t(image.width)*image.height,0);
    for(const auto index:order){const auto& batch=batches_[index];const auto& style=batch.style;
        if(world_submitted&&batch.world)continue;
        const bool triangle=batch.primitive==3,line=batch.primitive==1,point=batch.primitive==0;const unsigned stride=triangle?3:point?1:2;
        for(unsigned i=0;i+stride<=batch.vertices.size();i+=stride){auto a=batch.vertices[i],b=point?a:batch.vertices[i+1],c=triangle?batch.vertices[i+2]:b;
            for(auto* v:{&a,&b,&c}){v->x=v->x*scale+offset_x;v->y=v->y*scale+offset_y;}
            const auto edge=[](const Vertex& a,const Vertex& b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
            const float area=edge(a,b,c.x,c.y);if(triangle&&area==0)continue;
            if(triangle&&style.enabled[5]){const float sign=(style.front_face==1)==(style.cull_face==1)?1.f:-1.f;if(area*sign<0)continue;}
            const float expansion=line||point?.5f*scale:0.f;
            float left=std::min({a.x,b.x,c.x})-expansion,right=std::max({a.x,b.x,c.x})+expansion,top=std::min({a.y,b.y,c.y})-expansion,bottom=std::max({a.y,b.y,c.y})+expansion;
            if(point&&scale>1){left=std::floor(a.x/scale)*scale;top=std::floor(a.y/scale)*scale;right=left+scale;bottom=top+scale;}
            if(!std::isfinite(left+right+top+bottom+b.z) || right==left || bottom==top)continue;
            const int x0=int(std::clamp(std::ceil(left-.5f),0.f,float(image.width))),x1=int(std::clamp(std::ceil(right-.5f),0.f,float(image.width)));
            const int y0=int(std::clamp(std::ceil(top-.5f),0.f,float(image.height))),y1=int(std::clamp(std::ceil(bottom-.5f),0.f,float(image.height)));
            const bool interpolate=triangle||line||point;
            const float rho=triangle?std::max({std::abs((b.u-a.u)*(c.y-a.y)-(c.u-a.u)*(b.y-a.y)),std::abs((b.v-a.v)*(c.x-a.x)-(c.v-a.v)*(b.x-a.x))})/std::abs(area):std::max(std::abs((b.u-a.u)/(b.x-a.x)),std::abs((b.v-a.v)/(b.y-a.y)));
            unsigned last_mip=0;while(last_mip+1<batch.texture.mipmaps.size()&&batch.texture.mipmaps[last_mip+1])++last_mip;
            // Screen-space textured rectangles have separable UVs and constant
            // color/LOD. Resolve wrap/filter coordinates per row/column, not
            // repeatedly for every channel of every pixel (explosion overlay).
            if(batch.primitive==6&&batch.texture.image&&!style.enabled[6]&&!style.enabled[7]&&!style.enabled[8]&&
                (batch.texture.lod_bias<=0||batch.texture.min_filter<2||last_mip==0)){
                const auto& texture=*batch.texture.image;
                const bool minifying=rho>1||batch.texture.lod_bias>0;
                const bool linear=minifying?(batch.texture.min_filter==1||batch.texture.min_filter>=4):batch.texture.mag_filter!=0;
                const auto coordinate=[](int value,int extent,int mode,int low,int high){switch(mode){case 0:return (value%extent+extent)%extent;case 1:return std::clamp(value,0,extent-1);case 2:return std::min(std::max(value,low),high)%extent;case 3:return ((value&low)|high)%extent;}return 0;};
                struct Sample {int first,second;float mix;};
                const auto sample=[&](float value,int extent,int mode,int low,int high){
                    if(!std::isfinite(value)||std::abs(value)>268435456)throw std::runtime_error("Drawing texture coordinate exceeds native range");
                    const float at=value-(linear?.5f:0.f);const int first=int(std::floor(at));
                    return Sample{coordinate(first,extent,mode,low,high),coordinate(first+(linear?1:0),extent,mode,low,high),linear?at-first:0.f};
                };
                std::vector<Sample> columns;columns.reserve(std::max(0,x1-x0));
                for(int x=x0;x<x1;++x)columns.push_back(sample(a.u+(b.u-a.u)*(x+.5f-a.x)/(b.x-a.x),int(texture.width),batch.texture.wrap_u,batch.texture.region[0],batch.texture.region[1]));
                std::vector<Sample> rows;std::vector<int> scanlines;
                for(int y=y0;y<y1;++y){rows.push_back(sample(a.v+(b.v-a.v)*(y+.5f-a.y)/(b.y-a.y),int(texture.height),batch.texture.wrap_v,batch.texture.region[2],batch.texture.region[3]));scanlines.push_back(y);}
                const auto raster_row=[&](int y){const auto& row=rows[y-y0];
                    const auto* top=texture.rgba.data()+std::size_t(row.first)*texture.width*4;
                    const auto* bottom=texture.rgba.data()+std::size_t(row.second)*texture.width*4;
                    for(int x=x0;x<x1;++x){const auto& col=columns[x-x0];const auto pixel=std::size_t(y)*image.width+x;auto* destination=image.rgba.data()+pixel*4;
                        std::array<float,4> source=c.color,texel;
                        for(unsigned ch=0;ch<4;++ch)texel[ch]=linear?(float(top[col.first*4+ch])*(1-col.mix)+float(top[col.second*4+ch])*col.mix)*(1-row.mix)+(float(bottom[col.first*4+ch])*(1-col.mix)+float(bottom[col.second*4+ch])*col.mix)*row.mix:float(top[col.first*4+ch]);
                        const float alpha=texel[3]*128.f/255;
                        for(unsigned ch=0;ch<3;++ch)source[ch]=batch.texture.function==1?texel[ch]:texel[ch]*source[ch]/128+(batch.texture.function>=2?source[3]:0);
                        source[3]=batch.texture.function==0?alpha*source[3]/128:batch.texture.function==2?alpha+source[3]:alpha;
                        for(unsigned ch=0;ch<3;++ch)if(style.write[ch]){float value=source[ch];
                            if(style.enabled[3]){const auto color=[&](int selector){return selector==0?source[ch]:selector==1?float(destination[ch]):0.f;};const float factor=style.blend[2]==0?source[3]:style.blend[2]==1?destination[3]/2.f:float(style.blend[4]);value=(color(style.blend[0])-color(style.blend[1]))*factor/128+color(style.blend[3]);}
                            destination[ch]=std::uint8_t(std::clamp(std::trunc(value),0.f,255.f));
                        }
                        if(style.write[3])destination[3]=std::uint8_t(std::clamp(source[3]*2,0.f,255.f));if(style.depth_write)depth[pixel]=b.z;
                    }
                };
                if(std::size_t(x1-x0)*std::size_t(y1-y0)>=65536)std::for_each(std::execution::par,scanlines.begin(),scanlines.end(),raster_row);
                else for(int y:scanlines)raster_row(y);
                continue;
            }
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){const auto pixel=std::size_t(y)*image.width+x;auto* destination=image.rgba.data()+pixel*4;
                float wa=0,wb=0,wc=0;
                if(point){wa=1;if(int(x/scale)!=int(std::floor(a.x/scale))||int(y/scale)!=int(std::floor(a.y/scale)))continue;}
                if(line){const float dx=b.x-a.x,dy=b.y-a.y;if(dx==0&&dy==0)continue;
                    const bool horizontal=std::abs(dx)>=std::abs(dy);const float t=horizontal?(x+.5f-a.x)/dx:(y+.5f-a.y)/dy;
                    if(t<0||t>=1)continue;const float deviation=horizontal?y+.5f-(a.y+dy*t):x+.5f-(a.x+dx*t);if(deviation<-.5f*scale||deviation>=.5f*scale)continue;wa=1-t;wb=t;
                }
                if(triangle){wa=edge(b,c,x+.5f,y+.5f)/area;wb=edge(c,a,x+.5f,y+.5f)/area;wc=edge(a,b,x+.5f,y+.5f)/area;
                    const auto covered=[&](float weight,Vertex p,Vertex q){if(weight<0)return false;if(weight>0)return true;if(area<0)std::swap(p,q);return q.y<p.y||(q.y==p.y&&q.x>p.x);};
                    if(!covered(wa,b,c)||!covered(wb,c,a)||!covered(wc,a,b))continue;
                }
                const float z=interpolate?wa*a.z+wb*b.z+wc*c.z:b.z;
                std::array<float,4> source=c.color;
                if(interpolate&&style.enabled[0])for(unsigned channel=0;channel<4;++channel)source[channel]=wa*a.color[channel]+wb*b.color[channel]+wc*c.color[channel];
                if(batch.texture.image){const float q=interpolate?wa*a.q+wb*b.q+wc*c.q:1.f;
                    const float u=interpolate?(wa*a.u*a.q+wb*b.u*b.q+wc*c.u*c.q)/q:a.u+(b.u-a.u)*(x+.5f-a.x)/(b.x-a.x),v=interpolate?(wa*a.v*a.q+wb*b.v*b.q+wc*c.v*c.q)/q:a.v+(b.v-a.v)*(y+.5f-a.y)/(b.y-a.y);
                    if(!std::isfinite(u) || !std::isfinite(v) || std::abs(u)>268435456 || std::abs(v)>268435456)throw std::runtime_error("Drawing texture coordinate exceeds native range");
                    const auto coordinate=[](int value,int extent,int mode,int low,int high){switch(mode){case 0:return (value%extent+extent)%extent;case 1:return std::clamp(value,0,extent-1);case 2:return std::min(std::max(value,low),high)%extent;case 3:return ((value&low)|high)%extent;}return 0;};
                    const float lod=batch.texture.lod_bias+(batch.texture.fixed_lod||!interpolate?0.f:-std::log2(std::abs(q))*float(1u<<batch.texture.lod_scale));
                    const bool minifying=rho>1 || lod>0;const int filter=batch.texture.min_filter;
                    const bool linear=minifying?(filter==1||filter>=4):batch.texture.mag_filter!=0;
                    const unsigned last=last_mip;
                    const float level=minifying&&filter>=2?std::clamp(lod,0.f,float(last)):0.f;
                    const bool interpolate_mips=minifying&&(filter==3||filter==5);
                    const unsigned first=unsigned(interpolate_mips?std::floor(level):std::floor(level+.5f)),second=std::min(first+1,last);
                    const auto sample=[&](unsigned level){
                        const auto& texture=level?*batch.texture.mipmaps[level]:*batch.texture.image;
                        const float scale=1.f/float(1u<<level),tu=u*scale,tv=v*scale;
                        const auto fetch=[&](int x,int y){const auto tx=coordinate(x,int(texture.width),batch.texture.wrap_u,batch.texture.region[0]>>level,batch.texture.region[1]>>level),ty=coordinate(y,int(texture.height),batch.texture.wrap_v,batch.texture.region[2]>>level,batch.texture.region[3]>>level);return texture.rgba.data()+(std::size_t(ty)*texture.width+tx)*4;};
                        std::array<float,4> texel;
                        if(linear){const float sx=tu-.5f,sy=tv-.5f;const int x=int(std::floor(sx)),y=int(std::floor(sy));const float dx=sx-x,dy=sy-y;const auto p00=fetch(x,y),p10=fetch(x+1,y),p01=fetch(x,y+1),p11=fetch(x+1,y+1);for(unsigned c=0;c<4;++c)texel[c]=(float(p00[c])*(1-dx)+float(p10[c])*dx)*(1-dy)+(float(p01[c])*(1-dx)+float(p11[c])*dx)*dy;}
                        else {const auto p=fetch(int(std::floor(tu)),int(std::floor(tv)));for(unsigned c=0;c<4;++c)texel[c]=float(p[c]);}return texel;
                    };
                    auto texel=sample(first);if(interpolate_mips&&second!=first){const auto next=sample(second);for(unsigned channel=0;channel<4;++channel)texel[channel]=texel[channel]*(1-(level-first))+next[channel]*(level-first);}
                    const float alpha=texel[3]*128.f/255;
                    for(unsigned c=0;c<3;++c)source[c]=batch.texture.function==1?texel[c]:texel[c]*source[c]/128+(batch.texture.function>=2?source[3]:0);
                    source[3]=batch.texture.function==0?alpha*source[3]/128:batch.texture.function==2?alpha+source[3]:alpha;
                }
                auto write=style.write;bool depth_write=style.depth_write;
                if(style.enabled[6] && !compare(style.alpha_test,source[3],float(style.alpha_reference))){
                    if(style.alpha_fail==0)continue;
                    if(style.alpha_fail==1)depth_write=false;
                    if(style.alpha_fail==2)write.fill(false);
                    if(style.alpha_fail==3){depth_write=false;write[3]=false;}
                }
                if(style.enabled[7] && int(destination[3]>=128)!=style.destination_alpha)continue;
                if(style.enabled[8]){const bool pass=style.depth_test==1 || (style.depth_test==2 && z>=depth[pixel]) || (style.depth_test==3 && z>depth[pixel]);if(!pass)continue;}
                for(unsigned c=0;c<3;++c)if(write[c]){
                    float value=source[c];
                    if(style.enabled[3]){const auto color=[&](int selector){return selector==0?source[c]:selector==1?float(destination[c]):0.f;};const auto factor=style.blend[2]==0?source[3]:style.blend[2]==1?destination[3]/2.f:float(style.blend[4]);value=(color(style.blend[0])-color(style.blend[1]))*factor/128+color(style.blend[3]);}
                    destination[c]=std::uint8_t(std::clamp(std::trunc(value),0.f,255.f));
                }
                if(write[3])destination[3]=std::uint8_t(std::clamp(source[3]*2,0.f,255.f));if(depth_write)depth[pixel]=z;
            }
        }
    }
}
}


#include "script/drawing.hpp"
#include "game/transform_motion.hpp"
#include <numbers>

namespace lifeline::script {
namespace {
math::Matrix4 perspective(float fov,float aspect,float near,float far){
    const float y=1/std::tan(fov*.5f);
    if(!std::isfinite(y)||!std::isfinite(aspect)||aspect==0||near==far)throw std::runtime_error("Invalid drawing perspective");
    return {{math::Vector4{y/aspect,0,0,0},{0,y,0,0},{0,0,-(far+near)/(far-near),-1},{0,0,-2*far*near/(far-near),0}}};
}
}
Bindings Drawing::matrix_bindings(){
    Bindings out;
    out.emplace(953,Binding{4,0,[this](const Invocation& c){
        for(unsigned i=0;i<4;++i){const float value=std::nearbyint(c.arguments[i]);if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>INT32_MAX)throw std::runtime_error("Invalid drawing viewport");viewport_[i]=value;}
        // Script viewports are GS pixel coordinates, just like projection-1
        // vertices (whose 12.4 offsets are removed in finish). Normalize to
        // the native 640x448 framebuffer before rasterizing projected draws.
        // Camera binding 979 already supplies a native viewport separately.
        viewport_[0]-=27648.f/16;viewport_[1]-=29184.f/16;
        return Reply{false,0.f};
    }});
    out.emplace(965,Binding{3,0,[this](const Invocation& c){auto& matrix=matrices_[matrix_mode_].current;matrix.columns[3]=math::transform(matrix,{c.arguments[0],c.arguments[1],c.arguments[2],1});return Reply{false,0.f};}});
    out.emplace(967,Binding{3,0,[this](const Invocation& c){auto& matrix=matrices_[matrix_mode_].current;for(unsigned axis=0;axis<3;++axis)matrix.columns[axis]=matrix.columns[axis]*c.arguments[axis];return Reply{false,0.f};}});
    for(unsigned axis=0;axis<3;++axis)out.emplace(962+axis,Binding{1,0,[this,axis](const Invocation& c){
        // 10c498/10c620/10c7a8 quantize radians into 16-bit turns.
        const float turns=c.arguments[0]*0.15915493667125702f*65536;
        if(!std::isfinite(turns))throw std::runtime_error("Invalid drawing rotation");
        const float angle=std::fmod(std::nearbyint(turns),65536.f)*(2*std::numbers::pi_v<float>/65536);
        TransformMotion rotation;rotation.rotation=axis==0?math::Vector4{angle,0,0,0}:axis==1?math::Vector4{0,angle,0,0}:math::Vector4{0,0,angle,0};
        auto& matrix=matrices_[matrix_mode_].current;matrix=math::multiply(matrix,rotation.matrix());return Reply{false,0.f};
    }});
    out.emplace(971,Binding{4,0,[this](const Invocation& c){auto& matrix=matrices_[matrix_mode_].current;matrix=math::multiply(matrix,perspective(c.arguments[0],c.arguments[1],c.arguments[2],c.arguments[3]));return Reply{false,0.f};}});
    out.emplace(979,Binding{0,0,[this](const Invocation&){
        // 132830 rebuilds projection and model-view from the live camera, then
        // leaves matrix mode zero selected. It does not alter screen draw mode.
        if(!camera_)throw std::runtime_error("Drawing camera is not connected");const auto camera=camera_();
        const auto forward=math::normalize_xyz(camera.target+camera.eye*-1),right=math::normalize_xyz(math::cross_xyz(forward,camera.up)),up=math::cross_xyz(right,forward);
        auto& view=matrices_[0].current;
        view={{math::Vector4{right.x,up.x,-forward.x,0},{right.y,up.y,-forward.y,0},{right.z,up.z,-forward.z,0},{-math::dot_xyz(right,camera.eye),-math::dot_xyz(up,camera.eye),math::dot_xyz(forward,camera.eye),1}}};
        // Express the camera projection in native 640x448 pixel coordinates.
        const float focal=camera.focal_pixels,near=camera.near_plane,far=camera.far_plane;
        matrices_[1].current={{math::Vector4{focal/320,0,0,0},{0,-focal/224,0,0},{0,0,std::isinf(far)?-1.f:-(far+near)/(far-near),-1},{0,0,std::isinf(far)?-2*near:-2*far*near/(far-near),0}}};
        viewport_={0,0,640,448};matrix_mode_=0;return Reply{false,0.f};
    }});
    return out;
}
}


#include "script/drawing.hpp"
#include <algorithm>

namespace lifeline::script {
namespace {
unsigned texture_integer(float value){if(!std::isfinite(value)||value<0||value>16777216)throw std::runtime_error("Invalid texture parameter");return unsigned(std::nearbyint(value));}
std::array<unsigned char,4> color(std::span<const unsigned char> bytes,std::size_t index,unsigned format){
    const unsigned size=format==0?4:format==1?3:2;const auto offset=index*size;
    if(offset+size>bytes.size())throw std::runtime_error("Texture color exceeds installed data");
    if(format<=1)return {bytes[offset],bytes[offset+1],bytes[offset+2],format==0?static_cast<unsigned char>(std::min(255u,unsigned(bytes[offset+3])*2)):static_cast<unsigned char>(255)};
    const unsigned packed=bytes[offset]|unsigned(bytes[offset+1])<<8;
    return {static_cast<unsigned char>((packed&31)*255/31),static_cast<unsigned char>(((packed>>5)&31)*255/31),static_cast<unsigned char>(((packed>>10)&31)*255/31),static_cast<unsigned char>(packed&32768?255:0)};
}
}
AssetImage Drawing::upload(unsigned pointer,unsigned width,unsigned height,unsigned format)const{
    if(!width||!height||width>4096||height>4096)throw std::runtime_error("Invalid drawing texture dimensions");
    if(!resource_)throw std::runtime_error("Texture resource reader is not connected");
    const auto bytes=resource_(pointer);AssetImage image{"script texture",width,height,Bytes(std::size_t(width)*height*4)};
    for(std::size_t pixel=0;pixel<std::size_t(width)*height;++pixel){
        std::array<unsigned char,4> rgba;
        if(format==19||format==20){
            const auto index=format==19?pixel:pixel/2;if(index>=bytes.size())throw std::runtime_error("Indexed texture exceeds installed data");
            const unsigned entry=format==19?bytes[index]:(bytes[index]>>((pixel&1)*4))&15;
            if(palette_<0||entry>=palettes_[palette_].size())throw std::runtime_error("Texture palette is not uploaded");rgba=palettes_[palette_][entry];
        }else if(format==0||format==1||format==2||format==10)rgba=color(bytes,pixel,format);
        else throw std::runtime_error("Invalid drawing pixel format");
        std::copy(rgba.begin(),rgba.end(),image.rgba.begin()+pixel*4);
    }
    return image;
}
Bindings Drawing::texture_bindings(){
    Bindings out;
    out.emplace(934,Binding{7,0,[this](const Invocation& c){
        if(texture_integer(c.arguments[0])!=1||texture_<0)return Reply{false,0.f};
        const auto level=texture_integer(c.arguments[1]),width=texture_integer(c.arguments[2]),height=texture_integer(c.arguments[3]);
        if(level>12)throw std::runtime_error("Texture mip level outside image range");
        const auto w=width>>level,h=height>>level;if(w<8||h<8)return Reply{false,0.f};
        auto image=c.arguments[6]==0?AssetImage{"script texture",w,h,Bytes(std::size_t(w)*h*4)}:upload(texture_integer(c.arguments[6]),w,h,texture_integer(c.arguments[4]));
        if(level>=textures_[texture_].mipmaps.size())throw std::runtime_error("Texture mip level outside original range");
        auto& texture=textures_[texture_];texture.mipmaps[level]=std::make_shared<AssetImage>(std::move(image));
        if(level==0){texture.source=0;texture.image=texture.mipmaps[0];}return Reply{false,0.f};
    }});
    out.emplace(938,Binding{3,0,[this](const Invocation& c){
        if(texture_<0)return Reply{false,0.f};auto& texture=textures_[texture_];
        for(float value:c.arguments)if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>INT32_MAX)throw std::runtime_error("Invalid texture LOD parameter");
        texture.fixed_lod=(int(std::nearbyint(c.arguments[0]))&1)!=0;texture.lod_scale=unsigned(int(std::nearbyint(c.arguments[1])))&3;
        const int packed=int(std::nearbyint(c.arguments[2]))&4095;texture.lod_bias=float(packed>=2048?packed-4096:packed)/16;
        return Reply{false,0.f};
    }});
    out.emplace(977,Binding{1,0,[this](const Invocation& c){const auto index=texture_integer(c.arguments[0]);if(index<32)palette_=int(index);return Reply{false,0.f};}});
    out.emplace(978,Binding{3,0,[this](const Invocation& c){
        if(palette_<0)return Reply{false,0.f};if(!resource_)throw std::runtime_error("Palette resource reader is not connected");
        const auto count=texture_integer(c.arguments[0]),format=texture_integer(c.arguments[1]);if(count!=16&&count!=256)throw std::runtime_error("Invalid palette size");
        if(format!=0&&format!=2&&format!=10)throw std::runtime_error("Invalid palette format");
        const auto bytes=resource_(texture_integer(c.arguments[2]));auto& palette=palettes_[palette_];palette.resize(count);
        for(unsigned i=0;i<count;++i)palette[i]=color(bytes,i,format);return Reply{false,0.f};
    }});
    return out;
}
}

