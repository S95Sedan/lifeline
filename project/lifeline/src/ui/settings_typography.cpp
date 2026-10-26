#include "ui/settings_typography.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace lifeline {
namespace {
struct Point {float x,y;};
using Path=std::vector<Point>;
using Letter=std::vector<Path>;

// A small geometric alphabet for native PC menu captions. The square stems
// and chamfered corners follow the pause labels; body text retains its CF font.
Letter strokes(unsigned c){
    switch(c){
    case 'A':return {{{0,14},{0,3},{3,0},{7,0},{10,3},{10,14}},{{0,7},{10,7}}};
    case 'B':return {{{0,14},{0,0},{7,0},{10,3},{10,4},{7,7},{0,7}},{{7,7},{10,10},{10,11},{7,14},{0,14}}};
    case 'C':return {{{10,3},{7,0},{3,0},{0,3},{0,11},{3,14},{7,14},{10,11}}};
    case 'D':return {{{0,0},{7,0},{10,3},{10,11},{7,14},{0,14},{0,0}}};
    case 'E':return {{{10,0},{0,0},{0,14},{10,14}},{{0,7},{9,7}}};
    case 'F':return {{{10,0},{0,0},{0,14}},{{0,7},{9,7}}};
    case 'G':return {{{10,3},{7,0},{3,0},{0,3},{0,11},{3,14},{7,14},{10,11},{10,7},{6,7}}};
    case 'H':return {{{0,0},{0,14}},{{10,0},{10,14}},{{0,7},{10,7}}};
    case 'I':return {{{0,0},{10,0}},{{5,0},{5,14}},{{0,14},{10,14}}};
    case 'J':return {{{10,0},{10,11},{7,14},{3,14},{0,11},{0,9}}};
    case 'K':return {{{0,0},{0,14}},{{10,0},{0,8},{10,14}}};
    case 'L':return {{{0,0},{0,14},{10,14}}};
    case 'M':return {{{0,14},{0,0},{5,7},{10,0},{10,14}}};
    case 'N':return {{{0,14},{0,0},{10,14},{10,0}}};
    case 'O':case '0':return {{{3,0},{7,0},{10,3},{10,11},{7,14},{3,14},{0,11},{0,3},{3,0}}};
    case 'P':return {{{0,14},{0,0},{7,0},{10,3},{10,4},{7,7},{0,7}}};
    case 'Q':return {{{3,0},{7,0},{10,3},{10,11},{7,14},{3,14},{0,11},{0,3},{3,0}},{{6,10},{10,14}}};
    case 'R':return {{{0,14},{0,0},{7,0},{10,3},{10,4},{7,7},{0,7}},{{5,7},{10,14}}};
    case 'S':return {{{10,3},{7,0},{3,0},{0,3},{0,4},{3,7},{7,7},{10,10},{10,11},{7,14},{3,14},{0,11}}};
    case 'T':return {{{0,0},{10,0}},{{5,0},{5,14}}};
    case 'U':return {{{0,0},{0,11},{3,14},{7,14},{10,11},{10,0}}};
    case 'V':return {{{0,0},{5,14},{10,0}}};
    case 'W':return {{{0,0},{2,14},{5,7},{8,14},{10,0}}};
    case 'X':case 'x':return {{{0,0},{10,14}},{{10,0},{0,14}}};
    case 'Y':return {{{0,0},{5,7},{10,0}},{{5,7},{5,14}}};
    case 'Z':return {{{0,0},{10,0},{0,14},{10,14}}};
    case '1':return {{{0,4},{5,0},{5,14}},{{0,14},{10,14}}};
    case '2':return {{{0,3},{3,0},{7,0},{10,3},{10,4},{0,14},{10,14}}};
    case '3':return {{{0,0},{7,0},{10,3},{10,4},{7,7},{3,7}},{{7,7},{10,10},{10,11},{7,14},{0,14}}};
    case '4':return {{{7,14},{7,0},{0,9},{10,9}}};
    case '5':return {{{10,0},{0,0},{0,7},{7,7},{10,10},{10,11},{7,14},{0,14}}};
    case '6':return {{{10,0},{3,0},{0,3},{0,11},{3,14},{7,14},{10,11},{10,10},{7,7},{0,7}}};
    case '7':return {{{0,0},{10,0},{3,14}}};
    case '8':return {{{3,0},{7,0},{10,3},{10,4},{7,7},{3,7},{0,4},{0,3},{3,0}},{{3,7},{0,10},{0,11},{3,14},{7,14},{10,11},{10,10},{7,7}}};
    case '9':return {{{10,7},{3,7},{0,4},{0,3},{3,0},{7,0},{10,3},{10,11},{7,14},{0,14}}};
    case '<':return {{{10,0},{0,7},{10,14}}};
    case '>':return {{{0,0},{10,7},{0,14}}};
    case '/':return {{{10,0},{0,14}}};
    case '\\':return {{{0,0},{10,14}}};
    case '-':return {{{0,7},{10,7}}};
    case '_':return {{{0,14},{10,14}}};
    case '+':return {{{0,7},{10,7}},{{5,0},{5,14}}};
    case '%':return {{{10,0},{0,14}},{{0,0},{3,0},{3,4},{0,4},{0,0}},{{7,10},{10,10},{10,14},{7,14},{7,10}}};
    case '.':return {{{5,7},{5,7}}};
    case ':':return {{{5,0},{5,0}},{{5,14},{5,14}}};
    case ',':return {{{6,0},{6,7},{0,14}}};
    case ';':return {{{6,0},{6,0}},{{6,7},{6,10},{0,14}}};
    case '\'':return {{{5,0},{5,14}}};
    case '"':return {{{0,0},{0,14}},{{10,0},{10,14}}};
    case '=':return {{{0,0},{10,0}},{{0,14},{10,14}}};
    case '!':return {{{5,0},{5,9}},{{5,14},{5,14}}};
    case '?':return {{{0,3},{3,0},{7,0},{10,3},{10,4},{5,8},{5,10}},{{5,14},{5,14}}};
    case '*':return {{{0,0},{10,14}},{{10,0},{0,14}},{{0,7},{10,7}}};
    case '#':return {{{3,0},{3,14}},{{7,0},{7,14}},{{0,4},{10,4}},{{0,10},{10,10}}};
    case '$':return {{{10,3},{7,1},{3,1},{0,4},{3,7},{7,7},{10,10},{7,13},{3,13},{0,11}},{{5,0},{5,14}}};
    case '&':return {{{10,14},{0,4},{0,2},{3,0},{5,0},{7,2},{7,4},{0,10},{0,12},{3,14},{6,14},{10,7}}};
    case '|':return {{{5,0},{5,14}}};
    case '~':return {{{0,10},{3,4},{7,10},{10,4}}};
    case '{':return {{{10,0},{4,0},{4,5},{0,7},{4,9},{4,14},{10,14}}};
    case '}':return {{{0,0},{6,0},{6,5},{10,7},{6,9},{6,14},{0,14}}};
    case '`':return {{{0,0},{10,14}}};
    case '(':return {{{10,0},{4,3},{0,7},{4,11},{10,14}}};
    case ')':return {{{0,0},{6,3},{10,7},{6,11},{0,14}}};
    case '[':return {{{10,0},{0,0},{0,14},{10,14}}};
    case ']':return {{{0,0},{10,0},{10,14},{0,14}}};
    case '^':return {{{0,14},{5,0},{10,14}}};
    default:return {};
    }
}
float distance(Point p,Point a,Point b){
    const float x=b.x-a.x,y=b.y-a.y,length=x*x+y*y;
    const float t=length?std::clamp(((p.x-a.x)*x+(p.y-a.y)*y)/length,0.f,1.f):0;
    return std::hypot(p.x-a.x-t*x,p.y-a.y-t*y);
}
}
unsigned add_settings_caption_font(FlashMovie& movie,unsigned body_font){
    auto image=movie.textures.at(body_font);const auto& font=movie.fonts.at(body_font);
    const int cell_width=int(std::lround(image.width*font.u_step)),cell_height=int(std::lround(image.height*font.v_step));
    for(unsigned index=0;index<font.characters.size();++index){
        const auto paths=strokes(font.characters[index]);if(paths.empty())continue;
        const int ox=(index%font.columns)*cell_width,oy=(index/font.columns)*cell_height;
        int left=cell_width,top=cell_height,right=0,bottom=0;
        for(int y=0;y<cell_height;++y)for(int x=0;x<cell_width;++x)if(image.rgba[((oy+y)*image.width+ox+x)*4+3]){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
        if(left>=right||top>=bottom)continue;
        const float radius=std::min({.8f,(right-left)*.5f,(bottom-top)*.5f}),sx=std::max(0.f,(right-left-2*radius)/10),sy=std::max(0.f,(bottom-top-2*radius)/14);
        const auto position=[&](Point p){return Point{left+radius+p.x*sx,top+radius+p.y*sy};};
        for(int y=0;y<cell_height;++y)for(int x=0;x<cell_width;++x){
            auto* pixel=image.rgba.data()+((oy+y)*image.width+ox+x)*4;std::fill_n(pixel,4,static_cast<unsigned char>(0));
            if(x<left||x>=right||y<top||y>=bottom)continue;
            // Subpixel coverage smooths the thin strokes at the existing sizes.
            unsigned covered=0;
            for(unsigned v=0;v<4;++v)for(unsigned u=0;u<4;++u){
                const Point p{x+(u+.5f)/4,y+(v+.5f)/4};bool ink=false;
                for(const auto& path:paths)for(unsigned i=1;i<path.size();++i)ink|=distance(p,position(path[i-1]),position(path[i]))<=radius;
                if(ink)++covered;
            }
            pixel[0]=pixel[1]=pixel[2]=255;pixel[3]=static_cast<unsigned char>((covered*255+8)/16);
        }
    }
    const auto id=movie.textures.rbegin()->first+1;image.name="native Settings captions";
    movie.fonts.emplace(id,font);movie.textures.emplace(id,std::move(image));return id;
}
std::string settings_caption(std::string value){
    const auto dimension=value.find(" x ");
    for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));
    if(dimension!=std::string::npos)value[dimension+1]='x';
    return value;
}
FlashDraw caption_geometry(std::string value,math::Vector4 bounds,math::Vector4 color){
    for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));
    float units=0;for(char c:value)units+=c==' '?6.f:12.f;
    const float scale=std::min(bounds.w/14,bounds.z/std::max(1.f,units));
    float x=bounds.x+(bounds.z-units*scale)*.5f;const float y=bounds.y+(bounds.w-14*scale)*.5f;
    FlashDraw result;
    for(unsigned char c:value){
        for(const auto& path:strokes(c))for(unsigned i=1;i<path.size();++i){
            const auto a=path[i-1],b=path[i];const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy),radius=.6f*scale;
            const float nx=length?-dy/length*radius:radius,ny=length?dx/length*radius:0;
            const float ax=x+a.x*scale,ay=y+a.y*scale,bx=x+b.x*scale,by=y+b.y*scale;
            const int base=int(result.vertices.size());
            if(length)result.vertices.insert(result.vertices.end(),{{ax+nx,ay+ny,0,0,color},{bx+nx,by+ny,0,0,color},{bx-nx,by-ny,0,0,color},{ax-nx,ay-ny,0,0,color}});
            else result.vertices.insert(result.vertices.end(),{{ax-radius,ay-radius,0,0,color},{ax+radius,ay-radius,0,0,color},{ax+radius,ay+radius,0,0,color},{ax-radius,ay+radius,0,0,color}});
            result.indices.insert(result.indices.end(),{base,base+1,base+2,base,base+2,base+3});
        }
        x+=(c==' '?6.f:12.f)*scale;
    }
    return result;
}
}
