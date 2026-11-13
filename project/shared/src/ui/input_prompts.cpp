#include "ui/input_prompts.hpp"
#include "platform/bindings.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace lifeline {
namespace {
constexpr unsigned cell=40,columns=24,prompt_y=160;
constexpr float letter_scale=14.f/34.f;
constexpr float letter_width=letter_scale*1.35f;
void quad(FlashDraw& draw,const AssetImage& atlas,float x,float y,float w,float h,float u,float v,float uw,float vh){
    const int first=int(draw.vertices.size());
    draw.vertices.insert(draw.vertices.end(),{{x,y,u/atlas.width,v/atlas.height,{1,1,1,1}},{x+w,y,(u+uw)/atlas.width,v/atlas.height,{1,1,1,1}},{x+w,y+h,(u+uw)/atlas.width,(v+vh)/atlas.height,{1,1,1,1}},{x,y+h,u/atlas.width,(v+vh)/atlas.height,{1,1,1,1}}});
    draw.indices.insert(draw.indices.end(),{first,first+1,first+2,first,first+2,first+3});
}
FlashDraw title_prompt(const FlashMovie& movie){
    const auto id=*movie.input_prompt_font;const auto& font=movie.fonts.at(id);const auto& atlas=movie.textures.at(id);
    const std::string key="any";
    float width=0;for(unsigned char c:key){const auto i=std::find(font.characters.begin(),font.characters.end(),c);width+=c==' '?6.f:i==font.characters.end()?10.f:font.advances.at(i-font.characters.begin())*48*letter_width;}
    const float middle=width+12,total=154+middle,scale=std::min(1.f,216/total),left=-total*.5f;
    FlashDraw draw;draw.textured=true;draw.texture=id;
    // Preserve the authored "press" and "button" pixels; replace only START.
    quad(draw,atlas,left,-24,74,48,0,prompt_y,74,48);
    float x=left+80-6;
    for(unsigned char c:key){
        if(c==' '){x+=6;continue;}
        auto it=std::find(font.characters.begin(),font.characters.end(),c);if(it==font.characters.end())it=std::find(font.characters.begin(),font.characters.end(),unsigned('?'));
        const auto i=unsigned(it-font.characters.begin());
        quad(draw,atlas,x,-16,cell,cell,float(i%columns*cell),float(i/columns*cell),cell,cell);
        x+=font.advances.at(i)*48*letter_width;
    }
    quad(draw,atlas,left+74+middle,-24,80,48,136,prompt_y,80,48);
    for(auto& v:draw.vertices){v.x*=scale;v.y*=scale;}
    return draw;
}
FlashDraw prompt_label(const FlashMovie& movie,unsigned id,math::Vector4 bounds,bool right_aligned,const std::string& label){
    const auto& font=movie.fonts.at(id);const auto& atlas=movie.textures.at(id);
    constexpr float size=24;
    const auto advance=[&](unsigned c){
        if(c==' ')return size*.25f;
        auto i=std::find(font.characters.begin(),font.characters.end(),c);if(i==font.characters.end())i=std::find(font.characters.begin(),font.characters.end(),unsigned('?'));
        return font.advances.at(i-font.characters.begin())*size;
    };
    float width=0;for(unsigned char c:label)width+=advance(c);
    width+=size-advance(static_cast<unsigned char>(label.back()));
    const float scale=std::min(1.f,(bounds.z-bounds.x)/width),height=size*scale;
    float x=right_aligned?bounds.z-width*scale:(bounds.x+bounds.z-width*scale)*.5f;const float y=(bounds.y+bounds.w-height)*.5f;
    FlashDraw draw;draw.textured=true;draw.texture=id;bool key=false;
    for(unsigned char c:label){
        if(c==' '){x+=advance(c)*scale;continue;}
        if(c=='[')key=true;
        auto i=std::find(font.characters.begin(),font.characters.end(),c);if(i==font.characters.end())i=std::find(font.characters.begin(),font.characters.end(),unsigned('?'));
        const auto index=unsigned(i-font.characters.begin());
        quad(draw,atlas,x,y,height,height,float(index%font.columns)*font.u_step*atlas.width,float(index/font.columns)*font.v_step*atlas.height,font.u_step*atlas.width-1,font.v_step*atlas.height);
        const math::Vector4 color=key?math::Vector4{244.f/255,131.f/255,231.f/255,254.f/255}:math::Vector4{0,230.f/255,200.f/255,254.f/255};
        for(auto v=draw.vertices.end()-4;v!=draw.vertices.end();++v)v->color=color;
        if(c==']')key=false;
        x+=advance(c)*scale;
    }
    return draw;
}
FlashDraw continue_prompt(const FlashMovie& movie,unsigned id,math::Vector4 bounds,bool right_aligned){
    const auto binding=input_prompt(InputAction::confirm);
    return prompt_label(movie,id,bounds,right_aligned,binding=="Unbound"?"set Confirm in Controls":"press ["+binding+"]");
}
void copy_prompt_font(FlashMovie& movie,const FlashMovie& source){
    if(movie.textures.empty())throw std::runtime_error("Prompt movie has no texture atlas");
    const auto font=source.texts.at(5).font;
    const auto id=movie.textures.rbegin()->first+1;
    movie.fonts.emplace(id,source.fonts.at(font));movie.textures.emplace(id,source.textures.at(font));movie.input_prompt_font=id;
}
}
FlashDraw movie_skip_prompt(const FlashMovie& source,math::Vector4 bounds){
    const auto binding=input_prompt(InputAction::confirm);
    return prompt_label(source,source.texts.at(5).font,bounds,true,binding=="Unbound"?"Set Confirm in Controls":"Hold ["+binding+"] to skip");
}
void prepare_continue_prompt(FlashMovie& movie,const FlashMovie& source){
    if(movie.input_prompt_font)return;
    const auto slash=movie.name.find_last_of("/\\");if(std::string_view(movie.name).substr(slash==std::string::npos?0:slash+1)!="display2.cf")return;
    if(!movie.textures.contains(62)||movie.textures.at(62).width!=352||movie.textures.at(62).height!=32)return;
    copy_prompt_font(movie,source);
}
void prepare_navigation_prompt(FlashMovie& movie,const FlashMovie& source){
    if(movie.input_prompt_font)return;
    const auto slash=movie.name.find_last_of("/\\");if(std::string_view(movie.name).substr(slash==std::string::npos?0:slash+1)!="inputinfo.cf")return;
    const auto shape=movie.shapes.find(2);if(shape==movie.shapes.end())return;
    const auto b=shape->second.bounds;if(b.x!=-312||b.y!=-16||b.z!=312||b.w!=16)return;
    if(!movie.textures.contains(1)||movie.textures.at(1).width!=624||movie.textures.at(1).height!=32)return;
    copy_prompt_font(movie,source);
}
void prepare_title_prompt(FlashMovie& movie,const FlashMovie& source){
    if(movie.input_prompt_font)return;
    const auto title=movie.shapes.find(15);
    if(title==movie.shapes.end()||title->second.bounds.x!=-108||title->second.bounds.w!=24||!movie.textures.contains(14))return;
    const auto& original=movie.textures.at(14);if(original.width!=216||original.height!=48)return;
    const auto& font=source.fonts.at(source.texts.at(7).font);const auto& image=source.textures.at(source.texts.at(7).font);
    if(font.characters.size()>96||image.width*font.u_step!=48||image.height*font.v_step!=48)throw std::runtime_error("Unsupported title prompt font");
    AssetImage atlas{"native title prompt",1024,256,Bytes(1024*256*4)};
    std::array<float,cell*cell> mask{},blur{},horizontal{};
    std::array<float,11> kernel{};float sum=0;for(int k=-5;k<=5;++k){kernel[k+5]=std::exp(-float(k*k)/8);sum+=kernel[k+5];}for(auto& k:kernel)k/=sum;
    for(unsigned i=0;i<font.characters.size();++i){
        mask.fill(0);const unsigned sx=i%font.columns*48,sy=i/font.columns*48;
        // Filter at the title's native size, so small keys retain the bitmap's
        // soft edges instead of undersampling the much larger body-font atlas.
        for(unsigned y=0;y<cell;++y)for(unsigned x=0;x<cell;++x){
            const float x0=(float(x)-6)/letter_width,x1=(float(x)+1-6)/letter_width,y0=(float(y)-7)/letter_scale,y1=(float(y)+1-7)/letter_scale;float alpha=0;
            for(int v=std::max(0,int(std::floor(y0)));v<std::min(48,int(std::ceil(y1)));++v)for(int u=std::max(0,int(std::floor(x0)));u<std::min(48,int(std::ceil(x1)));++u)
                alpha+=image.rgba[((sy+v)*image.width+sx+u)*4+3]*std::max(0.f,std::min(x1,float(u+1))-std::max(x0,float(u)))*std::max(0.f,std::min(y1,float(v+1))-std::max(y0,float(v)));
            mask[y*cell+x]=alpha*letter_width*letter_scale/255;
        }
        for(int y=0;y<int(cell);++y)for(int x=0;x<int(cell);++x){float value=0;for(int k=-5;k<=5;++k)if(x+k>=0&&x+k<int(cell))value+=mask[y*cell+x+k]*kernel[k+5];horizontal[y*cell+x]=value;}
        for(int y=0;y<int(cell);++y)for(int x=0;x<int(cell);++x){float value=0;for(int k=-5;k<=5;++k)if(y+k>=0&&y+k<int(cell))value+=horizontal[(y+k)*cell+x]*kernel[k+5];blur[y*cell+x]=value;}
        for(unsigned y=0;y<cell;++y)for(unsigned x=0;x<cell;++x){
            const float ink=mask[y*cell+x],glow=std::min(1.f,blur[y*cell+x]*2),alpha=ink+glow*(1-ink);
            auto* p=atlas.rgba.data()+((i/columns*cell+y)*atlas.width+i%columns*cell+x)*4;
            if(alpha>0){p[0]=static_cast<unsigned char>((ink*248+glow*(1-ink)*36)/alpha);p[1]=static_cast<unsigned char>((ink*252+glow*(1-ink)*201)/alpha);p[2]=static_cast<unsigned char>((ink*252+glow*(1-ink)*217)/alpha);p[3]=static_cast<unsigned char>(std::lround(alpha*254));}
        }
    }
    for(unsigned y=0;y<48;++y)std::copy_n(original.rgba.data()+y*216*4,216*4,atlas.rgba.data()+((prompt_y+y)*atlas.width)*4);
    auto native_font=font;native_font.columns=columns;native_font.u_step=float(cell)/atlas.width;native_font.v_step=float(cell)/atlas.height;
    const auto id=movie.textures.rbegin()->first+1;movie.fonts.emplace(id,std::move(native_font));movie.textures.emplace(id,std::move(atlas));movie.input_prompt_font=id;
}
std::optional<FlashDraw> input_prompt_shape(const FlashMovie& movie,unsigned shape){
    const auto slash=movie.name.find_last_of("/\\");const auto name=std::string_view(movie.name).substr(slash==std::string::npos?0:slash+1);
    if(name!="tit_load.cf"&&name!="inputinfo.cf"&&name!="display2.cf"&&name!="msg.cf")return {};
    const auto found=movie.shapes.find(shape);if(found==movie.shapes.end())return {};
    const auto bounds=found->second.bounds;
    if(name=="msg.cf"&&shape==7&&bounds.x==-80&&bounds.y==-16&&bounds.z==80&&bounds.w==16&&movie.texts.contains(5)&&movie.texts.at(5).variable=="text_tu0"&&movie.textures.contains(6)&&movie.textures.at(6).width==160&&movie.textures.at(6).height==32)return continue_prompt(movie,movie.texts.at(5).font,bounds,true);
    if(name=="tit_load.cf"&&shape==15&&bounds.x==-108&&bounds.y==-24&&bounds.z==108&&bounds.w==24&&movie.input_prompt_font)return title_prompt(movie);
    if(name=="inputinfo.cf"&&shape==2&&bounds.x==-312&&bounds.y==-16&&bounds.z==312&&bounds.w==16&&movie.input_prompt_font)return prompt_label(movie,*movie.input_prompt_font,{-308,-16,308,16},false,"["+navigation_prompt()+"]: Select   ["+input_prompt(InputAction::confirm)+"]: OK   ["+input_prompt(InputAction::back)+"]: Back");
    if(name=="display2.cf"&&movie.textures.contains(62)&&movie.textures.at(62).width==352&&movie.textures.at(62).height==32){
        for(const auto& fill:found->second.fills)if(fill.textured&&fill.texture==62){if(movie.input_prompt_font)return continue_prompt(movie,*movie.input_prompt_font,bounds,false);return {};}
    }
    return {};
}
std::string input_prompt_text(std::string_view variable,const std::string& source){
    if(variable!="text_tu0")return source;
    std::string result=source;const auto key="["+input_prompt(InputAction::speak)+"]";
    if(key=="[Unbound]"&&(result.find('|')!=std::string::npos||result.starts_with("@ph28Use the ")))return "@ph28Set Push to talk in Options > Controls\nbefore speaking.";
    if(result.starts_with("@ph28Use the "))return "@ph28Hold "+key+" while speaking, then release\nto send your command.";
    const auto controller=result.find("@c80301850Explanation");
    if(controller!=std::string::npos){result.resize(controller);result+="@ph26Controls follow your saved bindings.\nYou can change them in Options > Controls.";}
    for(std::size_t p=0;(p=result.find('|',p))!=std::string::npos;p+=key.size())result.replace(p,1,key);
    return result;
}
}
