#pragma once
#include "assets/flash.hpp"

namespace lifeline {
struct TitlePoint { float x,y; };
struct TitleNavigation {
    std::array<TitlePoint,4> foreground,up,down,confirm;
    explicit TitleNavigation(std::span<const unsigned char> script);
};
// Recovered entry_1004.cb menu behavior as an ordinary native state machine.
// This does not execute the game's bytecode or claim its requested scenes exist.
class TitleController {
public:
    enum class Phase { start, reveal, menu, moving, confirming, unavailable };
    enum class Request { none, new_game, load_game, options, bonus, attract };
private:
    FlashPlayer& foreground_;
    FlashPlayer cursor_;
    TitleNavigation navigation_;
    Phase phase_=Phase::start;
    Request request_=Request::none;
    unsigned selection_=0,settle_=0,idle_=0;
    TitlePoint cursor_target_{};
    void begin_menu();
public:
    TitleController(FlashPlayer&,const FlashMovie& cursor,const TitleNavigation&);
    void advance();
    void start();
    void move(int direction);
    void confirm();
    void back();
    Phase phase() const { return phase_; }
    Request request() const { return request_; }
    unsigned selection() const { return selection_; }
    const FlashPlayer& cursor() const { return cursor_; }
    bool cursor_visible() const { return phase_!=Phase::start && phase_!=Phase::reveal && (request_!=Request::attract || foreground_.frame()>=61); }
    static const char* label(unsigned selection);
};
}
