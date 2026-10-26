#pragma once
#include "core/math.hpp"
#include "script/runtime.hpp"
#include "game/collision_grid.hpp"
#include <array>
#include <functional>
#include <optional>

namespace lifeline::script {
// Native 8091 service: independent dynamic shape pool and installed room grid.
class Collision {
public:
    using Vector=math::Vector4;
    using Attachment=std::function<math::Matrix4(int,int)>;
    enum class Shape {none,sphere,capsule,quad};
    struct Record {
        Shape shape=Shape::none;
        int source=-1,part=-1,owner=-1,mask=0,event=-1,secondary=-1,flags=0,material=0;
        std::array<Vector,6> local{},world{};
        float radius=0;
    };
    struct Hit {int slot=-1,event=0,flags=0,material=0;float distance=-1;Vector point{},normal{};};
private:
    std::array<Record,64> records_{};
    Attachment attachment_;
    mutable std::optional<Hit> last_hit_;
    CollisionGrid grid_;
    std::function<Bytes(const std::string&)> grid_loader_;
    void update(Record&);
public:
    explicit Collision(Attachment attachment={}):attachment_(std::move(attachment)){}
    int add(Record);
    void remove(int);
    void remove_owner(int);
    void clear();
    void grid_loader(std::function<Bytes(const std::string&)> loader){grid_loader_=std::move(loader);}
    CollisionGrid& grid(){return grid_;}
    const CollisionGrid& grid()const{return grid_;}
    void reset_grid(){grid_.reset();}
    void advance();
    const auto& records()const{return records_;}
    const auto& last_hit()const{return last_hit_;}
    // 14fab8: sphere overlap, strictly positive nearest distance; negative owner
    // excludes nobody. This is not a swept ray test.
    std::optional<Hit> overlap(Vector center,float radius,int owner,int mask,const Vector* direction=nullptr,bool match_owner=false)const;
    std::optional<Hit> overlap_capsule(Vector first,Vector second,float radius,int owner,int mask,const Vector* direction=nullptr,bool match_owner=false)const;
    Bindings bindings();
};
}
