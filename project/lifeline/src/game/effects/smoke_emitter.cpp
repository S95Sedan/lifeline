#include "game/effects/effects.hpp"

namespace lifeline::script {
void Effects::emit_smoke(Instance& emitter){
    // 17c928..17cd54: nine random draws precede child allocation. The
    // type-4 constructor consumes its own rotation draw only on success.
    const auto& p=emitter.parameters;
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    std::array<float,32> child{};
    const float speed=p[14]*.5f+p[14]*.5f*unit();
    child[1]=p[0];child[2]=p[1];
    child[3]=p[2]+unit();child[4]=p[3];child[5]=p[4]+unit();
    child[6]=p[5];child[7]=p[6];
    for(unsigned i=0;i<3;++i)child[8+i]=p[7+i]+(unit()-.5f)*.2f;
    child[11]=child[12]=1;
    for(unsigned i=0;i<4;++i)child[13+i]=child[17+i]=p[10+i];
    for(unsigned i=0;i<3;++i)child[21+i]=p[10+i];
    child[25]=child[26]=speed;child[27]=p[15];
    child[28]=p[16]+unit();child[29]=p[16]+unit();
    child[31]=p[17]*.5f;child[30]=p[17]+unit()*child[31];
    create(4,child);
}
}
