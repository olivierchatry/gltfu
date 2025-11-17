#ifndef GLTFU_MATH_UTILS_H
#define GLTFU_MATH_UTILS_H

#include <array>
#include <cstddef>

namespace gltfu {

using Matrix4 = std::array<double, 16>;

constexpr Matrix4 kIdentityMatrix = {
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
};

inline Matrix4 multiply(const Matrix4& lhs, const Matrix4& rhs) {
    Matrix4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            double value = 0.0;
            for (std::size_t k = 0; k < 4; ++k) {
                value += lhs[row + k * 4] * rhs[k + col * 4];
            }
            result[row + col * 4] = value;
        }
    }
    return result;
}

} // namespace gltfu

#endif // GLTFU_MATH_UTILS_H
