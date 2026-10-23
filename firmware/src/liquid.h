#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace liquid {
constexpr float kPi = 3.14159265358979323846f;
inline float clamp(float v, float a, float b) {
  return std::max(a, std::min(b, v));
}
struct Point {
  float x, y;
};
inline float area(const std::vector<Point> &p) {
  float a = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    auto q = p[(i + 1) % p.size()];
    a += p[i].x * q.y - q.x * p[i].y;
  }
  return fabsf(a) * .5f;
}
inline std::vector<Point> clip(std::vector<Point> p) {
  for (int edge = 0; edge < 4; ++edge) {
    std::vector<Point> out;
    out.reserve(72);
    auto distance = [edge](Point a) {
      return (edge < 2 ? a.x : a.y) * (edge % 2 ? -1 : 1) - 240;
    };
    for (size_t i = 0; i < p.size(); ++i) {
      auto a = p[i], b = p[(i + 1) % p.size()];
      float av = distance(a), bv = distance(b);
      if (av <= 0)
        out.push_back(a);
      if ((av <= 0) != (bv <= 0)) {
        float t = av / (av - bv);
        out.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
      }
    }
    p = std::move(out);
  }
  return p;
}
struct Slosh {
  float angle = 0, velocity = 0, wave = 0, waveVelocity = 0, ripple = 0,
        rippleVelocity = 0, lastTarget = 0, lastSpeed = 0;
  float heights[65] = {}, speeds[65] = {}, forces[65] = {}, wall[65] = {};
  Slosh() {
    for (int i = 0; i < 65; ++i) {
      float x = (i - 32) / 32.f;
      wall[i] =
          expf(-powf((x - .63f) / .13f, 2)) - expf(-powf((x + .63f) / .13f, 2));
    }
  }
  // target is the continuous gravity angle in display coordinates, in radians.
  void step(float target, float viscosity, float dt) {
    dt = clamp(dt, .001f, 1.f / 30);
    float speed = (target - lastTarget) / dt,
          impulse = clamp(speed - lastSpeed, -12, 12);
    lastTarget = target;
    lastSpeed = speed;
    waveVelocity += impulse * 28;
    rippleVelocity += impulse * 7;
    float damping = 2.5f + viscosity * 8, drive = clamp(speed, -4, 4);
    for (int i = 0; i < 65; ++i)
      speeds[i] += impulse * 30 * wall[i];
    for (int sub = 0; sub < 8; ++sub) {
      float h = dt / 8;
      for (int i = 0; i < 65; ++i) {
        int l = std::max(0, i - 1), r = std::min(64, i + 1);
        forces[i] = 2400 * (heights[l] + heights[r] - 2 * heights[i]) -
                    9 * heights[i] - (1.2f + viscosity * 3.5f) * speeds[i] +
                    viscosity * 45 * (speeds[l] + speeds[r] - 2 * speeds[i]) +
                    drive * 700 * wall[i];
      }
      for (int i = 0; i < 65; ++i) {
        speeds[i] += forces[i] * h;
        heights[i] += speeds[i] * h;
      }
    }
    for (int i = 0; i < 4; ++i) {
      float h = dt / 4;
      velocity += ((target - angle) * 45 - velocity * damping) * h;
      angle += velocity * h;
      waveVelocity += (-48 * wave - damping * waveVelocity) * h;
      wave += waveVelocity * h;
      rippleVelocity += (-110 * ripple - (damping + 2) * rippleVelocity) * h;
      ripple += rippleVelocity * h;
    }
  }
};
struct Shape {
  float c, s, level, offset[65];
  Point rotate(float x, float y) const {
    return {x * c - y * s, x * s + y * c};
  }
  std::vector<Point> polygon(float l) const {
    std::vector<Point> p;
    p.reserve(67);
    for (int i = 0; i < 65; ++i)
      p.push_back(rotate(-350 + i * 700.f / 64, l + offset[i]));
    p.push_back(rotate(350, 700));
    p.push_back(rotate(-350, 700));
    return clip(std::move(p));
  }
  Shape(const Slosh &f, float fraction) : c(cosf(f.angle)), s(sinf(f.angle)) {
    for (int i = 0; i < 65; ++i) {
      float x = -350 + i * 700.f / 64;
      offset[i] =
          f.wave * sinf(x / 115) + f.ripple * cosf(x / 57) + f.heights[i];
    }
    float lo = -460, hi = 460, target = clamp(fraction, 0, 1) * 480 * 480;
    for (int i = 0; i < 17; ++i) {
      float mid = (lo + hi) / 2;
      if (area(polygon(mid)) > target)
        lo = mid;
      else
        hi = mid;
    }
    level = (lo + hi) / 2;
  }
  float surface(float x) const {
    float u = clamp((x + 350) * 64 / 700, 0, 64);
    int i = std::min(63, (int)u);
    return level + offset[i] + (offset[i + 1] - offset[i]) * (u - i);
  }
};
} // namespace liquid
