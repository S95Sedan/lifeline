#pragma once
#include "assets/asset_directory.hpp"
#include "script/runtime.hpp"
#include <array>
#include "rendering/model.hpp"

namespace lifeline {
// Installed analysis records retain their authored group and callback IDs.
class Interaction {
public:
    struct Record {
        std::array<int,8> fields{};
        std::array<float,4> point{};
        std::string label,description;
        unsigned event()const{return (unsigned(fields[6])>>16)&65535;}
        unsigned kind()const{return unsigned(fields[7])>>16;}
        unsigned control()const{return (unsigned(fields[7])&65535)+1;}
    };
    enum class View {closed,label,description,controls};
private:
    std::vector<Record> records_;
    const Record* active_=nullptr;
    View view_=View::closed;
    bool open_=true;
    std::uint64_t revision_=0;
public:
    Interaction()=default;
    explicit Interaction(std::span<const unsigned char> table);
    void load(std::span<const unsigned char> table);
    const Record* available(int group,script::FlagBank&)const;
    std::vector<const Record*> eligible(int group,script::FlagBank&)const;
    void begin(int group,script::FlagBank&);
    bool direction(int action,int group,script::FlagBank&,const SceneCamera&);
    bool describe(script::FlagBank&);
    static std::optional<math::Vector4> project(const Record&,const SceneCamera&);
    void labels(){if(active_){view_=View::label;++revision_;}}
    void inspect(const Record&);
    void controls();
    bool cancel();
    void dismiss(){view_=View::closed;active_=nullptr;}
    void select(bool open){if(view_==View::controls)open_=open;}
    bool selected_open()const{return open_;}
    View view()const{return view_;}
    const Record* active()const{return active_;}
    auto revision()const{return revision_;}
};
}
