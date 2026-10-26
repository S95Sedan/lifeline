#include "ui/bonus.hpp"
#include <bit>
#include <sstream>

namespace lifeline {
BonusAssets::BonusAssets(std::span<const unsigned char> bg,std::span<const unsigned char> row,std::span<const unsigned char> script)
    :background(bg,"bg00.cf"),selector(row,"sel00.cf") {
    if(script.size()!=504948 || std::string(reinterpret_cast<const char*>(script.data()+0x4e24),7)!="bg00.cf") throw std::runtime_error("Unsupported Bonus script");
    const auto string_at=[&](unsigned offset) {const auto begin=script.begin()+offset,end=std::find(begin,script.end(),0);
        if(end==script.end()) throw std::runtime_error("Unterminated Bonus text");return std::string(begin,end);};
    constexpr std::array<unsigned,5> offsets{0x4ec8,0x4ef4,0x4f20,0x4f58,0x4f84};
    for(unsigned i=0;i<5;++i) labels[i]=string_at(offsets[i]);
    chips_label=string_at(0x4e8c);points_label=string_at(0x4fa8);record_label=string_at(0x501c);
    const auto point=[&](unsigned offset) {
        const float x=std::bit_cast<float>(le32(script.data()+offset)),y=std::bit_cast<float>(le32(script.data()+offset+4));
        if(!std::isfinite(x) || !std::isfinite(y) || std::abs(x)>640 || std::abs(y)>448) throw std::runtime_error("Invalid Bonus coordinate");
        return math::Vector4{x,y,0,1};
    };
    for(unsigned i=0;i<6;++i) {positions[i]=point(5217*4+i*8);positions[i].x+=320;positions[i].y+=224;}
    for(unsigned i=0;i<4;++i) partial[i]=point(5229*4+i*8);
}
BonusController::BonusController(const BonusAssets& assets,std::optional<BonusProgress> progress)
    :assets_(&assets),progress_(progress),background_(assets.background) {
    if(progress_) unlocked_=1+std::min(progress_->chips,16u)/4;
    const auto number=[](unsigned value,unsigned width) {std::ostringstream text;text<<std::setw(width)<<value;return text.str();};
    background_.set_text("text_chips",progress_?assets.chips_label+std::to_string(std::min(progress_->chips,16u)):"",24,24,{.7f,1,.7f,1});
    rows_.reserve(5);
    for(unsigned i=0;i<5;++i) {
        rows_.emplace_back(assets.selector);rows_.back().set_text("text_mg0",assets.labels[i]);
        if(progress_ && i) {
            const auto score=i<4?number(progress_->scores[i-1],6)+assets.points_label:
                number(std::min(progress_->wins,999u),7)+"    "+number(std::min(progress_->losses,999u),7)+assets.record_label;
            rows_.back().set_text("text_hs",score,55,35,{0,1,.7f,1});
        }
    }
}
void BonusController::configure() {
    rows_[0].click(-2,54);
    const auto chips=progress_?std::min(progress_->chips,16u):0;
    for(unsigned i=1;i<5;++i) {
        if(chips>=i*4) rows_[i].click(-2,54);
        else if((i==1 && chips<4) || (chips>(i-1)*4 && chips<i*4)) {
            const auto point=assets_->partial[chips%4];rows_[i].click(point.x,point.y);
        }
    }
}
void BonusController::advance() {
    if(request_==Request::closed) return;
    background_.advance();for(auto& row:rows_) row.advance();
    if(ticks_<90) ++ticks_;
    if(ticks_==60) configure();
    if(ticks_>=60) for(unsigned i=0;i<rows_.size();++i) if(i<unlocked_) rows_[i].pointer(50,i==selected_?24.f:-2.f,false);
}
void BonusController::move(int direction) {
    if(!ready()) return;
    selected_=unsigned(std::clamp(int(selected_)+direction,0,int(unlocked_)-1));
    for(unsigned i=0;i<unlocked_;++i) rows_[i].pointer(50,i==selected_?24.f:-2.f,false);
}
void BonusController::confirm() {
    if(!ready()) return;
    request_=progress_?static_cast<Request>(selected_+1):Request::missing_save;
}
void BonusController::back() {request_=request_==Request::none?Request::closed:Request::none;}
}
