// from net/minecraft/util/Mth.java — P0-a demo translation (floor + clamp only).
#include "mc/util/Mth.hpp"

#include <algorithm>
#include <cmath>

namespace mc::util {

int floor(double v) {
    // Java: return (int)Math.floor(v);
    return static_cast<int>(std::floor(v));
}

int clamp(int value, int min, int max) {
    // Java: return Math.min(Math.max(value, min), max);
    return std::min(std::max(value, min), max);
}

long clamp(long value, long min, long max) {
    // Java: return Math.min(Math.max(value, min), max);
    return std::min(std::max(value, min), max);
}

float clamp(float value, float min, float max) {
    // Java: return value < min ? min : Math.min(value, max);
    return value < min ? min : std::min(value, max);
}

double clamp(double value, double min, double max) {
    // Java: return value < min ? min : Math.min(value, max);
    return value < min ? min : std::min(value, max);
}

}  // namespace mc::util
