#include "ui/title_exit.hpp"
#include "assets/asset_directory.hpp"
#include "ui/geometry_batch.hpp"
#include <SDL3/SDL.h>
#include <cmath>

namespace lifeline {
namespace {
void check(bool ok){if(!ok)throw std::runtime_error(std::string("Title menu: ")+SDL_GetError());}
using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
}
struct TitleExit::Impl {
    SDL_Renderer* renderer;
    FlashMovie cursor_movie;
    FlashPlayer cursor;
    std::map<unsigned,Texture> cursor_textures;
    Texture letters{nullptr,SDL_DestroyTexture};
    struct Glyph {char c;unsigned texture,x,y,width;};
    // Keep the authored title's soft white glyphs, native size and tracking.
    static constexpr Glyph glyphs[]{
        {'Q',24,6,9,14},{'U',26,59,9,13},{'I',24,53,9,4},
        {'T',24,38,9,12},{'G',20,63,7,14},{'A',20,81,7,13},
        {'M',20,97,7,14},{'E',20,114,7,12},{'N',20,5,7,13},
        {'O',24,6,9,14},{'Y',0,0,0,13},{'S',26,75,9,14}};
    Uint64 tick=0;
    Texture texture(const AssetImage& image){
        Texture t(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);check(bool(t));
        check(SDL_UpdateTexture(t.get(),nullptr,image.rgba.data(),int(image.width*4)));
        check(SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND));check(SDL_SetTextureScaleMode(t.get(),SDL_SCALEMODE_LINEAR));return t;
    }
    Impl(const AssetDirectory& assets,SDL_Renderer* r):renderer(r),cursor_movie(assets.read("ui/title/tit_loadcur.cf"),"tit_loadcur.cf"),cursor(cursor_movie){
        const FlashMovie title(assets.read("ui/title/tit_load.cf"),"tit_load.cf");
        constexpr unsigned atlas_width=unsigned(std::size(glyphs))*16;
        AssetImage atlas{"native title letters",atlas_width,20,Bytes(atlas_width*20*4)};
        for(unsigned i=0;i<std::size(glyphs);++i){
            const auto& g=glyphs[i];if(!g.texture)continue;const auto& source=title.textures.at(g.texture);
            for(unsigned y=0;y<15;++y)for(unsigned x=0;x<g.width;++x)
                std::copy_n(source.rgba.data()+((g.y+y)*source.width+g.x+x)*4,4,atlas.rgba.data()+(y*atlas_width+i*16+x)*4);
        }
        // Q extends the existing O with a short, softly edged diagonal tail.
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<4;++x){
            auto* p=atlas.rgba.data()+((10+y)*atlas_width+8+y/2+x)*4;
            p[0]=p[1]=p[2]=240;p[3]=std::max<unsigned char>(p[3],x==0||x==3?64:224);
        }
        // Match the existing soft white title letters for the new Y, as with Q.
        const auto distance=[](float x,float y,float ax,float ay,float bx,float by){const float dx=bx-ax,dy=by-ay,t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);return std::hypot(x-ax-t*dx,y-ay-t*dy);};
        for(unsigned y=0;y<15;++y)for(unsigned x=0;x<13;++x){
            unsigned coverage=0;for(unsigned v=0;v<4;++v)for(unsigned u=0;u<4;++u){const float px=x+(u+.5f)/4,py=y+(v+.5f)/4;if(std::min({distance(px,py,1.2f,.9f,6.4f,7.2f),distance(px,py,11.6f,.9f,6.4f,7.2f),distance(px,py,6.4f,7.2f,6.4f,13.9f)})<1.15f)++coverage;}
            auto* p=atlas.rgba.data()+(y*atlas_width+10*16+x)*4;p[0]=p[1]=p[2]=240;p[3]=static_cast<unsigned char>((coverage*255+8)/16);
        }
        letters=texture(atlas);
        for(const auto& [id,image]:cursor_movie.textures)cursor_textures.emplace(id,texture(image));
        cursor.seek(0);
    }
    void text(std::string_view value,float x,float y){
        float width=-4;for(char c:value){if(c==' '){width+=8;continue;}for(const auto& g:glyphs)if(g.c==c){width+=g.width+4;break;}}
        x-=width*.5f;
        for(char c:value){
            if(c==' '){x+=8;continue;}
            for(unsigned i=0;i<std::size(glyphs);++i)if(glyphs[i].c==c){
                const auto& g=glyphs[i];const SDL_FRect source{float(i*16),0,float(g.width),20},dest{x,y-7.5f,float(g.width),20};
                check(SDL_RenderTexture(renderer,letters.get(),&source,&dest));x+=g.width+4;break;
            }
        }
    }
    void highlight(float y){
        const auto now=SDL_GetTicks()*60/1000;if(!tick)tick=now;
        for(unsigned n=0;n<std::min<Uint64>(now-tick,4);++n)cursor.advance();tick=now;
        GeometryBatch geometry(renderer);
        for(const auto& draw:cursor.compose())geometry.append(draw.textured?cursor_textures.at(draw.texture).get():nullptr,draw,[&](const FlashVertex& v){return SDL_FPoint{v.x,v.y+y-304.5f};});
        geometry.flush();
    }
};
TitleExit::TitleExit(const AssetDirectory& a,SDL_Renderer* r):impl_(std::make_unique<Impl>(a,r)){}
TitleExit::~TitleExit()=default;
void TitleExit::active(bool value,unsigned initial_selection){
    if(active_==value)return;active_=value;
    selection_=std::min(initial_selection,4u);dialog_=accept_=committing_=back_=false;original_request_.reset();
}
bool TitleExit::action(MenuAction action){
    if(!active_)return false;
    if(committing_)return true;
    if(dialog_){
        if(action==MenuAction::back||action==MenuAction::escape){dialog_=false;accept_=false;}
        else if(action==MenuAction::up||action==MenuAction::left)accept_=true;
        else if(action==MenuAction::down||action==MenuAction::right)accept_=false;
        else if(action==MenuAction::confirm){if(accept_)requested_=true;else dialog_=false;}
    }else if(action==MenuAction::up){if(selection_)--selection_;}
    else if(action==MenuAction::down){if(selection_<4)++selection_;}
    else if(action==MenuAction::back||action==MenuAction::escape){back_=committing_=true;}
    else if(action==MenuAction::confirm){
        if(selection_==4){dialog_=true;accept_=false;}
        else{original_request_=selection_;committing_=true;}
    }
    return true;
}
void TitleExit::render(){
    if(!active_||!dialog_)return;auto& d=*impl_;
    check(SDL_SetRenderDrawBlendMode(d.renderer,SDL_BLENDMODE_BLEND));check(SDL_SetRenderDrawColor(d.renderer,0,0,0,210));check(SDL_RenderFillRect(d.renderer,nullptr));
    const SDL_FRect panel{160,238,320,119};check(SDL_SetRenderDrawColor(d.renderer,0,0,0,255));check(SDL_RenderFillRect(d.renderer,&panel));
    d.text("QUIT GAME",320,258);d.text("YES",320,304.5f);d.text("NO",320,335);
    d.highlight(accept_?304.5f:335);
}
}
