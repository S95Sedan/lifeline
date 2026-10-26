#pragma once
#include "assets/resources.hpp"
#include "ui/interaction.hpp"
namespace lifeline::script {
// Asset-selection portion of analysis service 8092. Installed conversions
// supply the records; authored CB messages select their presentation assets.
class AnalysisAssets {
    Resources& resources_;
    Interaction& interaction_;
    std::string table_;
    std::function<std::string()> label_,description_;
    std::array<std::function<std::string()>,3> markers_,highlights_;
    std::function<float(unsigned)> layout_;
    FlagBank flags_;
    int group_=0;
    bool active_=false;
    std::function<void(int,int,int)> notify_;
    std::function<SceneCamera()> camera_;
    std::function<void(unsigned)> control_,event_,sound_;
public:
    AnalysisAssets(Resources& r,Interaction& i):resources_(r),interaction_(i){}
    Bindings bindings();
    const std::string& table()const{return table_;}
    std::string label_path()const;
    std::string description_path()const;
    void connect(FlagBank flags,std::function<void(int,int,int)> notify,std::function<SceneCamera()> camera,std::function<void(unsigned)> control,std::function<void(unsigned)> event,std::function<void(unsigned)> sound){flags_=flags;notify_=std::move(notify);camera_=std::move(camera);control_=std::move(control);event_=std::move(event);sound_=std::move(sound);}
    int message(int,int=0,int=0);
    bool advance(){return active_;}
    bool active()const{return active_;}
    bool input(int);
    void complete(bool);
    std::vector<const Interaction::Record*> records(){return interaction_.eligible(group_,flags_);}
    std::string marker_path(unsigned kind,bool selected)const;
    std::optional<math::Vector4> position(const Interaction::Record&,unsigned pair,const SceneCamera&)const;
};
}
