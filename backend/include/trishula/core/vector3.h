#pragma once

#include <cmath>
#include <stdexcept>

namespace trishula {

class Vector3 {
public:
    double x{0.0};
    double y{0.0};
    double z{0.0};

    constexpr Vector3() = default;
    constexpr Vector3(double x_value, double y_value, double z_value)
        : x(x_value), y(y_value), z(z_value) {}

    [[nodiscard]] constexpr Vector3 operator+(const Vector3& other) const noexcept {
        return {x + other.x, y + other.y, z + other.z};
    }

    [[nodiscard]] constexpr Vector3 operator-(const Vector3& other) const noexcept {
        return {x - other.x, y - other.y, z - other.z};
    }

    [[nodiscard]] constexpr Vector3 operator-() const noexcept {
        return {-x, -y, -z};
    }

    [[nodiscard]] constexpr Vector3 operator*(double scalar) const noexcept {
        return {x * scalar, y * scalar, z * scalar};
    }

    [[nodiscard]] constexpr Vector3 operator/(double scalar) const {
        if (scalar == 0.0) {
            throw std::invalid_argument("Vector3 division by zero");
        }
        return {x / scalar, y / scalar, z / scalar};
    }

    constexpr Vector3& operator+=(const Vector3& other) noexcept {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    constexpr Vector3& operator-=(const Vector3& other) noexcept {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    constexpr Vector3& operator*=(double scalar) noexcept {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    constexpr Vector3& operator/=(double scalar) {
        if (scalar == 0.0) {
            throw std::invalid_argument("Vector3 division by zero");
        }
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    [[nodiscard]] constexpr double magnitude_squared() const noexcept {
        return x * x + y * y + z * z;
    }

    [[nodiscard]] double magnitude() const noexcept {
        return std::sqrt(magnitude_squared());
    }

    [[nodiscard]] constexpr double dot(const Vector3& other) const noexcept {
        return x * other.x + y * other.y + z * other.z;
    }

    [[nodiscard]] constexpr Vector3 cross(const Vector3& other) const noexcept {
        return {
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        };
    }

    [[nodiscard]] Vector3 normalized() const {
        const double magnitude_value = magnitude();
        if (magnitude_value == 0.0) {
            throw std::invalid_argument("Cannot normalize zero vector");
        }
        return *this / magnitude_value;
    }
};

[[nodiscard]] constexpr Vector3 operator*(double scalar, const Vector3& vector) noexcept {
    return vector * scalar;
}

} // namespace trishula
