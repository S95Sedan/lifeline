#include "assets/flash.hpp"
#include "ui/input_prompts.hpp"
#include <bit>
#include <limits>
#include <functional>
#include <set>

namespace lifeline {
namespace {
struct Record {
    std::span<const unsigned char> b;
    void check(std::size_t p,std::size_t n) const { if (p>b.size() || n>b.size()-p) throw std::runtime_error("CF record read outside bounds"); }
    unsigned u32(std::size_t p) const { check(p,4); return le32(b.data()+p); }
    float number(std::size_t p) const {
        const auto f=std::bit_cast<float>(u32(p));
        if (!std::isfinite(f)) throw std::runtime_error("Nonfinite active CF field");
        return f;
    }
    math::Vector4 vector(std::size_t p) const { return {number(p),number(p+4),number(p+8),number(p+12)}; }
};
void actions(const Record& r,FlashFrame& frame) {
    std::vector<std::string> stack;
    std::size_t p=16;
    while (p<r.b.size()) {
        const auto opcode=r.b[p++]; if (!opcode) return;
        std::size_t size=0;
        if (opcode>=128) { r.check(p,2); size=le16(r.b.data()+p);p+=2;r.check(p,size); }
        const auto end=p+size;
        if (opcode==0x06 || opcode==0x07) frame.commands.emplace_back(FlashAction{opcode==6?FlashAction::play:FlashAction::stop,0,{},{}});
        else if (opcode==0x81 && size==2) frame.commands.emplace_back(FlashAction{FlashAction::go_to_frame,le16(r.b.data()+p),{},{}});
        else if (opcode==0x96) {
            while (p<end) {
                if (r.b[p++]!=0) throw std::runtime_error("Unrecovered CF action value type");
                const auto begin=p;
                while (p<end && r.b[p]) ++p;
                if (p==end) throw std::runtime_error("Unterminated CF action string");
                stack.emplace_back(reinterpret_cast<const char*>(r.b.data()+begin),p-begin);++p;
            }
        } else if (opcode==0x1d && stack.size()>=2) {
            auto value=stack.back();stack.pop_back();auto name=stack.back();stack.pop_back();
            frame.commands.emplace_back(FlashAction{FlashAction::set_variable,0,std::move(name),std::move(value)});
        } else throw std::runtime_error("Unrecovered CF action opcode " + std::to_string(opcode));
        p=end;
    }
    throw std::runtime_error("CF action stream has no terminator");
}
FlashShape shape(const Record& r) {
    FlashShape result;
    result.bounds=r.vector(32);
    if(result.bounds.x>result.bounds.z || result.bounds.y>result.bounds.w) throw std::runtime_error("Invalid CF shape bounds");
    const auto count=r.u32(16);
    if (count>4096) throw std::runtime_error("CF fill count exceeds limit");
    std::size_t p=48;
    for (unsigned i=0;i<count;++i) {
        r.check(p,48); FlashFill fill;
        const auto kind=r.u32(p),polygons=r.u32(p+4);
        if (kind>1 || polygons>4096) throw std::runtime_error("Unrecovered CF fill kind or polygon count");
        fill.textured=kind==0;
        if (fill.textured) { fill.texture=r.u32(p+16);fill.origin_x=r.number(p+20);fill.origin_y=r.number(p+24); }
        else fill.color=r.vector(p+16);
        p+=48;
        for (unsigned j=0;j<polygons;++j) {
            const auto vertices=r.u32(p);r.check(p,16);p+=16;
            if (vertices<3 || vertices>4096) throw std::runtime_error("Unsupported CF polygon size");
            r.check(p,std::size_t(vertices)*16);
            std::vector<math::Vector4> polygon;
            for (unsigned k=0;k<vertices;++k) { polygon.push_back(r.vector(p));p+=16; }
            fill.polygons.push_back(std::move(polygon));
        }
        result.fills.push_back(std::move(fill));
    }
    if (p!=r.b.size()) throw std::runtime_error("Unrecovered CF shape tail");
    return result;
}
// Clip each triangle to the exported convex mask polygons, retaining UV/color
// interpolation. Multiple mask polygons form the authored visible regions.
void clip_draw(FlashDraw& draw,const FlashShape& shape,const math::Matrix4& matrix) {
    FlashDraw output;output.texture=draw.texture;output.textured=draw.textured;
    for(const auto& fill:shape.fills) for(const auto& polygon:fill.polygons) {
        std::vector<math::Vector4> mask;
        float area=0;
        for(const auto& point:polygon) mask.push_back(math::transform(matrix,point));
        for(unsigned i=0;i<mask.size();++i) {const auto a=mask[i],b=mask[(i+1)%mask.size()];area+=a.x*b.y-b.x*a.y;}
        if(std::abs(area)<.00001f) continue;
        const float sign=area>0?1.f:-1.f;
        for(unsigned i=0;i<mask.size();++i) {
            const auto a=mask[i],b=mask[(i+1)%mask.size()],c=mask[(i+2)%mask.size()];
            if(sign*((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x))<-.001f) throw std::runtime_error("Non-convex CF mask");
        }
        for(unsigned triangle=0;triangle<draw.indices.size();triangle+=3) {
            std::vector<FlashVertex> points;
            for(unsigned k=0;k<3;++k) points.push_back(draw.vertices.at(draw.indices[triangle+k]));
            for(unsigned edge=0;edge<mask.size() && !points.empty();++edge) {
                const auto a=mask[edge],b=mask[(edge+1)%mask.size()];
                const auto distance=[&](const FlashVertex& p){return sign*((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x));};
                std::vector<FlashVertex> clipped;auto previous=points.back();float d0=distance(previous);
                for(const auto& current:points) {
                    const float d1=distance(current);
                    if((d0>=0)!=(d1>=0)) {
                        const float t=d0/(d0-d1);const auto lerp=[&](float x,float y){return x+(y-x)*t;};
                        clipped.push_back({lerp(previous.x,current.x),lerp(previous.y,current.y),lerp(previous.u,current.u),lerp(previous.v,current.v),
                            {lerp(previous.color.x,current.color.x),lerp(previous.color.y,current.color.y),lerp(previous.color.z,current.color.z),lerp(previous.color.w,current.color.w)}});
                    }
                    if(d1>=0) clipped.push_back(current);previous=current;d0=d1;
                }
                points=std::move(clipped);
            }
            if(points.size()<3) continue;
            const auto base=static_cast<int>(output.vertices.size());output.vertices.insert(output.vertices.end(),points.begin(),points.end());
            for(unsigned k=1;k+1<points.size();++k) output.indices.insert(output.indices.end(),{base,base+int(k),base+int(k)+1});
        }
    }
    draw=std::move(output);
}

FlashText text_record(const Record& r,const std::map<unsigned,FlashFont>& fonts) {
    FlashText result;result.bounds=r.vector(32);
    float x=result.bounds.x,y=result.bounds.y,height=0;unsigned font=0;
    math::Vector4 color{1,1,1,1};std::string prefix;std::size_t p=48;
    bool ended=false;
    while(p+4<=r.b.size()) {
        const auto flags=r.u32(p);p+=4;if(!flags) {ended=true;break;}
        if(flags&128) {
            if(flags&~143u) throw std::runtime_error("Unsupported CF text style flags");
            if(flags&8) {font=r.u32(p);p+=4;if(!fonts.contains(font)) throw std::runtime_error("Missing CF font");result.font=font;}
            if(flags&4) {
                const auto c=r.u32(p);p+=4;
                color={float((c>>16)&255)/256,float((c>>8)&255)/256,float(c&255)/256,std::min(float(c>>24)/128,1.0f)};
            }
            if(flags&1) {x=result.bounds.x+r.number(p);p+=4;}
            if(flags&2) {y=result.bounds.y+r.number(p);p+=4;}
            if(flags&8) {height=r.number(p);p+=4;result.height=height;}
        } else {
            if(flags>127 || !fonts.contains(font) || height<=0) throw std::runtime_error("Invalid CF glyph run");
            const auto& map=fonts.at(font);
            for(unsigned i=0;i<flags;++i) {
                const auto packed=r.u32(p);p+=4;const auto glyph=packed>>16;const float advance=float(packed&65535);
                if(glyph>=map.characters.size()) throw std::runtime_error("CF glyph outside font map");
                if(prefix.size()<16) prefix.push_back(static_cast<char>(map.characters[glyph]));
                result.glyphs.push_back({font,glyph,x,y-height,advance,height,color});x+=advance;
            }
        }
    }
    if(!ended) throw std::runtime_error("Unterminated CF text records");
    if(prefix.starts_with("\\\\")) {
        result.variable=prefix.substr(2,11);const auto end=result.variable.find_first_of(" \0",0,2);
        if(end!=std::string::npos) result.variable.resize(end);
    } else result.hidden=prefix.starts_with("@@");
    return result;
}

}
FlashMovie::FlashMovie(std::span<const unsigned char> bytes,std::string label):name(std::move(label)) {
    Record file{bytes};
    if (file.u32(0)!=0x63662020 || file.u32(4)!=0x10000) throw std::runtime_error("Unsupported CF movie header");
    width=file.u32(8);height=file.u32(12);rate=file.u32(16);const auto count=file.u32(20);
    if (!width || width>4096 || !height || height>4096 || !rate || rate>240 || !count || count>100000)
        throw std::runtime_error("Invalid CF movie dimensions/timing");
    std::size_t p=32;
    std::function<std::vector<FlashFrame>(unsigned,unsigned)> parse;
    parse=[&](unsigned expected,unsigned nesting) {
    if (nesting>8) throw std::runtime_error("CF nesting exceeds limit");
    std::vector<FlashFrame> parsed;
    FlashFrame frame;bool ended=false;
    while (p+16<=bytes.size()) {
        const auto kind=file.u32(p),id=file.u32(p+4),size=file.u32(p+8);
        if (size<16 || size%16) throw std::runtime_error("Invalid CF tag size");
        file.check(p,size);Record r{bytes.subspan(p,size)};p+=size;
        switch (kind) {
        case 0:
            if (!shapes.emplace(id,shape(r)).second) throw std::runtime_error("Duplicate CF shape identifier");
            break;
        case 1:
            if (!textures.emplace(id,decode_cf_texture_record(r.b,name)).second) throw std::runtime_error("Duplicate CF texture identifier");
            break;
        case 8: {
            r.check(0,96);const auto width=le16(r.b.data()+16),height=le16(r.b.data()+18);
            FlashFont font;font.columns=r.u32(20);font.u_step=r.number(24);font.v_step=r.number(28);
            if(!width || !height || width>4096 || height>4096 || !font.columns || font.columns>256 ||
                font.u_step<=0 || font.v_step<=0 || width%128 || height%128 || size!=96ULL+std::uint64_t(width)*height/2)
                throw std::runtime_error("Unsupported CF font atlas");
            AssetImage image{name+" font",width,height,Bytes(std::size_t(width)*height*4)};
            for(std::size_t pixel=0;pixel<std::size_t(width)*height;++pixel) {
                // Font upload 0x1e4fd8 uses a width/2, height/4 PSMCT32
                // source. Invert that offline texel ordering directly to RGBA.
                const unsigned x=unsigned(pixel%width),y=unsigned(pixel/width);
                const unsigned block=((y>>4)&1)|(((x>>5)&1)<<1)|(((y>>5)&1)<<2)|(((x>>6)&1)<<3)|(((y>>6)&1)<<4);
                const unsigned column=(y>>2)&3;
                const unsigned word=((x&1)|((y&1)<<1)|((x&6)<<1))^((((y>>1)^(y>>2))&1)<<3);
                const unsigned source_x=(x/128)*64+((block&1)<<3)+((block&4)<<2)+((block&16)<<1)+(word&1)+((word&12)>>1);
                const unsigned source_y=(y/128)*32+((block&2)<<2)+((block&8)<<1)+column*2+((word&2)>>1);
                const unsigned offset=(source_y*(width/2)+source_x)*4+((x>>3)&3);
                r.check(96+offset,1);
                const unsigned index=(r.b[96+offset]>>(((y>>1)&1)*4))&15;
                const auto* c=r.b.data()+32+index*4;auto* dst=image.rgba.data()+pixel*4;
                std::copy_n(c,3,dst);dst[3]=static_cast<unsigned char>(std::min(unsigned(c[3])*2,255u));
            }
            if(!fonts.emplace(id,std::move(font)).second || !textures.emplace(id,std::move(image)).second)
                throw std::runtime_error("Duplicate CF font");
            break;
        }
        case 15: {
            const auto count=r.u32(16),font_id=r.u32(20),metrics=r.u32(24);
            if(!fonts.contains(font_id) || !count || count>4096) throw std::runtime_error("Invalid CF font character map");
            r.check(32,count*2);r.check(metrics,count*4);auto& font=fonts.at(font_id);
            if(!font.characters.empty()) throw std::runtime_error("Duplicate CF character map");
            for(unsigned i=0;i<count;++i) {
                font.characters.push_back(le16(r.b.data()+32+i*2));font.advances.push_back(r.number(metrics+i*4));
            }
            break;
        }
        case 7:
            if(!texts.emplace(id,text_record(r,fonts)).second) throw std::runtime_error("Duplicate CF text");
            break;
        case 3: {
            if (size!=128) throw std::runtime_error("Invalid CF placement size");
            FlashPlacement place;place.depth=r.u32(112);place.flags=r.u32(120);
            if (place.depth>32767 || (place.flags&~95u)) throw std::runtime_error("Unrecovered CF placement flags or depth");
            if (place.flags&2) place.shape=r.u32(116);
            if (place.flags&64) {place.clip_depth=r.u32(124)&65535; if(place.clip_depth<=place.depth) throw std::runtime_error("Invalid CF clip depth");}
            // Inactive fields contain uninitialized exporter data, even NaNs.
            if (place.flags&4) for (unsigned i=0;i<4;++i) place.matrix.columns[i]=r.vector(16+i*16);
            if (place.flags&8) { place.multiply=r.vector(80);place.add=r.vector(96); }
            frame.commands.emplace_back(place);break;
        }
        case 5: frame.commands.emplace_back(FlashRemove{id});break;
        case 6: parsed.push_back(std::move(frame));frame={};
            if(parsed.size()>expected) throw std::runtime_error("Too many CF frames");break;
        case 9: {
            if(size!=32) throw std::runtime_error("Invalid CF nested header");
            const auto nested_count=r.u32(16);
            if(!nested_count || nested_count>100000) throw std::runtime_error("Invalid nested CF frame count");
            auto nested=parse(nested_count,nesting+1);
            if(!timelines.emplace(id,std::move(nested)).second) throw std::runtime_error("Duplicate CF timeline");
            break;
        }
        case 10: {
            FlashButton button;const auto parts=r.u32(32);
            if(!parts || parts>4096 || size<112ULL+80ULL*parts) throw std::runtime_error("Unsupported CF button layout in "+name);
            for(unsigned j=0;j<parts;++j) {
                const auto offset=96+j*80;FlashButtonPart part;
                for(unsigned k=0;k<4;++k) part.matrix.columns[k]=r.vector(offset+k*16);
                part.resource=r.u32(offset+64);part.states=r.u32(offset+68);
                if(!part.states || (part.states&~15u)) throw std::runtime_error("Unsupported CF button states");
                const auto color=r.u32(offset+72);
                constexpr float channel_scale=std::bit_cast<float>(0x3b7f9724u); // Literal at 0x15ace8.
                part.color={float(color>>24)*channel_scale,float((color>>16)&255)*channel_scale,
                    float((color>>8)&255)*channel_scale,float(color&255)*channel_scale};
                button.parts.push_back(part);
            }
            std::size_t tail=96+parts*80;button.condition=r.u32(tail);
            if(r.u32(tail+4)==0 && size==128ULL+80ULL*parts)actions(Record{r.b.subspan(tail)},button.action);
            else for(;;){
                r.check(tail,16);const auto condition=r.u32(tail),next=r.u32(tail+4);const auto length=next?std::size_t(next):r.b.size()-tail;
                if(length<17)throw std::runtime_error("Invalid CF button action chain");r.check(tail,length);
                FlashAction program{FlashAction::bytecode,0,{},{}};program.program.assign(r.b.begin()+tail+16,r.b.begin()+tail+length);
                FlashFrame block;block.commands.emplace_back(std::move(program));button.linked_actions.emplace_back(condition,std::move(block));
                if(!next)break;tail+=next;
            }
            if(!buttons.emplace(id,std::move(button)).second) throw std::runtime_error("Duplicate CF button");
            break;
        }
        case 11: frame.commands.emplace_back(FlashBackground{{float((id>>16)&255)/256,float((id>>8)&255)/256,float(id&255)/256,1}});break;
        case 12: ended=true;break;
        case 14: actions(r,frame);break;
        default: throw std::runtime_error("Unrecovered CF tag " + std::to_string(kind) + " in " + name);
        }
        if (ended) break;
    }
    if (!ended || parsed.size()!=expected) throw std::runtime_error("CF movie frame count or terminator mismatch: " + name);
    // Actions immediately following the final ShowFrame execute at that boundary.
    if (!frame.commands.empty()) parsed.back().commands.insert(parsed.back().commands.end(),frame.commands.begin(),frame.commands.end());
    return parsed;
    };
    frames=parse(count,0);
    for (const auto& [id,s]:shapes) for (const auto& fill:s.fills)
        if (fill.textured && !textures.contains(fill.texture)) throw std::runtime_error("CF shape references missing texture");
    const auto validate=[&](const std::vector<FlashFrame>& sequence) {
    for (const auto& f:sequence) for (const auto& command:f.commands)
        if (const auto* action=std::get_if<FlashAction>(&command);action && action->kind==FlashAction::go_to_frame && action->frame>=sequence.size())
            throw std::runtime_error("CF action frame outside movie");
    };
    validate(frames);for(const auto& [id,sequence]:timelines) validate(sequence);
    for(const auto& f:frames) for(const auto& c:f.commands)
        if(const auto* place=std::get_if<FlashPlacement>(&c);place && (place->flags&2) &&
            !shapes.contains(place->shape) && !texts.contains(place->shape) && !buttons.contains(place->shape))
            throw std::runtime_error("Unrecovered root CF resource placement");
    // This recovered subset allows shape/text child timelines and buttons whose
    // parts are shapes or child timelines. Reject cycles/deeper behavior explicitly.
    for(const auto& [id,sequence]:timelines) for(const auto& f:sequence) for(const auto& c:f.commands)
        if(const auto* place=std::get_if<FlashPlacement>(&c);place && (place->flags&2) && !shapes.contains(place->shape) && !texts.contains(place->shape))
            throw std::runtime_error("Unrecovered nested CF resource placement");
    for(const auto& [id,button]:buttons) {
        if(shapes.contains(id) || textures.contains(id)) throw std::runtime_error("Duplicate CF resource identifier");
        for(const auto& part:button.parts)
            if(!shapes.contains(part.resource) && !texts.contains(part.resource) && !timelines.contains(part.resource)) throw std::runtime_error("Missing CF button resource");
        for(const auto& c:button.action.commands)
            if(const auto* a=std::get_if<FlashAction>(&c);a && a->kind==FlashAction::go_to_frame && a->frame>=frames.size())
                throw std::runtime_error("CF button target outside root timeline");
    }
}
void FlashPlayer::text_property(const std::string& name,int selector,std::int32_t value) {
    auto& text=(*text_values_)[name];
    const auto half=std::bit_cast<std::int16_t>(std::uint16_t(value));
    // 157670: signed halfword dimensions/flags, integer spacing and offset.
    switch(selector) {
    case 0:text.width=float(half);break;
    case 1:text.height=float(half);break;
    case 3:text.flags=half;break;
    case 6:text.line_spacing=half;break;
    case 7:text.horizontal_spacing=value;break;
    case 9:text.horizontal_offset=float(value);break;
    case 2:throw std::runtime_error("CF text pointer must be resolved by script service");
    case 8:if(value)throw std::runtime_error("CF text visibility predicate requires a recovered native function");break;
    default:break; // Native selectors 4/5 and values outside 0..9 do nothing.
    }
}
void FlashPlayer::apply(unsigned index) {
    frame_=index;jump_=-1;jump_playing_=-1;
    for (const auto& command:(*frames_)[index].commands) {
        if (const auto* place=std::get_if<FlashPlacement>(&command)) {
            if (place->flags&2) {
                if (!movie_->shapes.contains(place->shape) && !movie_->texts.contains(place->shape) && !movie_->buttons.contains(place->shape)) throw std::runtime_error("CF placement references missing shape");
                layers_.insert_or_assign(place->depth,*place);
            } else if (place->flags&1) {
                auto found=layers_.find(place->depth);
                if (found==layers_.end()) continue; // Original update scans existing depths only.
                if (place->flags&4) found->second.matrix=place->matrix;
                if (place->flags&8) { found->second.multiply=place->multiply;found->second.add=place->add; }
            }
        } else if (const auto* remove=std::get_if<FlashRemove>(&command)) layers_.erase(remove->depth);
        else if (const auto* background=std::get_if<FlashBackground>(&command)) background_=background->color;
        else if (const auto* action=std::get_if<FlashAction>(&command)) {
            execute(*action);
        }
    }
}
void FlashPlayer::execute(const FlashAction& action) {
    switch(action.kind) {
    case FlashAction::play: playing_=true;if(jump_>=0)jump_playing_=1;break;
    case FlashAction::stop: playing_=false;if(jump_>=0)jump_playing_=0;break;
    case FlashAction::go_to_frame: jump_=static_cast<int>(action.frame);jump_playing_=-1;break;
    case FlashAction::set_variable: variable(action.name,action.value);break;
    case FlashAction::bytecode:execute_bytecode(action.program);break;
    }
}
void FlashPlayer::set_button_state(unsigned depth,unsigned state) {
    if(state!=1 && state!=2 && state!=4) throw std::runtime_error("Invalid CF button state");
    auto found=layers_.find(depth);
    if(found==layers_.end() || !movie_->buttons.contains(found->second.shape)) throw std::runtime_error("Missing CF button depth");
    auto& layer=found->second;const auto previous=layer.button_state;
    if(previous==state) return;
    if(!event_sink_->input_enabled){layer.button_state=state;return;}
    // Original state transition mask function 0x15e130.
    unsigned event=0;
    if((previous&1) && (state&2)) event|=1;
    if(previous&2) { event|=state&4;if(state&1) event|=2; }
    if((previous&4) && (state&2)) event|=8;
    const auto& button=movie_->buttons.at(layer.shape);
    for(const auto& part:button.parts) if((part.states&previous) && children_.contains(part.resource)) children_.at(part.resource)->seek(0);
    layer.button_state=state;sync_children(false);
    if(button.condition&event) for(const auto& c:button.action.commands) execute(std::get<FlashAction>(c));
    for(const auto& [condition,actions]:button.linked_actions)if(condition&event)for(const auto& c:actions.commands)execute(std::get<FlashAction>(c));
}
void FlashPlayer::pointer(float x,float y,bool down,const math::Matrix4& parent) {
    if(!std::isfinite(x) || !std::isfinite(y)) throw std::runtime_error("Invalid CF pointer");
    bool occupied=false;
    // Original input traverses the depth list backwards: the frontmost hit wins.
    for(auto it=layers_.rbegin();it!=layers_.rend();++it) {
        const auto button=movie_->buttons.find(it->second.shape);
        if(button==movie_->buttons.end()) continue;
        const auto& parts=button->second.parts;
        auto part=std::find_if(parts.begin(),parts.end(),[](const auto& p){return p.states&8;});
        if(part==parts.end()) part=parts.begin();
        if(!movie_->shapes.contains(part->resource)) throw std::runtime_error("Unrecovered nested CF hit bounds");
        const auto bounds=movie_->shapes.at(part->resource).bounds;
        const auto matrix=math::multiply(parent,math::multiply(it->second.matrix,part->matrix));
        const auto low=math::transform(matrix,{bounds.x,bounds.y,0,1});
        const auto high=math::transform(matrix,{bounds.z,bounds.w,0,1});
        // Match 0x15e1d0: transform the exported rectangle's two corners.
        const bool hit=!occupied && x>low.x && x<high.x && y>low.y && y<high.y;
        set_button_state(it->first,hit?(down?4:2):1);
        occupied|=hit;
    }
}
void FlashPlayer::click(float x,float y) {
    // Script API 0x15dbd0 mode 1 injects hover and press, then releases on
    // the next input update. This API completes the event synchronously.
    pointer(x,y,false);pointer(x,y,true);pointer(x,y,false);
}
void FlashPlayer::seek(unsigned frame) {
    if (frame>=frames_->size()) throw std::runtime_error("CF seek outside movie");
    layers_.clear();children_.clear();variables_.clear();background_={0,0,0,1};playing_=true;finished_=false;
    for (unsigned i=0;i<=frame;++i) { apply(i);sync_children(i!=0); }
}
void FlashPlayer::advance() {
    if (jump_>=0) {
        const auto target=static_cast<unsigned>(jump_);const bool resume=playing_;const int override_playing=jump_playing_;
        // Rebuilding the display list is not a second playback of callbacks.
        // The destination's actions below deliver its messages exactly once.
        ++event_sink_->suppressed;
        try{seek(target);}catch(...){--event_sink_->suppressed;throw;}
        --event_sink_->suppressed;playing_=resume;
        // Rebuilding the display list must not re-trigger Stop actions on frames
        // crossed by a jump. Only the destination's actions affect playback.
        for(const auto& c:(*frames_)[target].commands) if(const auto* a=std::get_if<FlashAction>(&c)) execute(*a);
        // Play/Stop following GoToFrame belongs after the destination actions.
        if(override_playing>=0)playing_=override_playing!=0;
        return;
    }
    if (!playing_ || finished_) { sync_children(true);return; }
    if (frame_+1>=frames_->size()) {
        if(nested_) { frame_=0;apply(0); }
        else if(looping_) { seek(0);return; }
        else finished_=true;
        sync_children(true);return;
    }
    apply(frame_+1);sync_children(true);
}
void FlashPlayer::sync_children(bool tick) {
    std::set<unsigned> active;
    for(const auto& [depth,layer]:layers_) {
        const auto button=movie_->buttons.find(layer.shape);
        if(button==movie_->buttons.end()) continue;
        for(const auto& part:button->second.parts) if((part.states&layer.button_state) && movie_->timelines.contains(part.resource)) active.insert(part.resource);
    }
    for(const auto id:active) {
        auto found=children_.find(id);
        if(found==children_.end()) children_.emplace(id,std::unique_ptr<FlashPlayer>(new FlashPlayer(*movie_,movie_->timelines.at(id),event_sink_,text_values_)));
        else if(tick) found->second->advance();
    }
}
std::vector<FlashDraw> FlashPlayer::compose() const {
    std::vector<FlashDraw> result;
    const auto draw_shape=[&](unsigned shape_id,const FlashPlacement& layer) {
        if(auto prompt=input_prompt_shape(*movie_,shape_id)){
            for(auto& v:prompt->vertices){const auto p=math::transform(layer.matrix,{v.x,v.y,0,1});
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.w!=1)throw std::runtime_error("Unrecovered non-affine CF geometry");v.x=p.x;v.y=p.y;
                v.color={std::clamp(v.color.x*layer.multiply.x+layer.add.x,0.f,1.f),std::clamp(v.color.y*layer.multiply.y+layer.add.y,0.f,1.f),std::clamp(v.color.z*layer.multiply.z+layer.add.z,0.f,1.f),std::clamp(v.color.w*layer.multiply.w+layer.add.w,0.f,1.f)};
            }result.push_back(std::move(*prompt));return;
        }
        for (const auto& fill:movie_->shapes.at(shape_id).fills) {
            FlashDraw draw;draw.textured=fill.textured;draw.texture=fill.texture;
            const auto c=fill.color,m=layer.multiply,a=layer.add;
            const math::Vector4 color{std::clamp(c.x*m.x+a.x,0.0f,1.0f),std::clamp(c.y*m.y+a.y,0.0f,1.0f),
                std::clamp(c.z*m.z+a.z,0.0f,1.0f),std::clamp(c.w*m.w+a.w,0.0f,1.0f)};
            if (color.w==0) continue;
            for (const auto& polygon:fill.polygons) {
                const auto base=static_cast<int>(draw.vertices.size());
                for (const auto& point:polygon) {
                    const auto transformed=math::transform(layer.matrix,point);
                    if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) || transformed.w!=1)
                        throw std::runtime_error("Unrecovered non-affine CF geometry");
                    float u=0,v=0;
                    if (fill.textured) {
                        const auto& texture=movie_->textures.at(fill.texture);
                        u=(point.x-fill.origin_x)/float(texture.width);v=(point.y-fill.origin_y)/float(texture.height);
                    }
                    draw.vertices.push_back({transformed.x,transformed.y,u,v,color});
                }
                for (unsigned i=1;i+1<polygon.size();++i)
                    draw.indices.insert(draw.indices.end(),{base,base+static_cast<int>(i),base+static_cast<int>(i)+1});
            }
            result.push_back(std::move(draw));
        }
    };
    const auto draw_text=[&](unsigned id,const FlashPlacement& layer) {
        const auto& text=movie_->texts.at(id);
        if(text.hidden) return;
        auto glyphs=text.glyphs;
        if(!text.variable.empty()) {
            glyphs.clear();const auto value=text_values_->find(text.variable);
            if(value==text_values_->end()) return;
            const auto& font=movie_->fonts.at(text.font);
            const auto& style=value->second;
            float x=text.bounds.x,y=text.bounds.y,width=style.width,height=style.height;
            float draw_width=width+float(style.horizontal_spacing);
            auto text_color=value->second.color;
            const auto prompted=text.variable=="text_tu0"?std::optional(input_prompt_text(text.variable,value->second.value)):std::nullopt;
            const auto& content=prompted?*prompted:value->second.value;
            for(std::size_t position=0;position<content.size();) {
                unsigned character=static_cast<unsigned char>(content[position++]);
                // Original 15c580..15c5b4: Shift-JIS lead bytes form a
                // two-byte atlas code, including inventory quantity symbols.
                if((character>=0x81&&character<=0x9f)||(character>=0xe0&&character<=0xfc)){
                    if(position==content.size())break;
                    character=(character<<8)|static_cast<unsigned char>(content[position++]);
                }
                if(character=='\n') {x=text.bounds.x;y+=height+style.line_spacing;continue;}
                if(character=='@') {
                    // Scripted typewriter text can end partway through a
                    // directive. Render the completed prefix and wait for the
                    // next text update before interpreting that directive.
                    if(position==content.size())break;
                    std::size_t directive_size=3; // hNN / vNN
                    if(content[position]=='c')directive_size=9;
                    if(content[position]=='p'){
                        if(position+1==content.size())break;
                        directive_size=(content[position+1]=='h'||content[position+1]=='v')?4:3;
                    }
                    if(content.size()-position<directive_size)break;
                    const auto decimal=[&]() {
                        if(position+2>content.size() || content[position]<'0' || content[position]>'9' || content[position+1]<'0' || content[position+1]>'9')
                            throw std::runtime_error("Invalid CF text decimal");
                        const auto result=(content[position]-'0')*10+content[position+1]-'0';position+=2;return float(result);
                    };
                    // Original 0x15c89c: RGBA as four two-digit percentages.
                    if(position<content.size() && content[position]=='c') {
                        ++position;text_color={decimal()*.01f,decimal()*.01f,decimal()*.01f,decimal()*.01f};continue;
                    }
                    // Original 0x15c764: @pNN selects the seven-entry palette
                    // at 0x302de8. These are already in the CF color range.
                    if(position<content.size() && content[position]=='p') {
                        ++position;
                        if(position<content.size() && content[position]>='0' && content[position]<='9') {
                            static constexpr std::array<math::Vector4,7> palette{{
                                {.85f,.85f,.85f,.5f},{.8f,.3f,.18f,.5f},{.2f,.75f,.2f,.5f},
                                {.8f,.8f,.2f,.5f},{.1f,.4f,.75f,.5f},{.45f,.45f,.45f,.5f},{1,1,1,.5f}}};
                            const auto index=static_cast<unsigned>(decimal());
                            if(index>=palette.size()) throw std::runtime_error("Invalid CF text palette index");
                            text_color=palette[index];continue;
                        }
                    }
                    const char axis=content[position++],a=content[position++],b=content[position++];
                    if((axis!='h' && axis!='v') || a<'0' || a>'9' || b<'0' || b>'9') throw std::runtime_error("Unsupported CF text directive");
                    const float size=float((a-'0')*10+b-'0');
                    // 15c66c..15c760 allow zero. Combat labels use @pv00
                    // before a newline so the following X overlays the word.
                    if(axis=='h') width=draw_width=size;else height=size;
                    continue;
                }
                if(character==' ') {x+=style.flags?width*.25f:width;}
                else {
                    const auto found=std::find(font.characters.begin(),font.characters.end(),character);
                    // Original 0x15ca00 skips characters absent from this atlas.
                    if(found==font.characters.end()) continue;
                    const auto index=static_cast<unsigned>(found-font.characters.begin());
                    glyphs.push_back({text.font,index,x+style.horizontal_offset,y,draw_width,height,text_color,true});
                    x+=(style.flags&1)?font.advances.at(index)*draw_width+float(style.horizontal_spacing):draw_width;
                }
                if(x>text.bounds.x+text.bounds.z) {x=text.bounds.x;y+=height+(character==' '?0:style.line_spacing);}
            }
            if(value->second.alignment==FlashTextAlignment::center){
                for(std::size_t first=0;first<glyphs.size();){
                    auto end=first+1;while(end<glyphs.size() && glyphs[end].y==glyphs[first].y)++end;
                    const auto& last=glyphs[end-1];
                    const float extent=last.x-text.bounds.x+font.advances.at(last.index)*last.width;
                    const float offset=(text.bounds.z-extent)*.5f;
                    for(auto i=first;i<end;++i)glyphs[i].x+=offset;
                    first=end;
                }
            }
        }
        for(const auto& glyph:glyphs) {
            const auto& font=movie_->fonts.at(glyph.font);
            const auto c=glyph.color,m=layer.multiply,a=layer.add;
            const math::Vector4 color{std::clamp(c.x*m.x+a.x*.5f,0.0f,1.0f),std::clamp(c.y*m.y+a.y*.5f,0.0f,1.0f),
                std::clamp(c.z*m.z+a.z*.5f,0.0f,1.0f),std::clamp(c.w*m.w+a.w*.5f,0.0f,1.0f)};
            if(!color.w || !glyph.width) continue;
            FlashDraw draw;draw.textured=true;draw.texture=glyph.font;
            const float u=float(glyph.index%font.columns)*font.u_step,v=float(glyph.index/font.columns)*font.v_step;
            for(const auto corner:std::array<math::Vector4,4>{{{0,0,0,1},{1,0,0,1},{1,1,0,1},{0,1,0,1}}}) {
                const auto point=math::transform(layer.matrix,{glyph.x+corner.x*glyph.width,glyph.y+corner.y*glyph.height,0,1});
                if(point.w!=1 || !std::isfinite(point.x) || !std::isfinite(point.y)) throw std::runtime_error("Non-affine CF text transform");
                draw.vertices.push_back({point.x,point.y,u+corner.x*(font.u_step-(glyph.dynamic?1.f/movie_->textures.at(glyph.font).width:0.f)),v+corner.y*font.v_step,color});
            }
            draw.indices={0,1,2,0,2,3};result.push_back(std::move(draw));
        }
    };
    for(const auto& [depth,layer]:layers_) {
        if(layer.flags&64) continue;
        const auto first=result.size();
        if(movie_->shapes.contains(layer.shape)) draw_shape(layer.shape,layer);
        else if(movie_->texts.contains(layer.shape)) draw_text(layer.shape,layer);
        else for(const auto& part:movie_->buttons.at(layer.shape).parts) {
            if(!(part.states&layer.button_state)) continue;
            FlashPlacement combined=layer;
            combined.matrix=math::multiply(layer.matrix,part.matrix);
            combined.multiply={layer.multiply.x*part.color.x,layer.multiply.y*part.color.y,layer.multiply.z*part.color.z,layer.multiply.w*part.color.w};
            if(movie_->shapes.contains(part.resource)) draw_shape(part.resource,combined);
            else if(movie_->texts.contains(part.resource)) draw_text(part.resource,combined);
            else {
                auto draws=children_.at(part.resource)->compose();
                for(auto& draw:draws) {
                    for(auto& v:draw.vertices) {
                        const auto point=math::transform(combined.matrix,{v.x,v.y,0,1});
                        if(point.w!=1 || !std::isfinite(point.x) || !std::isfinite(point.y)) throw std::runtime_error("Non-affine CF button transform");
                        v.x=point.x;v.y=point.y;
                        // Child scripts own their colors; the original nested draw
                        // passes the matrix stack, not the parent's color transform.
                    }
                    result.push_back(std::move(draw));
                }
            }
        }
        for(const auto& [mask_depth,mask]:layers_) if((mask.flags&64) && depth>mask_depth && depth<=mask.clip_depth) {
            if(!movie_->shapes.contains(mask.shape)) throw std::runtime_error("Unsupported CF mask resource");
            for(auto index=first;index<result.size();++index) clip_draw(result[index],movie_->shapes.at(mask.shape),mask.matrix);
        }
    }
    std::erase_if(result,[](const auto& draw){return draw.indices.empty();});
    return result;
}
}
