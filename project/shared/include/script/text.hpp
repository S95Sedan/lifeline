#pragma once
#include "script/runtime.hpp"
#include "ui/font.hpp"

namespace lifeline::script {
class Text {
public:
    struct Window {
        int mode=-1,width=640,height=448,lifetime=-1,revealed=0,delay=0,counter=0;
        float x=0,y=0,z=1,scale=1,spacing=2,line_spacing=2;
        std::array<unsigned,4> background{0,0,0,128};
        std::array<std::array<unsigned,4>,8> colors{{{127,127,127,128},{127,36,36,128},{36,51,127,128},{127,101,36,128},{36,127,41,128},{70,70,70,128},{0,0,0,128},{94,36,127,128}}};
        std::string text,buffer;
        std::size_t cursor=0;
    };
private:
    AssetDirectory assets_;
    std::unique_ptr<Font> font_;
    std::array<Window,30> windows_;
    std::vector<Window> frame_;
    int allocate(int mode);
    Window& get(int);
    static void set_text(Window&,std::string);
public:
    explicit Text(const AssetDirectory& assets):assets_(assets){}
    Bindings bindings();
    void advance();
    void render(AssetImage&);
    const Window& window(unsigned index)const{return windows_.at(index);}
};
}
