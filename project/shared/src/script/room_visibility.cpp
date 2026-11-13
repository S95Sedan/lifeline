#include "script/room_visibility.hpp"
#include "game/objects.hpp"

namespace lifeline::script {
std::array<unsigned char,374> cylinder_room_visibility(math::Vector4 eye,FlagBank flags){
    if(!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z))throw std::runtime_error("Invalid room visibility camera");
    const float position=729.5f*std::atan2(eye.z,729.5f-eye.y);
    std::array<unsigned char,374> visible{};
    const auto show=[&](std::initializer_list<unsigned> nodes){for(auto node:nodes)visible.at(node)=1;};
    show({148});
    if(position<560){
        show({1,2,3,4,5,43,45,70,71,72,73,89,90,91,92,93,104,105,107,108,109,112,120,121,122,123,149,161,172,173,174,181,182,183,328,329,330,331,332,333,334,335,345,347,354,365,366,35,195,206,217,228,239,250});
        if(!flags[31])show({39,199,210,221,232,243,254});
    }else if(position>1730){
        show({24,25,26,64,81,82,102,103,116,117,144,145,146,147,154,175,176,177,184,185,186,197,208,219,230,241,252,304,305,306,307,308,309,310,311,336,337,338,339,340,341,342,343,346,368,369});
    }
    if(position>=485 && position<=1805){
        if(eye.x>=0){
            show({3,6,7,8,9,25,47,48,49,72,74,76,81,85,91,94,96,101,109,110,111,117,150,155,162,163,172,175,178,181,185,187,256,257,258,259,260,261,262,263,280,281,282,283,284,285,286,287,304,305,306,307,308,309,310,311,348,355,356,365,365,371,30,190,201,212,223,234,245,33,193,204,215,226,237,248,37,197,208,219,230,241,252});
        }else{
            show({4,11,12,13,14,16,17,18,19,21,22,23,24,52,53,54,56,57,58,59,60,61,73,75,77,78,79,80,82,86,87,88,92,93,95,97,98,99,102,103,106,112,114,116,124,125,126,127,128,129,130,131,136,137,138,139,140,141,142,143,144,145,146,147,151,152,153,156,157,158,164,165,166,167,168,169,173,175,179,180,182,184,188,189,264,265,266,267,268,269,270,271,288,289,290,291,292,293,294,295,312,313,314,315,316,317,318,319,320,321,322,323,324,325,326,327,336,337,338,339,340,341,342,343,348,349,350,351,357,358,359,360,361,362,366,368,372,373});
            if(!flags[8])show({31,191,202,213,224,235,246});
            if(flags[5] && !flags[863])show({34,194,205,216,227,238,249});
            if(flags[40] && !flags[861])show({38,198,209,220,231,242,253});
            if(flags[40] && !flags[862])show({40,200,211,222,233,244,255});
        }
        if(position>=892 && position<=980){
            show({9,14,76,77,96,97,113,132,133,134,135,178,179,187,188,272,273,274,275,276,277,278,279,371,372});
            if(!flags[2])show({32,192,203,214,225,236,247});
        }
        if(position>=1312 && position<=1400)show({114,115,36,196,207,218,229,240,251});
    }
    return visible;
}
void Objects::update_room_visibility(math::Vector4 eye){
    auto* room=slots_[0].get();
    if(!cylinder_room_enabled_ || !room || room->name!="solid067.dat")return;
    if(!room->nodes_initialized || room->nodes.size()<374)throw std::runtime_error("Cylindrical room lacks its authored node pool");
    const auto visible=cylinder_room_visibility(eye,room_flags_);
    for(unsigned i=0;i<visible.size();++i){room->nodes[i].state=visible[i]?1:-1;room->nodes[i].visible=visible[i]!=0;}
}
}
