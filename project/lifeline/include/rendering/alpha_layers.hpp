#pragma once
#include "rendering/model.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <span>
#include <vector>

namespace lifeline {
// A planar floor/decal stack is atomic, but its centroid cannot order it
// against a window above the floor. Use plane-side dependencies where a whole
// triangle lies on one side. Keep centroid order for intersecting geometry.
template<class Draw> void order_alpha_layers(std::span<Draw> draws,math::Vector4 eye){
    if(std::none_of(draws.begin(),draws.end(),[](const auto& d){return d.mesh->layer_group!=0;}))return;
    constexpr auto none=std::numeric_limits<std::size_t>::max();
    struct Unit {std::size_t first=std::numeric_limits<std::size_t>::max(),last=first,edge=first;unsigned before=0;bool group=false;};
    struct Edge {std::size_t to,next;};std::vector<Edge> edges;
    std::vector<Unit> units;units.reserve(draws.size());std::vector<std::size_t> next(draws.size(),none);
    std::map<std::pair<unsigned,unsigned>,std::size_t> groups;
    for(std::size_t i=0;i<draws.size();++i){const auto& mesh=*draws[i].mesh;std::size_t unit=units.size();
        if(mesh.layer_group){const auto [it,inserted]=groups.emplace(std::pair{mesh.node,mesh.layer_group},unit);unit=it->second;if(inserted){units.emplace_back();units.back().group=true;}}
        else units.emplace_back();
        auto& u=units[unit];if(u.last!=none)next[u.last]=i;else u.first=i;u.last=i;
    }
    const auto corners=[&](const Unit& unit,auto visit){for(auto member=unit.first;member!=none;member=next[member]){const auto& d=draws[member];for(unsigned i=0;i<3;++i)visit(d.mesh->vertices[d.mesh->indices[d.triangle+i]].position);}};
    const auto dependency=[&](std::size_t first,std::size_t second){edges.push_back({second,units[first].edge});units[first].edge=edges.size()-1;++units[second].before;};
    for(std::size_t g=0;g<units.size();++g)if(units[g].group){
        math::Vector4 normal{},origin{};float largest=0;
        for(auto member=units[g].first;member!=none;member=next[member]){const auto& d=draws[member];const auto& m=*d.mesh;
            const auto a=m.vertices[m.indices[d.triangle]].position,b=m.vertices[m.indices[d.triangle+1]].position,c=m.vertices[m.indices[d.triangle+2]].position;
            const auto n=math::cross_xyz(b+a*-1,c+a*-1);const float area=math::dot_xyz(n,n);if(area>largest){largest=area;normal=n;origin=a;}}
        if(largest<=1e-12f)continue;normal=normal*(1/std::sqrt(largest));
        const auto distance=[&](math::Vector4 p){return double(normal.x)*(double(p.x)-origin.x)+double(normal.y)*(double(p.y)-origin.y)+double(normal.z)*(double(p.z)-origin.z);};
        double low=std::numeric_limits<double>::infinity(),high=-low;
        corners(units[g],[&](auto p){const auto d=distance(p);low=std::min(low,d);high=std::max(high,d);});
        const auto camera=distance(eye);constexpr double tolerance=.001;
        if(camera>=low-tolerance&&camera<=high+tolerance)continue;
        for(std::size_t u=0;u<units.size();++u)if(u!=g){
            double a=std::numeric_limits<double>::infinity(),b=-a;
            corners(units[u],[&](auto p){const auto d=distance(p);a=std::min(a,d);b=std::max(b,d);});
            if(a>high+tolerance){if(camera>high)dependency(g,u);else dependency(u,g);}
            else if(b<low-tolerance){if(camera<low)dependency(g,u);else dependency(u,g);}
        }
    }
    // Stable topological order: among eligible draws, retain the existing
    // far-to-near order. Do not introduce a non-transitive sort comparator.
    std::priority_queue<std::size_t,std::vector<std::size_t>,std::greater<>> ready;
    for(std::size_t i=0;i<units.size();++i)if(!units[i].before)ready.push(i);
    std::vector<std::size_t> order;order.reserve(units.size());
    while(!ready.empty()){const auto u=ready.top();ready.pop();order.push_back(u);for(auto e=units[u].edge;e!=none;e=edges[e].next)if(!--units[edges[e].to].before)ready.push(edges[e].to);}
    // Conflicting intersecting stacks need geometric splitting. Preserve the
    // previous deterministic order rather than emitting a partial frame.
    if(order.size()!=units.size())return;
    std::vector<Draw> result;result.reserve(draws.size());for(auto u:order)for(auto member=units[u].first;member!=none;member=next[member])result.push_back(draws[member]);std::copy(result.begin(),result.end(),draws.begin());
}
}
