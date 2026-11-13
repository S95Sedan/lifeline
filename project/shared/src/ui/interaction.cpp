#include "ui/interaction.hpp"
#include "rendering/renderer.hpp"
#include <sstream>
#include <charconv>
#include <cmath>

namespace lifeline {
namespace {
std::string unescape(const std::string& text){
    std::string result;
    for(std::size_t i=0;i<text.size();++i){
        if(text[i]!='\\'){result+=text[i];continue;}
        if(++i==text.size())throw std::runtime_error("Invalid interaction escape");
        switch(text[i]){case '\\':result+='\\';break;case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;default:throw std::runtime_error("Unknown interaction escape");}
    }return result;
}
template<class T>T number(const std::string& s){T value{};auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),value);if(error!=std::errc{} || end!=s.data()+s.size())throw std::runtime_error("Invalid interaction number");return value;}
}
Interaction::Interaction(std::span<const unsigned char> table){
    std::istringstream input(std::string(table.begin(),table.end()));std::string line;
    if(!std::getline(input,line) || line!="LIFELINE_INTERACTIONS\t1")throw std::runtime_error("Unsupported interaction table");
    while(std::getline(input,line)){
        std::vector<std::string> fields;std::size_t start=0;
        for(;;){auto end=line.find('\t',start);fields.push_back(line.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}
        if(fields.size()!=15 || number<unsigned>(fields[0])!=records_.size())throw std::runtime_error("Invalid interaction record");
        Record r;for(unsigned i=0;i<8;++i)r.fields[i]=number<int>(fields[i+1]);
        for(unsigned i=0;i<4;++i){r.point[i]=number<float>(fields[i+9]);if(!std::isfinite(r.point[i]))throw std::runtime_error("Invalid interaction point");}
        r.label=unescape(fields[13]);r.description=unescape(fields[14]);records_.push_back(std::move(r));
    }
}
void Interaction::load(std::span<const unsigned char> table){
    Interaction replacement(table);dismiss();records_=std::move(replacement.records_);++revision_;
}
const Interaction::Record* Interaction::available(int group,script::FlagBank& flags)const{
    const auto records=eligible(group,flags);return records.empty()?nullptr:records.front();
}
std::vector<const Interaction::Record*> Interaction::eligible(int group,script::FlagBank& flags)const{
    std::vector<const Record*> result;
    for(const auto& r:records_){
        if(r.fields[0]!=group || r.kind()>=3)continue;
        bool enabled=true;
        for(unsigned i:{1u,3u})if(r.fields[i]>0)enabled&=int(flags[unsigned(r.fields[i])])==r.fields[i+1];
        if(enabled)result.push_back(&r);
    }return result;
}
void Interaction::begin(int group,script::FlagBank& flags){active_=available(group,flags);view_=active_?View::label:View::closed;++revision_;}
bool Interaction::describe(script::FlagBank& flags){
    if(!active_ || (active_->kind()!=1 && active_->kind()!=2))return false;
    if(active_->fields[5]>=0){if(active_->fields[5]>=32768)throw std::runtime_error("Analysis discovery flag out of range");flags[unsigned(active_->fields[5])]=(unsigned(active_->fields[6])&65535)!=0;}
    view_=View::description;++revision_;return true;
}
std::optional<math::Vector4> Interaction::project(const Record& record,const SceneCamera& camera){
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1.f));
    auto right=math::normalize_xyz(math::cross_xyz(forward,camera.up));
    const auto up=math::cross_xyz(right,forward);
    if(camera.mirror_horizontal)right=right*(-1.f);
        const math::Vector4 delta{record.point[0]-camera.eye.x,record.point[1]-camera.eye.y,record.point[2]-camera.eye.z,0};
        const float depth=math::dot_xyz(delta,forward);if(depth<=camera.near_plane)return {};
        const float x=math::dot_xyz(delta,right)*camera.focal_pixels/depth,y=-math::dot_xyz(delta,up)*camera.focal_pixels/depth;
        const auto [width,height]=scene_render_size();
        const float scale=width&&height?std::min(width/640.f,height/448.f):1.f;
        const float half_width=width?width/(2*scale):320.f;
        const float half_height=height?std::max(240.f,height/(2*scale)):240.f;
        if(x< -half_width||x>half_width||y< -half_height||y>half_height)return {};return math::Vector4{x,y,depth,1};
}
bool Interaction::direction(int action,int group,script::FlagBank& flags,const SceneCamera& camera){
    if(!active_||(action!=2&&action!=3)||view_==View::controls)return false;
    const auto origin=project(*active_,camera);if(!origin)return false;
    const Record* selected=nullptr;float distance=1000000;
    for(const auto* record:eligible(group,flags)){if(record==active_)continue;const auto x=project(*record,camera);if(!x)continue;
        const float delta=x->x-origin->x;if((action==2&&delta>0)||(action==3&&delta<0))continue;
        if(delta*delta<=distance){distance=delta*delta;selected=record;}}
    if(!selected)return false;active_=selected;++revision_;if(view_==View::description){view_=View::label;describe(flags);}return true;
}
void Interaction::inspect(const Record& record){active_=&record;view_=View::description;open_=true;++revision_;}
void Interaction::controls(){if(active_ && active_->kind()==2 && view_==View::description){view_=View::controls;open_=true;}}
bool Interaction::cancel(){if(view_==View::closed)return false;if(view_==View::controls)view_=View::description;else dismiss();return true;}
}
