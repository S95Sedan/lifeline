#pragma once
#include <array>
#include <cmath>

namespace lifeline::math {
struct Vector4 { float x, y, z, w; };
// Column-major matrices acting on column vectors.
struct Matrix4 { std::array<Vector4, 4> columns; };
constexpr Vector4 operator+(Vector4 a, Vector4 b) { return {a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w}; }
constexpr Vector4 operator*(Vector4 a, float scale) { return {a.x*scale, a.y*scale, a.z*scale, a.w*scale}; }
constexpr float dot_xyz(Vector4 a, Vector4 b) { return (a.x*b.x + a.y*b.y) + a.z*b.z; }
constexpr Vector4 cross_xyz(Vector4 a, Vector4 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x, 0}; }
inline Vector4 normalize_xyz(Vector4 v) {
    const auto length = std::sqrt(dot_xyz(v, v));
    if (length == 0) return {0, 0, 0, 0};
    const auto inverse = 1.0f / length;
    return {v.x*inverse, v.y*inverse, v.z*inverse, 0};
}
constexpr Vector4 transform(const Matrix4& matrix, Vector4 v) {
    return ((matrix.columns[0]*v.x + matrix.columns[1]*v.y) + matrix.columns[2]*v.z) + matrix.columns[3]*v.w;
}
constexpr Matrix4 multiply(const Matrix4& left, const Matrix4& right) {
    return {{transform(left, right.columns[0]), transform(left, right.columns[1]),
             transform(left, right.columns[2]), transform(left, right.columns[3])}};
}
constexpr Matrix4 transpose(const Matrix4& m) {
    const auto& c = m.columns;
    return {{Vector4{c[0].x,c[1].x,c[2].x,c[3].x}, Vector4{c[0].y,c[1].y,c[2].y,c[3].y},
             Vector4{c[0].z,c[1].z,c[2].z,c[3].z}, Vector4{c[0].w,c[1].w,c[2].w,c[3].w}}};
}
constexpr Matrix4 identity() {
    return {{Vector4{1, 0, 0, 0}, Vector4{0, 1, 0, 0}, Vector4{0, 0, 1, 0}, Vector4{0, 0, 0, 1}}};
}
}
