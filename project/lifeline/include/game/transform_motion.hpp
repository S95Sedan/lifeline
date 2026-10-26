#pragma once
#include "core/math.hpp"
#include <array>
#include <cstdint>

namespace lifeline {
// Native translation and angular controllers recovered from 1d8a38/1d89f8.
// advance() is one authored simulation tick, independent of display refresh.
class TransformMotion {
    struct Turn {float target=0,limit=0,velocity=0;bool active=false;};
    std::array<Turn,3> turns_;
    math::Vector4 origin_{},target_{},velocity_{},acceleration_{};
    std::int32_t elapsed_=0,duration_=0;
    bool moving_=false;
    float turn_gain_=.1f;
    bool turn_acceleration_=false;
public:
    math::Vector4 position{0,0,0,1},rotation{0,0,0,0};
    static float angle(float);
    void move_to(math::Vector4 target,float speed);
    void trajectory(math::Vector4 velocity,math::Vector4 acceleration);
    void turn(unsigned axis,float target,float maximum_step);
    void turn_gain(float);
    float turn_gain()const{return turn_gain_;}
    void stop_translation(){moving_=false;}
    void stop_turn(unsigned axis);
    void clear_turn_velocity(){for(auto& turn:turns_)turn.velocity=0;}
    void turn_acceleration(bool enabled){turn_acceleration_=enabled;clear_turn_velocity();}
    bool turn_acceleration()const{return turn_acceleration_;}
    bool moving()const{return moving_;}
    bool velocity_near_zero()const;
    bool turning(unsigned axis)const{return turns_.at(axis).active;}
    math::Vector4 next_position()const;
    void advance();
    math::Matrix4 matrix()const;
    math::Matrix4 cylindrical_matrix()const;
};
}
