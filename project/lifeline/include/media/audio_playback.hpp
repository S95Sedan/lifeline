#pragma once
namespace lifeline {
class AudioPlayback {
public:
    virtual ~AudioPlayback()=default;
    virtual void advance()=0;
    virtual void pause(bool)=0;
    virtual void fade(unsigned ticks)=0;
    virtual bool active()const=0;
    virtual bool paused()const=0;
};
}
