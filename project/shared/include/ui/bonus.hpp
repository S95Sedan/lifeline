#pragma once
#include "assets/flash.hpp"
#include <array>
#include <optional>

namespace lifeline {
struct BonusProgress {
    unsigned chips=0;
    std::array<unsigned,3> scores{};
    unsigned wins=0,losses=0;
};
struct BonusAssets {
    FlashMovie background,selector;
    std::array<math::Vector4,6> positions;
    std::array<math::Vector4,4> partial;
    std::array<std::string,5> labels;
    std::string chips_label,points_label,record_label;
    BonusAssets(std::span<const unsigned char>,std::span<const unsigned char>,std::span<const unsigned char>);
};
class BonusController {
public:
    enum class Request {none,tutorial,word_jumble,phrases,calculation,category,missing_save,closed};
private:
    const BonusAssets* assets_;
    std::optional<BonusProgress> progress_;
    FlashPlayer background_;
    std::vector<FlashPlayer> rows_;
    unsigned ticks_=0,selected_=0,unlocked_=1;
    Request request_=Request::none;
    void configure();
public:
    BonusController(const BonusAssets&,std::optional<BonusProgress>);
    void advance();
    void move(int direction);
    void confirm();
    void back();
    unsigned selection() const {return selected_;}
    unsigned unlocked() const {return unlocked_;}
    bool ready() const {return ticks_>=90 && request_==Request::none;}
    Request request() const {return request_;}
    const FlashPlayer& background() const {return background_;}
    const std::vector<FlashPlayer>& rows() const {return rows_;}
};
}
