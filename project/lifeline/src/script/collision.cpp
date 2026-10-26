#include "script/collision.hpp"
#include "script/memory.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline::script {
namespace {
using V=math::Vector4;
V subtract(V a,V b){return a+b*-1.f;}
float length(V a){return std::sqrt(math::dot_xyz(a,a));}
V closest(V p,V a,V b){const auto edge=subtract(b,a);const auto square=math::dot_xyz(edge,edge);return a+edge*(square==0?0:std::clamp(math::dot_xyz(subtract(p,a),edge)/square,0.f,1.f));}
std::pair<V,V> closest_segments(V a,V b,V c,V d){
    const auto u=subtract(b,a),v=subtract(d,c),r=subtract(a,c);
    const float aa=math::dot_xyz(u,u),ee=math::dot_xyz(v,v),ff=math::dot_xyz(v,r);
    float s=0,t=0;
    if(aa==0)t=ee==0?0:std::clamp(ff/ee,0.f,1.f);
    else {const float cc=math::dot_xyz(u,r);
        if(ee==0)s=std::clamp(-cc/aa,0.f,1.f);
        else {const float bb=math::dot_xyz(u,v),denom=aa*ee-bb*bb;
            if(denom>0)s=std::clamp((bb*ff-cc*ee)/denom,0.f,1.f);
            t=(bb*s+ff)/ee;
            if(t<0){t=0;s=std::clamp(-cc/aa,0.f,1.f);}
            else if(t>1){t=1;s=std::clamp((bb-cc)/aa,0.f,1.f);}
        }
    }
    return {a+u*s,c+v*t};
}
bool inside_quad(V p,const std::array<V,6>& points){
    const auto first=math::cross_xyz(subtract(p,points[0]),subtract(points[1],points[0]));
    for(unsigned i=1;i<4;++i)if(math::dot_xyz(first,math::cross_xyz(subtract(p,points[i]),subtract(points[(i+1)%4],points[i])))<=0)return false;
    return true;
}
int integer(float v){if(!std::isfinite(v) || double(v)<-2147483648. || double(v)>=2147483648.)throw std::runtime_error("Invalid collision integer");return int(v);}
float read(const Invocation& c,unsigned word){
    if(!c.state || !c.state->data_contains(c.program,word,1))throw std::runtime_error("Collision table outside live script data");
    const auto i=c.state->data->find(word);const float v=i==c.state->data->end()?std::bit_cast<float>(c.program.word(word)):i->second;
    if(!std::isfinite(v))throw std::runtime_error("Non-finite collision table value");return v;
}
unsigned address(float v){const float bytes=v*4;const double n=std::trunc(double(bytes+std::copysign(.5f,bytes)));
    if(!std::isfinite(n) || n<0 || n>2147483647. || std::fmod(n,4)!=0)throw std::runtime_error("Invalid collision table address");return unsigned(n)/4;
}
}
void Collision::update(Record& r){
    if(r.shape==Shape::none)return;
    if(r.source==0 && r.part==0){r.world=r.local;return;}
    if(!attachment_)throw std::runtime_error("Collision attachment resolver is unavailable");
    const auto transform=attachment_(r.source,r.part);
    const unsigned count=r.shape==Shape::sphere?1:r.shape==Shape::capsule?2:6;
    for(unsigned i=0;i<count;++i)r.world[i]=math::transform(transform,r.local[i]);
    // The original transforms the stored quad normal with W=0. Radii are not scaled.
}
int Collision::add(Record r){
    const auto slot=std::find_if(records_.begin(),records_.end(),[](const Record& v){return v.shape==Shape::none;});
    if(slot==records_.end())return -1;
    if(r.shape==Shape::none)throw std::runtime_error("Cannot allocate an empty collision shape");
    if(r.shape==Shape::quad){
        r.local[4]=(r.local[0]+r.local[1]+r.local[2]+r.local[3])*.25f;
        r.local[5]=math::normalize_xyz(math::cross_xyz(subtract(r.local[1],r.local[2]),subtract(r.local[1],r.local[0])));
    }
    update(r);*slot=r;return int(slot-records_.begin());
}
void Collision::remove(int slot){
    if(slot<0 || slot>=int(records_.size()))return;
    auto& r=records_[slot];r.shape=Shape::none;r.source=r.part=r.owner=r.event=r.secondary=-1;r.mask=r.flags=r.material=0;
}
void Collision::remove_owner(int owner){for(unsigned i=0;i<records_.size();++i)if(records_[i].owner==owner)remove(int(i));}
void Collision::clear(){for(unsigned i=0;i<records_.size();++i)remove(int(i));}
void Collision::advance(){for(auto& record:records_)update(record);}
std::optional<Collision::Hit> Collision::overlap(Vector center,float radius,int owner,int mask,const Vector* direction,bool match_owner)const{
    std::optional<Hit> best;
    for(unsigned i=0;i<records_.size();++i){
        const auto& r=records_[i];if(r.shape==Shape::none || (owner>=0 && ((r.owner==owner)!=match_owner)) || !(r.mask&mask))continue;
        Hit h;h.slot=int(i);h.event=r.event;h.flags=r.flags;h.material=r.material;
        if(r.shape==Shape::sphere || r.shape==Shape::capsule){
            const auto p=r.shape==Shape::sphere?r.world[0]:closest(center,r.world[0],r.world[1]);
            const auto direction=subtract(center,p);h.distance=length(direction);
            if(h.distance>radius+r.radius)continue;
            h.normal=math::normalize_xyz(direction);h.point=p+h.normal*r.radius;
        }else{
            const auto n=r.world[5];const float distance=math::dot_xyz(subtract(center,r.world[4]),n);
            if(distance<=-radius || distance>radius)continue;
            const auto projection=center+n*(-distance);bool inside=true;
            const auto first=math::cross_xyz(subtract(projection,r.world[0]),subtract(r.world[1],r.world[0]));
            for(unsigned edge=1;edge<4;++edge){
                const auto cross=math::cross_xyz(subtract(projection,r.world[edge]),subtract(r.world[(edge+1)%4],r.world[edge]));
                if(math::dot_xyz(first,cross)<=0){inside=false;break;}
            }
            if(inside){h.distance=std::abs(distance);h.normal=n;h.point=projection;}
            else {
                if(distance<0)continue;
                for(unsigned edge=0;edge<4;++edge){
                    const auto point=closest(center,r.world[edge],r.world[(edge+1)%4]);const float d=length(subtract(center,point));
                    if(d<=radius && (h.distance<0 || d<h.distance)){h.distance=d;h.point=point;h.normal=math::normalize_xyz(subtract(center,point));}
                }
            }
        }
        if(direction && math::dot_xyz(h.normal,*direction)<-.1f)continue;
        if(h.distance>0 && (!best || h.distance<best->distance))best=h;
    }
    if(best)last_hit_=best;
    return best;
}
Bindings Collision::bindings(){
    Bindings b;
    b.emplace(1022,Binding{3,0,[this](const Invocation& c){
        // 106988 -> 14fa98 -> 14e288 queries the installed occupancy grid,
        // not the dynamic sphere/capsule pool. Only descriptor type 3 is used.
        const auto base=address(c.arguments[0]);if(integer(read(c,base))!=3)return Reply{false,-1.f};
        const int source=integer(read(c,base+1)),part=integer(read(c,base+2));
        Vector first{read(c,base+3),read(c,base+4),read(c,base+5),1},second{read(c,base+6),read(c,base+7),read(c,base+8),1};
        if(source||part){if(!attachment_)throw std::runtime_error("Grid query attachment resolver is unavailable");const auto matrix=attachment_(source,part);first=math::transform(matrix,first);second=math::transform(matrix,second);first.w=second.w=1;}
        const auto hit=grid_.segment(first,second);if(!hit)return Reply{false,-1.f};
        // 14e288 never produces a normal. 106bcc copies uninitialized scratch
        // bytes to that output in the console executable. Native policy is to
        // preserve the destination; do not invent a surface normal or copy
        // uninitialized C++ storage. The opening impact routine uses only XYZ
        // from the first output (66b44..66ba0).
        const auto unused_output=address(c.arguments[2]);
        if(!c.state->data_contains(c.program,unused_output,4))throw std::runtime_error("Grid secondary output outside live data");
        {const auto output=address(c.arguments[1]);if(!c.state->data_contains(c.program,output,4))throw std::runtime_error("Grid hit output outside live data");
            const std::array<float,4> value{hit->point.x,hit->point.y,hit->point.z,hit->point.w};for(unsigned i=0;i<4;++i)(*c.state->data)[output+i]=value[i];}
        return Reply{false,hit->distance};
    }});
    b.emplace(1014,Binding{5,0,[this](const Invocation& c){
        const int count=integer(c.arguments[0]);unsigned cursor=address(c.arguments[1]);
        const int owner=integer(c.arguments[2]),mask=integer(c.arguments[3]),material=integer(c.arguments[4]);
        int last=-1;
        for(int i=0;i<count;++i){
            Record r;const int type=integer(read(c,cursor++));
            if(type<0 || type>2){
                // The entry_020 elevator table declares five entries but
                // contains four complete spheres followed by the next
                // constant (500). Accept that observed terminal overcount;
                // other malformed tables must still fail visibly.
                if(count==5 && i==4 && type==500 && last>=0)break;
                throw std::runtime_error("Unknown collision table shape: "+std::to_string(type)+
                    " count "+std::to_string(count)+" entry "+std::to_string(i)+" word "+std::to_string(cursor-1));
            }
            r.shape=Shape(type+1);r.source=integer(read(c,cursor++));r.part=integer(read(c,cursor++));
            const unsigned points=type==0?1:type==1?2:4;
            for(unsigned p=0;p<points;++p){const float x=read(c,cursor++),y=read(c,cursor++),z=read(c,cursor++);r.local[p]={x,y,z,1};}
            if(type!=2)r.radius=read(c,cursor++);
            r.owner=owner;r.mask=mask;r.material=material;r.event=r.secondary=r.flags=0;
            last=add(r);
        }
        return Reply{false,float(last)};
    }});
    b.emplace(1016,Binding{1,0,[this](const Invocation& c){remove(integer(c.arguments[0]));return Reply{false,0.f};}});
    b.emplace(1017,Binding{1,0,[this](const Invocation& c){remove_owner(integer(c.arguments[0]));return Reply{false,0.f};}});
    b.emplace(1018,Binding{0,0,[this](const Invocation&){clear();return Reply{false,0.f};}});
    b.emplace(1013,Binding{1,0,[this](const Invocation& c){
        if(!grid_loader_)throw std::runtime_error("Room collision resource service is not connected");
        const auto name=read_script_string(c,float(integer(c.arguments[0]))*4.f);grid_.load(grid_loader_(name));return Reply{false,0.f};
    }});
    b.emplace(1015,Binding{0,0,[this](const Invocation&){grid_.reset();return Reply{false,0.f};}});
    b.emplace(1019,Binding{0,0,[this](const Invocation&){clear();grid_.reset();return Reply{false,0.f};}});
    b.emplace(1020,Binding{5,0,[this](const Invocation& c){
        unsigned cursor=address(c.arguments[0]);const int owner=integer(c.arguments[1]),mask=integer(c.arguments[2]);
        const int type=integer(read(c,cursor++));
        // Native handler returns -1 for other query shapes without writing outputs.
        if(type<0 || type>1)return Reply{false,-1.f};
        const int source=integer(read(c,cursor++)),part=integer(read(c,cursor++));
        const auto point=[&](){const float x=read(c,cursor++),y=read(c,cursor++),z=read(c,cursor++);return Vector{x,y,z,1};};
        auto first=point(),second=type==1?point():first;const float radius=read(c,cursor++);
        if(source!=0 || part!=0){
            if(!attachment_)throw std::runtime_error("Collision query attachment resolver is unavailable");
            const auto matrix=attachment_(source,part);first=math::transform(matrix,first);second=math::transform(matrix,second);first.w=second.w=1;
        }
        const bool match_owner=c.opcode==1021 || c.opcode==1026;
        const auto hit=type==0?overlap(first,radius,owner,mask,nullptr,match_owner):overlap_capsule(first,second,radius,owner,mask,nullptr,match_owner);
        if(!hit)return Reply{false,-1.f};
        std::array<unsigned,2> output{};
        // 103c68 adds the program base even for offset zero; zero is a real
        // script destination here, not a null pointer sentinel.
        for(unsigned i=0;i<2;++i){output[i]=address(c.arguments[i+3]);if(!c.state->data_contains(c.program,output[i],4))throw std::runtime_error("Collision query output outside live data");}
        const Vector values[]{hit->point,hit->normal};
        for(unsigned i=0;i<2;++i){const auto v=values[i];const float words[]{v.x,v.y,v.z,v.w};for(unsigned j=0;j<4;++j)(*c.state->data)[output[i]+j]=words[j];}
        return Reply{false,hit->distance};
    }});
    b.emplace(1023,Binding{4,0,[this](const Invocation& c){
        // 105108: resolve overlap, retaining each contact normal to reject
        // opposing contacts on subsequent iterations (14fc78 / 150028).
        unsigned cursor=address(c.arguments[0]);const int owner=integer(c.arguments[1]),mask=integer(c.arguments[2]);
        const int type=integer(read(c,cursor++));if(type<0 || type>1)return Reply{false,-1.f};
        const int source=integer(read(c,cursor++)),part=integer(read(c,cursor++));
        const auto point=[&](){const float x=read(c,cursor++),y=read(c,cursor++),z=read(c,cursor++);return Vector{x,y,z,1};};
        auto first=point(),second=type==1?point():first;const float radius=read(c,cursor++);
        if(source!=0 || part!=0){
            if(!attachment_)throw std::runtime_error("Collision query attachment resolver is unavailable");
            const auto matrix=attachment_(source,part);first=math::transform(matrix,first);second=math::transform(matrix,second);first.w=second.w=1;
        }
        Vector previous{0,0,0,1};float result=-1;
        for(unsigned iteration=0;iteration<20;++iteration){
            const bool match_owner=c.opcode==1024;
            const auto hit=type==0?overlap(first,radius,owner,mask,&previous,match_owner):overlap_capsule(first,second,radius,owner,mask,&previous,match_owner);
            if(!hit)break;
            if(type==0)first=hit->point+hit->normal*(radius+.1f);
            else {
                const auto nearest=closest(hit->point,first,second);auto delta=subtract(nearest,hit->point);
                float distance=length(delta);float alignment=math::dot_xyz(math::normalize_xyz(delta),hit->normal);
                float correction;
                if(alignment<0){
                    if(alignment>-.9f){
                        delta=subtract(first,hit->point);distance=length(delta);
                        alignment=math::dot_xyz(math::normalize_xyz(delta),hit->normal);
                        if(alignment>-.9f)distance=length(subtract(hit->point,second));
                    }
                    correction=distance+radius;
                }else correction=radius-distance;
                const auto shift=hit->normal*(correction+.1f);first=first+shift;second=second+shift;
            }
            previous=hit->normal;result=hit->distance;
        }
        if(result>=0){
            const auto output=address(c.arguments[3]);if(!c.state->data_contains(c.program,output,4))throw std::runtime_error("Collision resolution output outside live data");
            const float words[]{first.x,first.y,first.z,1};for(unsigned i=0;i<4;++i)(*c.state->data)[output+i]=words[i];
        }
        return Reply{false,result};
    }});
    b.emplace(1021,b.at(1020)); // 150228/1505d8 require the specified owner.
    // 1025/1026 call the direction-filtered helpers with XYZ=(0,0,0),
    // which admits every finite normal; their other arguments/results match.
    b.emplace(1025,b.at(1020));
    b.emplace(1026,b.at(1020));
    b.emplace(1024,b.at(1023)); // Owner-matching resolution via 1503e8/150798.
    return b;
}
std::optional<Collision::Hit> Collision::overlap_capsule(Vector a,Vector b,float radius,int owner,int mask,const Vector* direction,bool match_owner)const{
    std::optional<Hit> best;
    for(unsigned i=0;i<records_.size();++i){
        const auto& r=records_[i];if(r.shape==Shape::none || (owner>=0 && ((r.owner==owner)!=match_owner)) || !(r.mask&mask))continue;
        Hit h;h.slot=int(i);h.event=r.event;h.flags=r.flags;h.material=r.material;
        if(r.shape==Shape::sphere || r.shape==Shape::capsule){
            const auto pair=r.shape==Shape::sphere?std::pair{r.world[0],closest(r.world[0],a,b)}:closest_segments(r.world[0],r.world[1],a,b);
            h.distance=length(subtract(pair.second,pair.first));if(h.distance>r.radius+radius)continue;
            h.normal=math::normalize_xyz(subtract(pair.second,pair.first));h.point=pair.first+h.normal*r.radius;
        }else{
            const auto n=r.world[5];const float da=math::dot_xyz(subtract(a,r.world[4]),n),db=math::dot_xyz(subtract(b,r.world[4]),n);
            if(da!=db && ((da<=0 && db>=0)||(da>=0 && db<=0))){
                const auto crossing=a+subtract(b,a)*(da/(da-db));
                if(inside_quad(crossing,r.world)){
                    // 14d290 selects the endpoint behind the plane, rather than
                    // returning zero distance at the segment-plane intersection.
                    const auto endpoint=da>0?b:a;const float distance=da>0?db:da;
                    h.distance=-distance;h.normal=n;h.point=endpoint+n*(-distance);
                }else for(unsigned edge=0;edge<4;++edge){
                    const auto pair=closest_segments(r.world[edge],r.world[(edge+1)%4],a,b);const float d=length(subtract(pair.second,pair.first));
                    if(d<=radius && (h.distance<0 || d<h.distance)){h.distance=d;h.point=pair.first;h.normal=math::normalize_xyz(subtract(pair.second,pair.first));}
                }
            }else{
                // 14d3b8 queries each endpoint sphere against this quad only.
                Collision single;single.records_[0]=r;
                auto first=single.overlap(a,radius,-1,mask),second=single.overlap(b,radius,-1,mask);
                if(first)h=*first;
                if(second && (!first || second->distance<first->distance))h=*second;
                h.slot=int(i);
            }
        }
        if(direction && math::dot_xyz(h.normal,*direction)<-.1f)continue;
        if(h.distance>0 && (!best || h.distance<best->distance))best=h;
    }
    if(best)last_hit_=best;
    return best;
}
}
