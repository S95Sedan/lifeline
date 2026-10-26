#include "script/interface.hpp"
#include "ui/input_prompts.hpp"
#include <cmath>
#include <algorithm>
#include <bit>

namespace lifeline::script {
namespace {
std::uint32_t address(const Invocation& call,float value){
    if(!std::isfinite(value) || value<0 || std::floor(value)!=value || double(value)>=call.program.size())throw std::runtime_error("Invalid interface string address");
    return std::uint32_t(value);
}
std::string text(const Invocation& c,float value){
    if(!std::isfinite(value) || value<0 || double(value)>INT32_MAX)throw std::runtime_error("Invalid interface text address");
    std::string result;
    for(unsigned word=unsigned(std::trunc(value));;++word){
        if(c.state?!c.state->data_contains(c.program,word):word>=c.program.size())throw std::runtime_error("Unterminated interface script text");
        auto bits=word<c.program.size()?c.program.word(word):0;
        if(c.state){const auto it=c.state->data->find(word);if(it!=c.state->data->end())bits=std::bit_cast<unsigned>(it->second);}
        for(unsigned i=0;i<4;++i){const char ch=char(bits>>(i*8));if(!ch)return result;result+=ch;}
    }
}
}
Interface::Instance* Interface::get(float value){
    if(!std::isfinite(value) || value<0 || value>=slots_.size() || std::floor(value)!=value)throw std::runtime_error("Invalid interface slot");
    return slots_[unsigned(value)].get();
}
void Interface::pointer(Instance& instance,bool down){
    if(!instance.pointer)return;
    // 992/15dbd0 receives movie-local coordinates. Hit bounds use the
    // placement/part matrices (15e1d0), not the instance's screen projection.
    // Scripts click offscreen control buttons to start authored animations.
    instance.player.pointer((*instance.pointer)[0],(*instance.pointer)[1],down);
}
Bindings Interface::bindings(){
    Bindings result;
    result.emplace(save_menu_update_opcode,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value)||value<0||value!=std::floor(value)||value>0x400000)throw std::runtime_error("Invalid save list argument");
        if(!has_native_save_menu(c.program.name()))throw std::runtime_error("Save list service called outside storage menu");
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{};
        if(instance->movie.name!="fileselecter.cf"||!c.state)throw std::runtime_error("Save list requires its storage panel");
        if(c.arguments[1]==1)instance->save_metadata=unsigned(c.arguments[2]);
        else if(c.arguments[2]>=3&&c.arguments[2]<save_slot_count+3)instance->save_list.select(unsigned(c.arguments[2])-3);
        else {instance->pointer=std::array{0.f,0.f};pointer(*instance,false);return Reply{};}
        const auto base=instance->save_metadata;
        const auto word=[&](unsigned address){
            if(!c.state->data_contains(c.program,address))throw std::runtime_error("Save list metadata is outside its allocation");
            const auto it=c.state->data->find(address);return it==c.state->data->end()?(address<c.program.size()?std::bit_cast<float>(c.program.word(address)):0.f):it->second;
        };
        if(base)for(unsigned row=0;row<save_visible_rows;++row){
            const unsigned record=base+(instance->save_list.first+row)*128;
            const auto suffix=std::to_string(row+1);
            instance->player.text_content("text_d"+suffix,text(c,word(record+1)));
            const bool valid=word(record)>0;
            const float fields[]{word(record+2),word(record+3),float(record+64),word(record+9),float(record+96),float(record+16),float(record+32)};
            for(unsigned field=0;field<7;++field)instance->player.text_content("text_dw"+suffix+"_"+std::to_string(field+1),valid?text(c,fields[field]):" ");
        }
        else for(unsigned row=1;row<=save_visible_rows;++row){
            const auto suffix=std::to_string(row);instance->player.text_content("text_d"+suffix," ");
            for(unsigned field=1;field<=7;++field)instance->player.text_content("text_dw"+suffix+"_"+std::to_string(field)," ");
        }
        for(unsigned row=0;row<save_visible_rows;++row){
            const auto value=instance->save_list.first+row+1;
            instance->player.set_text("save_label_"+std::to_string(row+1),"DATA "+std::string(value<10?"0":"")+std::to_string(value),16,16,{.2f,1.f,.95f,1.f},FlashTextAlignment::center);
        }
        constexpr float row_y[]{128,192,240,304};
        instance->pointer=std::array{100.f,row_y[instance->save_list.row()]};pointer(*instance,false);
        return Reply{};
    }});
    // 108bd0 reboots the Flash allocation pools, not the story or resources.
    result.emplace(999,Binding{0,0,[this](const Invocation&){
        for(auto& slot:slots_)slot.reset();++generation_;
        if(reboot_)reboot_();return Reply{false,0.f};
    }});
    result.emplace(117,Binding{0,1,[this](const Invocation& c){
        enabled_=c.operands[0]!=0;
        // A display-off immediately following a fade-to-black freezes the
        // scene behind that black frame. Ordinary gameplay commonly keeps CF
        // display disabled and must continue rendering its 3D room.
        // Enabling CF display can precede the next room's actor/camera setup
        // by many ticks. Only its fade-in (125), not CF enable (117), releases
        // an existing black transition; otherwise those setup poses flash.
        if(!enabled_&&fade_.alpha>0&&fade_.step>0)scene_suspended_=true;
        return Reply{};
    }});
    result.emplace(159,Binding{0,0,[this](const Invocation&){return Reply{false,float(enabled_)};}});
    result.emplace(981,Binding{2,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        if(!std::isfinite(c.arguments[1]))throw std::runtime_error("Invalid interface loop flag");
        // 15e988 restarts the root timeline only when decoder +460 is 1.
        instance->player.looping(std::trunc(c.arguments[1])==1.f);return Reply{false,1.f};
    }});
    result.emplace(982,Binding{1,0,[this](const Invocation& c){
        const auto name=c.program.string(address(c,c.arguments[0]));
        for(unsigned i=0;i<slots_.size();++i)if(!slots_[i]){
            // 1588d0 calls 1c6e50, which returns the first matching archive
            // slot. Shared/room CF names can coexist with different contents.
            const auto* files=resources_.first_loaded_files(name);
            if(!files)throw std::runtime_error("Resource member is not loaded: "+name);
            if(files->size()!=1||files->front().conversion!="raw")throw std::runtime_error("Interface requires one authored CF resource: "+name);
            auto instance=std::make_unique<Instance>(files->front(),name);
            if(name=="tit_load.cf")prepare_title_prompt(instance->movie,FlashMovie(resources_.read_raw("/archive/teru/a/1004.a","systemwindow.cf"),"systemwindow.cf"));
            if(name=="display2.cf")prepare_continue_prompt(instance->movie,FlashMovie(resources_.read_raw("/archive/teru/a/1000.a","msg.cf"),"msg.cf"));
            if(name=="inputinfo.cf")prepare_navigation_prompt(instance->movie,FlashMovie(resources_.read_raw("/archive/teru/a/1000.a","msg.cf"),"msg.cf"));
            if(name=="fileselecter.cf"){
                // The four DATA headings are baked images, not text fields.
                // Reuse their exact placements with the menu's own font atlas;
                // no texture uploads or CF reconstruction while scrolling.
                constexpr unsigned headings[]{8,10,14,19};
                for(unsigned row=0;row<save_visible_rows;++row){
                    const auto id=headings[row];const auto bounds=instance->movie.shapes.at(id).bounds;
                    FlashText label;label.variable="save_label_"+std::to_string(row+1);
                    label.font=instance->movie.texts.at(20).font;label.bounds={bounds.x,bounds.y,bounds.z-bounds.x,bounds.w-bounds.y};
                    instance->movie.shapes.erase(id);instance->movie.texts.emplace(id,std::move(label));
                }
            }
            instance->generation=++generation_;
            slots_[i]=std::move(instance);return Reply{false,float(i)};
        }
        return Reply{false,-1.f};
    }});
    for(unsigned opcode:{983u,984u,985u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        instance->state=opcode==983?2:opcode==984?4:3;
        if(opcode==985)instance->player.rewind_stopped();
        return Reply{false,1.f};
    }});
    result.emplace(986,Binding{1,0,[this](const Invocation& c){
        if(!get(c.arguments[0]))return Reply{false,0.f};slots_[unsigned(c.arguments[0])].reset();return Reply{false,1.f};
    }});
    result.emplace(987,Binding{0,0,[this](const Invocation&){for(auto& slot:slots_)slot.reset();return Reply{false,1.f};}});
    result.emplace(988,Binding{2,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        // 108178..1081f4 calls 15b030 for 0/1/2. That helper changes
        // projection only for mode 2; the 0/1 calls leave it unchanged.
        if(c.arguments[1]==2)instance->projection=2;
        else if(!std::isfinite(c.arguments[1]))throw std::runtime_error("Invalid interface projection");
        // 1081d8 ignores other selectors, including the datapad's 3. Its
        // constructor retains the default world-space projection (zero).
        return Reply{false,1.f};
    }});
    result.emplace(989,Binding{4,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        for(unsigned i=1;i<4;++i)if(!std::isfinite(c.arguments[i]))throw std::runtime_error("Invalid interface position");
        instance->position={c.arguments[1],c.arguments[2],c.arguments[3],1};return Reply{false,1.f};
    }});
    for(const auto opcode:{990u,991u})result.emplace(opcode,Binding{4,0,[this,opcode](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        for(unsigned i=1;i<4;++i)if(!std::isfinite(c.arguments[i]))throw std::runtime_error("Invalid interface transform");
        (opcode==990?instance->rotation:instance->scale)={c.arguments[1],c.arguments[2],c.arguments[3],0};return Reply{false,1.f};
    }});
    result.emplace(125,Binding{5,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid screen fade");
        const float seconds=c.arguments[0];
        fade_.alpha=seconds<=0?128.f:1.f;fade_.step=seconds==0?128.f:128.f/(seconds*60.f);
        if(seconds<0)scene_suspended_=false;
        for(unsigned i=0;i<4;++i)fade_.rectangle[i]=std::trunc(c.arguments[i+1]);
        return Reply{false,0.f};
    }});
    result.emplace(997,Binding{2,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        if(!std::isfinite(c.arguments[1]) || double(c.arguments[1])<INT32_MIN || double(c.arguments[1])>INT32_MAX)throw std::runtime_error("Invalid interface drawing order");
        // 108a78 -> 158810 stores +1a4; 15d4dc submits this drawing band.
        instance->draw_order=int(std::trunc(c.arguments[1]));return Reply{false,1.f};
    }});
    result.emplace(993,Binding{2,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        if(!std::isfinite(c.arguments[1]) || c.arguments[1]<0 || c.arguments[1]>=24)throw std::runtime_error("Invalid interface callback worker");
        const unsigned worker=unsigned(std::trunc(c.arguments[1])),slot=unsigned(c.arguments[0]);
        if(worker>=24)throw std::runtime_error("Invalid interface callback worker");
        instance->player.on_event([this,worker,slot](unsigned kind,int value){
            if(!message_)throw std::runtime_error("Interface callback has no script message sink");
            message_(worker,{std::int32_t(kind),std::int32_t(slot),value});
        });return Reply{false,1.f};
    }});
    result.emplace(994,Binding{3,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        instance->player.text_content(text(c,c.arguments[1]),text(c,c.arguments[2]));return Reply{false,1.f};
    }});
    result.emplace(992,Binding{4,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        if(!std::isfinite(c.arguments[1]))throw std::runtime_error("Invalid interface input mode");
        const auto mode=std::trunc(c.arguments[1]);if(mode!=0 && mode!=1)return Reply{false,1.f};
        if(!std::isfinite(c.arguments[2]) || !std::isfinite(c.arguments[3]))throw std::runtime_error("Invalid interface pointer position");
        instance->pointer=std::array{c.arguments[2],c.arguments[3]};pointer(*instance,false);
        if(mode==1)pointer(*instance,true);
        // 15dbd0 restores hover mode after injecting the press. The next
        // interface update releases it and keeps tracking animated hit bounds.
        return Reply{false,1.f};
    }});
    result.emplace(996,Binding{6,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        for(unsigned i=2;i<6;++i)if(!std::isfinite(c.arguments[i]))throw std::runtime_error("Invalid interface text color");
        instance->player.text_color(text(c,c.arguments[1]),{c.arguments[2],c.arguments[3],c.arguments[4],c.arguments[5]});return Reply{false,1.f};
    }});
    result.emplace(995,Binding{4,0,[this](const Invocation& c){
        auto* instance=get(c.arguments[0]);if(!instance)return Reply{false,0.f};
        for(unsigned i=2;i<4;++i)if(!std::isfinite(c.arguments[i]) || double(c.arguments[i])<INT32_MIN || double(c.arguments[i])>INT32_MAX)throw std::runtime_error("Invalid interface text property");
        const int selector=int(std::trunc(c.arguments[2])),value=int(std::trunc(c.arguments[3]));
        const auto name=text(c,c.arguments[1]);
        if(selector==2){
            // Unlike 994, selector 2 receives a byte pointer, not a word index.
            if(value<0)throw std::runtime_error("Invalid interface text pointer");
            std::string content;
            if(value){
                // Read from the addressed byte, including unaligned strings.
                for(unsigned at=unsigned(value);;++at){
                    const unsigned index=at/4;
                    if(c.state?!c.state->data_contains(c.program,index):index>=c.program.size())throw std::runtime_error("Unterminated interface text pointer");
                    auto bits=index<c.program.size()?c.program.word(index):0;
                    if(c.state){const auto it=c.state->data->find(index);if(it!=c.state->data->end())bits=std::bit_cast<unsigned>(it->second);}
                    const char ch=char(bits>>((at%4)*8));if(!ch)break;content+=ch;
                }
            }
            instance->player.text_content(name,std::move(content));
        }else instance->player.text_property(name,selector,value);
        return Reply{false,1.f};
    }});
    result.emplace(998,Binding{1,0,[this](const Invocation& c){
        const auto* instance=get(c.arguments[0]);
        // 108b38 reads the timeline state at +198, not loader mode +464.
        return Reply{false,instance?float(instance->player.playback_state()):-1.f};
    }});
    return result;
}
void Interface::append_background(StaticModel& model,const SceneCamera& camera)const{
    // Destination-alpha effects must read preceding CF bands in the same
    // framebuffer, rather than an independently cleared scene texture.
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1));
    const auto base_right=math::normalize_xyz(math::cross_xyz(forward,camera.up));
    const auto right=base_right*(camera.mirror_horizontal?-1.f:1.f),up=math::cross_xyz(base_right,forward);
    const float depth=std::max(1.f,camera.near_plane*2),scale=depth/camera.focal_pixels;
    for(const auto& command:compose(Layer::background)){
        const auto& instance=*slots_[command.slot];if(instance.draw_order>=4)continue;
        const auto& draw=command.geometry;
        const auto texture=unsigned(model.textures.size());
        model.textures.push_back(draw.textured?instance.movie.textures.at(draw.texture):AssetImage{"CF fill",1,1,Bytes{255,255,255,255}});
        ModelMesh mesh;mesh.texture=texture;mesh.background=true;mesh.depth_test=mesh.depth_write=false;mesh.linear_filter=true;mesh.fog_mode=0;
        for(const auto& v:draw.vertices)mesh.vertices.push_back({camera.eye+forward*depth+right*((v.x-320)*scale)+up*((224-v.y)*scale),{},v.color,v.u,v.v});
        for(auto index:draw.indices)mesh.indices.push_back(unsigned(index));
        model.meshes.push_back(std::move(mesh));
    }
}
void Interface::append_world(StaticModel& model)const{
    if(!enabled_)return;
    std::vector<const Instance*> instances;
    for(const auto& slot:slots_)if(slot&&slot->state!=3&&slot->projection==0)instances.push_back(slot.get());
    std::stable_sort(instances.begin(),instances.end(),[](const auto* a,const auto* b){return a->draw_order<b->draw_order;});
    for(const auto* instance:instances){
        // 15ce20..15d4dc: translation, X/Y/Z rotations, then local scale.
        const auto& r=instance->rotation;
        auto rx=math::identity(),ry=rx,rz=rx,scale=rx;
        rx.columns[1]={0,std::cos(r.x),std::sin(r.x),0};rx.columns[2]={0,-std::sin(r.x),std::cos(r.x),0};
        ry.columns[0]={std::cos(r.y),0,-std::sin(r.y),0};ry.columns[2]={std::sin(r.y),0,std::cos(r.y),0};
        rz.columns[0]={std::cos(r.z),std::sin(r.z),0,0};rz.columns[1]={-std::sin(r.z),std::cos(r.z),0,0};
        scale.columns[0].x=instance->scale.x;scale.columns[1].y=instance->scale.y;scale.columns[2].z=instance->scale.z;
        auto transform=math::multiply(math::multiply(math::multiply(rx,ry),rz),scale);transform.columns[3]=instance->position;
        std::map<unsigned,unsigned> textures;
        for(const auto& [id,image]:instance->movie.textures){textures[id]=unsigned(model.textures.size());model.textures.push_back(image);}
        const auto white=unsigned(model.textures.size()),node=model.nodes++;
        model.textures.push_back({"Interface solid fill",1,1,Bytes{255,255,255,255}});
        for(const auto& draw:instance->player.compose()){
            ModelMesh mesh;mesh.node=node;mesh.texture=draw.textured?textures.at(draw.texture):white;mesh.depth_write=false;
            for(const auto& v:draw.vertices)mesh.vertices.push_back({math::transform(transform,{v.x,v.y,0,1}),{},v.color,v.u,v.v});
            for(const auto index:draw.indices){if(index<0||std::size_t(index)>=mesh.vertices.size())throw std::runtime_error("Invalid world interface index");mesh.indices.push_back(unsigned(index));}
            model.meshes.push_back(std::move(mesh));
        }
    }
}
void Interface::advance(){
    for(auto& slot:slots_)if(slot && slot->state==2){slot->player.advance();pointer(*slot,false);}
    // Opcode 125 calls 1ce980 with hold=0. Completed positive fades therefore
    // deactivate; loading and room transitions rely on that behavior.
    if(fade_.alpha>0){fade_.alpha+=fade_.step;const auto rounded=std::nearbyint(fade_.alpha);if(rounded>128||rounded<=0)fade_.alpha=0;}
}
std::vector<Interface::Draw> Interface::compose(Layer layer) const {
    std::vector<Draw> result;if(!enabled_)return result;
    for(unsigned i=0;i<slots_.size();++i){const auto& slot=slots_[i];if(!slot || slot->state==3)continue;
        if(slot->projection==0)continue; // Submitted with scene geometry.
        if(slot->projection!=2)throw std::runtime_error("Interface requires an implemented projection before rendering");
        if((layer==Layer::background&&slot->draw_order>=4)||(layer==Layer::foreground&&slot->draw_order<4))continue;
        for(auto draw:slot->player.compose()){
            // 15d5a8's screen projection applies scale and translation but
            // does not read the rotation used by the other projections.
            for(auto& vertex:draw.vertices){vertex.x=vertex.x*slot->scale.x+slot->position.x+320;vertex.y=vertex.y*slot->scale.y+slot->position.y+224;}
            result.push_back({i,std::move(draw)});
        }
    }
    std::stable_sort(result.begin(),result.end(),[this](const Draw& a,const Draw& b){return slots_[a.slot]->draw_order<slots_[b.slot]->draw_order;});
    return result;
}
}
