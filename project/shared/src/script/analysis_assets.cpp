#include "script/analysis_assets.hpp"
#include <bit>
#include <cmath>

namespace lifeline::script {
namespace {
int integer(float value){
    if(!std::isfinite(value)||double(value)<-2147483648.||double(value)>2147483647.)throw std::runtime_error("Invalid analysis message argument");return int(value);
}
std::string string(const Invocation& c,float address){
    const int word=integer(address);if(word<0)throw std::runtime_error("Negative analysis string pointer");
    std::string text;
    for(std::uint64_t byte=std::uint64_t(word)*4;;++byte){
        const auto cell=unsigned(byte/4);
        if(byte>2147483647u || (c.state?!c.state->data_contains(c.program,cell):cell>=c.program.size()))throw std::runtime_error("Analysis string outside live script data");
        auto raw=c.program.size()>cell?c.program.word(cell):0;
        if(c.state){const auto found=c.state->data->find(cell);if(found!=c.state->data->end())raw=std::bit_cast<unsigned>(found->second);}
        const char ch=char((raw>>(8*(byte%4)))&255);if(!ch)return text;text+=ch;
    }
}
}
Bindings AnalysisAssets::bindings(){
    return {{1039,Binding{3,0,[this](const Invocation& c){return Reply{false,float(message(integer(c.arguments[0]),integer(c.arguments[1]),integer(c.arguments[2])))};}}},
        {1040,Binding{3,0,[this](const Invocation& c){
        const auto message=integer(c.arguments[0]),slot=integer(c.arguments[2]);
        if(message==35008){
            auto program=std::make_shared<Program>(c.program);State source;if(c.state){source.data=c.state->data;source.arrays=c.state->arrays;}
            const int start=integer(c.arguments[1]);if(start<0)throw std::runtime_error("Negative analysis layout pointer");
            auto read=[program,source,start](unsigned offset){const auto word=std::uint64_t(start)+offset;if(word>UINT32_MAX||!source.data_contains(*program,unsigned(word)))throw std::runtime_error("Analysis layout outside live script data");const auto it=source.data->find(unsigned(word));const float value=it==source.data->end()?std::bit_cast<float>(program->word(unsigned(word))):it->second;if(!std::isfinite(value))throw std::runtime_error("Invalid analysis layout coordinate");return value;};
            for(unsigned i=0;i<144;++i)read(i);layout_=std::move(read);return Reply{false,1.f};
        }
        if(message!=35000 && message!=35001 && message!=35004 && message!=35002 && message!=35007)throw std::runtime_error("Analysis pointer protocol not implemented: "+std::to_string(message));
        if((message==35002||message==35007)&&(slot<0||slot>=3))return Reply{false,1.f};
        auto name=string(c,c.arguments[1]);
        if(message==35000){
            const auto* files=resources_.find_loaded_files(name);
            if(!files)throw std::runtime_error("Analysis resource is not loaded: "+name);
            const auto table=std::find_if(files->begin(),files->end(),[](const auto& f){return f.conversion=="interaction-tsv-v1";});
            if(table==files->end()||!table->bytes)throw std::runtime_error("Analysis resource has no installed interaction conversion: "+name);
            interaction_.load(*table->bytes);table_=std::move(name);
        }else {
            // 1459cc/1459d8 retain script pointers, not a copy of their text.
            // Immutable code stays owned; writable data/allocation lifetimes
            // are shared with the originating script workers.
            auto program=std::make_shared<Program>(c.program);State source;
            if(c.state){source.data=c.state->data;source.arrays=c.state->arrays;}
            auto read=[program,source=std::move(source),address=c.arguments[1]]()mutable{
                return string({*program,0,1040,{},{},&source},address);
            };
            if(message==35001)description_=std::move(read);
            else if(message==35004)label_=std::move(read);
            else (message==35002?highlights_:markers_)[unsigned(slot)]=std::move(read);
        }
        // 145408 enqueues and drains immediately, returning one independently
        // of the table loader's internal status. Missing installed data is an
        // explicit port error instead of a silently unusable analysis panel.
        return Reply{false,1.f};
    }}}};
}
int AnalysisAssets::message(int code,int first,int){
    switch(code){
    case 34000:active_=true;if(group_)interaction_.begin(group_,flags_);if(!notify_)throw std::runtime_error("Analysis command owner not connected");notify_(34007,1,1);break;
    case 34001:active_=false;interaction_.dismiss();break;
    case 34003:active_=false;group_=0;interaction_.dismiss();break;
    case 34002:input(first);break;
    case 35005:if(group_!=first){group_=first;if(active_&&group_)interaction_.begin(group_,flags_);else interaction_.dismiss();}break;
    case 35010:complete(first!=0);break;
    default:throw std::runtime_error("Analysis numeric protocol not implemented: "+std::to_string(code));
    }return 1;
}
bool AnalysisAssets::input(int action){
    if(!active_||interaction_.view()==Interaction::View::closed)return false;
    // Once an authored object control has opened, its room worker owns every
    // direction through the ordinary pad service.  Consuming those presses in
    // the inspection frontend prevents controls such as the Utility Unit's
    // eight-direction lift from seeing them at all.
    if(interaction_.view()==Interaction::View::controls&&action>=0&&action<=3)return false;
    if(action==2||action==3){if(camera_&&interaction_.direction(action,group_,flags_,camera_())){if(sound_)sound_(0);}return true;}
    if(action==5){if(interaction_.view()==Interaction::View::description){interaction_.labels();if(sound_)sound_(2);}return true;}
    if(action!=6)return true;
    if(interaction_.view()==Interaction::View::label){if(interaction_.describe(flags_)&&sound_)sound_(0);}
    else if(interaction_.view()==Interaction::View::description&&interaction_.active()->kind()==2){interaction_.controls();if(sound_)sound_(0);if(!control_)throw std::runtime_error("Analysis control owner not connected");control_(interaction_.active()->control());}
    return true;
}
void AnalysisAssets::complete(bool accepted){
    if(interaction_.view()!=Interaction::View::controls)return;
    const auto callback=interaction_.active()->event();
    if(!accepted){interaction_.cancel();return;}
    // An accepted object operation hands control to its authored callback.
    // Leaving the inspection description open makes the native frontend keep
    // claiming arrows while room workers (including the eight-direction
    // Utility Unit lift) are polling the ordinary pad service.
    interaction_.dismiss();
    if(!event_)throw std::runtime_error("Analysis event owner not connected");event_(callback);
}
std::string AnalysisAssets::label_path()const{
    if(!label_)throw std::runtime_error("Analysis label CF has not been selected");return resources_.find_raw(label_()).path;
}
std::string AnalysisAssets::description_path()const{
    if(!description_)throw std::runtime_error("Analysis description CF has not been selected");return resources_.find_raw(description_()).path;
}
std::string AnalysisAssets::marker_path(unsigned kind,bool selected)const{
    const auto& read=(selected?highlights_:markers_).at(kind);if(!read)throw std::runtime_error("Analysis marker CF has not been selected");return resources_.installed_raw("sns/ac.a",read()).path;
}
std::optional<math::Vector4> AnalysisAssets::position(const Interaction::Record& record,unsigned pair,const SceneCamera& camera)const{
    auto point=Interaction::project(record,camera);if(!point)return {};
    if(!layout_)throw std::runtime_error("Analysis layout has not been selected");
    // Projection already checks the active scene bounds. Extended side areas
    // use the nearest authored layout region rather than discarding the label.
    const float x=point->x+320,y=point->y+224;
    const unsigned region=(y<142?0:y<276?1:2)*3+(x<237?0:x<412?1:2);
    if(pair>=8)throw std::runtime_error("Invalid analysis layout pair");
    point->x=x+layout_(region*16+pair*2);point->y=y+layout_(region*16+pair*2+1);return point;
}
}
