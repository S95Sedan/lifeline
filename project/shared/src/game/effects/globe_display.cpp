#include "game/effects/effects.hpp"
#include <numbers>

namespace lifeline::script {
std::vector<FlashDraw> Effects::procedural_displays(bool extend_background)const{
    std::vector<FlashDraw> result;
    constexpr float pi=std::numbers::pi_v<float>,focal=554.2562584f;
    for(const auto& slot:slots_)if(slot&&slot->type==10011){
        const float height=slot->parameters[0],phase=slot->parameters[1];
        FlashDraw lines;
        const auto project=[](math::Vector4 point){const float depth=35-point.z;return math::Vector4{320+focal*point.x/depth,224-focal*point.y/depth,0,1};};
        const auto segment=[&](math::Vector4 a,math::Vector4 b,math::Vector4 ca,math::Vector4 cb,int side=0,float boundary=0){
            a=project(a);b=project(b);
            if(side){
                const float da=(a.x-boundary)*side,db=(b.x-boundary)*side;
                if(da<0&&db<0)return;
                if((da<0)!=(db<0)){const float t=da/(da-db);const auto p=a*(1-t)+b*t,color=ca*(1-t)+cb*t;if(da<0){a=p;ca=color;}else{b=p;cb=color;}}
            }
            const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(length==0)return;
            const float x=-dy*.5f/length,y=dx*.5f/length;const int base=int(lines.vertices.size());
            lines.vertices.insert(lines.vertices.end(),{{a.x+x,a.y+y,0,0,ca},{a.x-x,a.y-y,0,0,ca},{b.x+x,b.y+y,0,0,cb},{b.x-x,b.y-y,0,0,cb}});
            lines.indices.insert(lines.indices.end(),{base,base+1,base+2,base+1,base+2,base+3});
        };
        for(unsigned i=0;i<16;++i){const float angle=phase+(i+1)*pi/16;math::Vector4 previous{},previous_color{};
            for(unsigned j=0;j<11;++j){const float latitude=(int(j)-5)*pi/16,s=std::sin(latitude),c=std::cos(latitude),weight=(1-s*s)*height/20;
                const math::Vector4 point{30*c*std::cos(angle),s*height,-30*c*std::sin(angle),1},color{0,.4f*weight,.5f*weight,1};
                if(j)segment(previous,point,previous_color,color);previous=point;previous_color=color;
            }
        }
        for(unsigned i=0;i<11;++i){const float latitude=(i+3)*pi/16,s=std::sin(latitude),c=std::cos(latitude),weight=(1-c*c)*height/20;
            const math::Vector4 color{0,.4f*weight,.5f*weight,1};math::Vector4 previous{};
            for(unsigned j=0;j<18;++j){const float angle=j*pi/16+phase;const math::Vector4 point{30*s*std::cos(angle),c*height,-30*s*std::sin(angle),1};if(j)segment(previous,point,color,color);previous=point;}
            if(extend_background)for(int side:{1,-1}){
                const int start=side==1?0:17;
                const auto point=[&](int j){const float angle=j*pi/16+phase;return math::Vector4{30*s*std::cos(angle),c*height,-30*s*std::sin(angle),1};};
                auto a=point(start);const float boundary=side==1?std::max(640.f,project(a).x):std::min(0.f,project(a).x);
                // Continue the same latitude curve beyond its authored ends.
                // Stop at the sphere silhouette rather than drawing its front
                // across the existing menu. The center is never overwritten.
                for(int step=1;step<=8;++step){const auto b=point(start-side*step);if((project(b).x-project(a).x)*side<=0)break;segment(a,b,color,color,side,boundary);a=b;}
            }
        }
        result.push_back(std::move(lines));
    }
    return result;
}
}
