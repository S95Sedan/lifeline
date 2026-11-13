#pragma once
#include "script/runtime.hpp"
#include "rendering/image.hpp"
#include "core/math.hpp"
#include "rendering/model.hpp"
#include <array>

namespace lifeline::script {
class Drawing {
    struct Vertex {float x=0,y=0,z=0,u=0,v=0;std::array<float,4> color{128,128,128,128};float q=1;};
    struct Style {
        std::array<bool,11> enabled{true,false,false,false,false,false,false,false,false,false,false};
        std::array<int,5> blend{0,1,0,1,0};
        std::array<bool,4> write{true,true,true,true};
        int alpha_test=0,alpha_reference=0,alpha_fail=0,depth_test=0,destination_alpha=0;
        bool depth_write=true;
        int front_face=0,cull_face=0;
    } style_;
    struct Texture {
        std::shared_ptr<const AssetImage> image;
        unsigned source=0,width=0,height=0,format=0;
        std::array<std::shared_ptr<const AssetImage>,7> mipmaps;
        bool fixed_lod=false;
        unsigned lod_scale=0;
        float lod_bias=0;
        int function=0,min_filter=3,mag_filter=1,wrap_u=0,wrap_v=0;
        std::array<int,4> region{};
    };
    struct Batch {unsigned primitive;int order;Style style;std::vector<Vertex> vertices;Texture texture;bool world=false;};
    std::array<Texture,32> textures_;
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
    Vertex current_;
    int texture_=-1,projection_=0,coordinates_=3;
    unsigned primitive_=0;
    int order_=4;
    int presentation_mode_=0;
    struct MatrixStack {math::Matrix4 current=math::identity();std::vector<math::Matrix4> saved;};
    std::array<MatrixStack,3> matrices_;
    unsigned matrix_mode_=0;
    std::function<SceneCamera()> camera_;
    std::function<std::span<const unsigned char>(unsigned)> resource_;
    int palette_=-1;
    std::array<std::vector<std::array<unsigned char,4>>,32> palettes_;
    std::array<float,4> viewport_{0,0,640,448};
    Bindings matrix_bindings();
    Bindings texture_bindings();
    AssetImage upload(unsigned pointer,unsigned width,unsigned height,unsigned format)const;
    std::function<const AssetImage&(unsigned)> image_;
    void finish();
public:
    int presentation_mode()const{return presentation_mode_;}
    explicit Drawing(std::function<const AssetImage&(unsigned)> image):image_(std::move(image)){}
    void camera(std::function<SceneCamera()> camera){camera_=std::move(camera);}
    void resources(std::function<std::span<const unsigned char>(unsigned)> resource){resource_=std::move(resource);}
    Bindings bindings();
    void begin_frame(){batches_.clear();}
    void append_world(StaticModel&,const SceneCamera&)const;
    void render(AssetImage&,bool world_submitted=false)const;
};
}
