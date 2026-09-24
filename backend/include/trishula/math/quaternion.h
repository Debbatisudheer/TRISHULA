#pragma once

#include "trishula/core/vector3.h"

#include <cmath>
#include <stdexcept>

namespace trishula {

class Quaternion {
public:
    double w{1.0};
    double x{0.0};
    double y{0.0};
    double z{0.0};

    constexpr Quaternion() = default;
    constexpr Quaternion(double w_value, double x_value, double y_value, double z_value)
        : w(w_value), x(x_value), y(y_value), z(z_value) {}

    [[nodiscard]] constexpr Quaternion operator+(const Quaternion& other) const noexcept {
        return {w + other.w, x + other.x, y + other.y, z + other.z};
    }

    [[nodiscard]] constexpr Quaternion operator*(double scalar) const noexcept {
        return {w * scalar, x * scalar, y * scalar, z * scalar};
    }

    constexpr Quaternion& operator+=(const Quaternion& other) noexcept {
        w += other.w;
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    [[nodiscard]] constexpr Quaternion operator*(const Quaternion& other) const noexcept {
        return {
            w * other.w - x * other.x - y * other.y - z * other.z,
            w * other.x + x * other.w + y * other.z - z * other.y,
            w * other.y - x * other.z + y * other.w + z * other.x,
            w * other.z + x * other.y - y * other.x + z * other.w
        };
    }

    [[nodiscard]] constexpr double norm_squared() const noexcept {
        return w * w + x * x + y * y + z * z;
    }

    [[nodiscard]] double norm() const noexcept {
        return std::sqrt(norm_squared());
    }

    [[nodiscard]] Quaternion normalized() const {
        const double n = norm();
        if (n == 0.0) {
            throw std::invalid_argument("Cannot normalize zero quaternion");
        }
        return *this * (1.0 / n);
    }

    [[nodiscard]] constexpr Quaternion conjugate() const noexcept {
        return {w, -x, -y, -z};
    }

    [[nodiscard]] Quaternion inverse() const {
        const double n2 = norm_squared();
        if (n2 == 0.0) {
            throw std::invalid_argument("Cannot invert zero quaternion");
        }
        return conjugate() * (1.0 / n2);
    }

    [[nodiscard]] Vector3 rotate(const Vector3& vector) const {
        const Quaternion unit = normalized();
        const Quaternion vector_quaternion{0.0, vector.x, vector.y, vector.z};
        const Quaternion rotated = unit * vector_quaternion * unit.conjugate();
        return {rotated.x, rotated.y, rotated.z};
    }
};

[[nodiscard]] constexpr Quaternion operator*(double scalar, const Quaternion& quaternion) noexcept {
    return quaternion * scalar;
}

} // namespace trishula
