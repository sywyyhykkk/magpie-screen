#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
// Liquid inside the 480x480 screen, modelled in the gravity frame: x runs along
// the free surface, y points along gravity. The surface is a bulk tilt mode
// (angle) plus standing waves fitted to the real wall-to-wall chord, with
// water-wave dispersion, viscous damping and exact area (volume) conservation.
namespace liquid {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalf = 240;     // container half size, px
constexpr float kSpan = 350;     // surface x range; covers any chord (240*sqrt2)
constexpr float kGravity = 8000; // virtual gravity, px/s^2 (sets slosh tempo)
constexpr float kTension = 1.2e5f; // capillary term, px^3/s^2 (crisper ripples)
constexpr float kStep = 1.f / 240; // physics substep, s
constexpr int kModes = 32, kGrid = 257;
inline float clamp(float v, float a, float b) {
  return std::max(a, std::min(b, v));
}
struct Point {
  float x, y;
};
// Flat cut of the square perpendicular to gravity holding `fill` of its area.
struct Section {
  float level = 0, left = 0, right = 0;
  float width() const { return right - left; }
};
inline Section flatSection(float angle, float fill) {
  float c = cosf(angle), s = sinf(angle);
  Point v[4];
  const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  float top = 1e9f, bottom = -1e9f;
  for (int i = 0; i < 4; ++i) {
    float X = corners[i][0] * kHalf, Y = corners[i][1] * kHalf;
    v[i] = {X * c + Y * s, -X * s + Y * c};
    top = std::min(top, v[i].y);
    bottom = std::max(bottom, v[i].y);
  }
  // Area of the square below y=level (liquid side), by half-plane clipping.
  auto below = [&](float level) {
    Point p[5];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
      Point a = v[i], b = v[(i + 1) & 3];
      if (a.y >= level)
        p[n++] = a;
      if ((a.y >= level) != (b.y >= level)) {
        float t = (level - a.y) / (b.y - a.y);
        p[n++] = {a.x + t * (b.x - a.x), level};
      }
    }
    float area = 0;
    for (int i = 0; i < n; ++i) {
      Point q = p[(i + 1) % n];
      area += p[i].x * q.y - q.x * p[i].y;
    }
    return fabsf(area) * .5f;
  };
  float target = clamp(fill, 0, 1) * 4 * kHalf * kHalf, lo = top, hi = bottom;
  for (int i = 0; i < 24; ++i) {
    float mid = (lo + hi) * .5f;
    if (below(mid) > target)
      lo = mid;
    else
      hi = mid;
  }
  Section out;
  out.level = (lo + hi) * .5f;
  out.left = 1e9f;
  out.right = -1e9f;
  for (int i = 0; i < 4; ++i) {
    Point a = v[i], b = v[(i + 1) & 3];
    if ((a.y - out.level) * (b.y - out.level) <= 0 && a.y != b.y) {
      float x = a.x + (out.level - a.y) / (b.y - a.y) * (b.x - a.x);
      out.left = std::min(out.left, x);
      out.right = std::max(out.right, x);
    }
  }
  if (out.left > out.right)
    out.left = out.right = 0;
  return out;
}
// Smoothed effective gravity from the accelerometer (in g). Specific force is
// what the liquid feels, so lateral shakes deliberately pass through. Shakes
// swing gravity without turning the board, so rotational effects use `turning`:
// the gravity swing rate limited by the gyroscope's measured rotation rate.
struct LevelCalibration {
  // Looking at the display: buttons up, right, down, left. Clockwise board
  // rotation moves gravity counterclockwise in screen coordinates.
  float degrees[4] = {};
  float correction(float sensorAngle) const {
    float quarter = -sensorAngle / (kPi / 2);
    quarter -= floorf(quarter / 4) * 4;
    int i = std::min(3, (int)quarter);
    // Use the exact setting within 15 degrees of each resting orientation;
    // smoothly blend between those regions, including across the 360 seam.
    float t = clamp((quarter - i - 1.f / 6) * 1.5f, 0, 1);
    t = t * t * (3 - 2 * t);
    return (degrees[i] + (degrees[(i + 1) % 4] - degrees[i]) * t) * kPi / 180;
  }
};
struct Motion {
  float target = 0, spin = 0, turning = 0, accel = 0, gravity = 1, trend = 0,
        gyro = 0;
  bool holding = false, primed = false;
  LevelCalibration calibration;
  float sensorAngle = 0;
  float biasX = 0, biasY = 0;
  void setAccelBias(float x, float y) {
    if (biasX == x && biasY == y) return;
    biasX = x;
    biasY = y;
    // Re-prime the gravity filter from the next corrected sample. Editing a
    // calibration must not be interpreted as angular speed/acceleration.
    primed = false;
    spin = turning = accel = trend = gyro = 0;
  }
  void setLevelOffset(float degrees) {
    setLevelOffsets(degrees, degrees, degrees, degrees);
  }
  void setLevelOffsets(float up, float right, float down, float left) {
    float previous = calibration.correction(sensorAngle);
    calibration.degrees[0] = up;
    calibration.degrees[1] = right;
    calibration.degrees[2] = down;
    calibration.degrees[3] = left;
    // Shift the reference even while lying flat, without inventing a turn.
    target += calibration.correction(sensorAngle) - previous;
  }
  static float alpha(float dt, float hz) {
    return 1 / (1 + 1 / (2 * kPi * hz * dt));
  }
  void reset(float angle) {
    target = angle;
    spin = turning = accel = trend = gyro = 0;
    primed = true;
  }
  // `rate` is the rotation rate about the screen normal (rad/s), or NAN when no
  // gyroscope is available, in which case every gravity swing counts as a turn.
  void update(float x, float y, float z, float dt, float rate = NAN) {
    (void)z;
    x -= biasX;
    y -= biasY;
    dt = clamp(dt, .001f, .1f);
    float planar = sqrtf(x * x + y * y);
    // Lying flat leaves no in-plane gravity; hold the last direction.
    holding = holding ? planar < .22f : planar < .17f;
    gravity += (clamp(planar, .25f, 2.5f) - gravity) * alpha(dt, 8);
    float last = target, lastTurning = turning;
    if (!holding) {
      sensorAngle = atan2f(x, y);
      float corrected = sensorAngle + calibration.correction(sensorAngle);
      if (!primed) {
        reset(target + remainderf(corrected - target, 2 * kPi));
        return;
      }
      // One-euro filter: steady when still, low lag when turning fast.
      float raw = target + remainderf(corrected - target, 2 * kPi);
      trend += ((raw - target) / dt - trend) * alpha(dt, 4);
      // Sub-pixel sensor noise at rest must not keep the liquid redrawing.
      if (fabsf(spin) > .02f || fabsf(raw - target) > .004f)
        target += (raw - target) * alpha(dt, 1.2f + 1.8f * fabsf(trend));
    }
    spin += ((target - last) / dt - spin) * alpha(dt, 10);
    if (std::isnan(rate))
      turning = spin;
    else {
      gyro += (fabsf(rate) - gyro) * alpha(dt, 10);
      turning = clamp(spin, -gyro, gyro);
    }
    accel += (clamp((turning - lastTurning) / dt, -400, 400) - accel) *
             alpha(dt, 8);
  }
};
struct Slosh {
  // Bulk orientation of the surface (display radians) and its rate.
  float angle = 0, velocity = 0;
  // Standing-wave amplitudes (px) and rates on the current chord: cos(n pi u).
  float amp[kModes] = {}, rate[kModes] = {};
  float lastGravity = 1;
  void reset(float target) {
    *this = Slosh();
    angle = target;
  }
  // Velocity impulse (px/s) of a Gaussian bump at chord position `at` (0..1).
  void kick(float strength, float at, float width) {
    float base = 2 * width * sqrtf(2 * kPi) * strength;
    for (int n = 1; n <= kModes; ++n) {
      float k = n * kPi * width;
      rate[n - 1] += base * cosf(n * kPi * at) * expf(-.5f * k * k);
    }
  }
  // A drop landing mid-surface, e.g. when quota arrives.
  void splash(float strength) { kick(strength, .5f, .07f); }
  void step(float target, float spin, float accel, float gravity,
            float viscosity, float fill, float dt) {
    dt = clamp(dt, 0, .1f);
    viscosity = clamp(viscosity, 0, 1);
    Section sec = flatSection(angle, fill);
    float width = std::max(sec.width(), 24.f),
          depth = std::max(clamp(fill, 0, 1) * 4 * kHalf * kHalf / width, 4.f),
          g = kGravity * gravity, mid = (sec.left + sec.right) * .5f;
    // Bulk slosh tempo is the fundamental mode of this chord and depth.
    float k1 = kPi / width,
          bulk = std::max(12.f, g * k1 * tanhf(k1 * depth) * 1.2f),
          zeta = .1f + .85f * viscosity,
          friction = .35f + 1.6f * viscosity,
          nu = 6 + 750 * viscosity * viscosity;
    // Thin liquid keeps its world orientation while the container turns (so
    // it follows gravity at once); thick liquid is dragged along and lags.
    float inertia = .95f * (1 - viscosity);
    // Walls at the surface height push the liquid towards the low side while
    // turning (overshoot); centrifugal load dishes it towards the walls.
    float turn = clamp(accel, -400, 400) * (1 - .6f * viscosity), lever = kHalf * .5f - sec.level,
          spin2 = std::min(spin * spin, 400.f),
          tilt = (turn * lever - spin2 * mid) / g * width,
          dish = -spin2 / (2 * g) * width * width;
    // When gravity swings faster than the bulk follows (shaking), the shorter
    // waves already lean towards the new level; thick liquid resists.
    float lean = tanf(clamp(target - angle, -.6f, .6f)) * width *
                 (1 - .5f * viscosity);
    float jolt = dt > 0 ? clamp((gravity - lastGravity) / dt, -40, 40) : 0;
    lastGravity = gravity;
    float omega2[kModes], damp[kModes], equilibrium[kModes], limit[kModes];
    int active = std::min(kModes, std::max(1, (int)(width / 7)));
    for (int n = 1; n <= kModes; ++n) {
      float k = n * kPi / width, w2 = (g * k + kTension * k * k * k) *
                                      tanhf(k * depth);
      float bound = 1.4f / kStep;
      omega2[n - 1] = std::min(w2, bound * bound);
      damp[n - 1] = friction + 2 * nu * k * k +
                    .05f * sqrtf(nu * sqrtf(omega2[n - 1]));
      float shape = 4 / (n * n * kPi * kPi);
      equilibrium[n - 1] =
          n & 1 ? -(tilt + (n > 1 ? lean : 0)) * shape : dish * shape;
      limit[n - 1] = .32f / k; // steepness above this breaks
      if (n > active)
        damp[n - 1] += 60;
    }
    // Sudden pressure changes (bumps) launch rings from both walls; turning
    // jerks push one wall up and pull the other down.
    float wallWidth = .05f;
    for (int n = 1; n <= kModes; ++n) {
      float k = n * kPi * wallWidth,
            profile = 2 * wallWidth * sqrtf(2 * kPi) * expf(-.5f * k * k);
      float wall = n & 1 ? -turn * lever * .02f : -jolt * 18;
      rate[n - 1] += profile * wall * dt;
    }
    int steps = std::max(1, (int)ceilf(dt / kStep - 1e-4f));
    float h = dt / steps, decay[kModes];
    for (int n = 0; n < kModes; ++n)
      decay[n] = expf(-damp[n] * h);
    for (int i = 0; i < steps; ++i) {
      float lag = target - angle;
      velocity += (bulk * lag - 2 * zeta * sqrtf(bulk) * velocity +
                   inertia * clamp(accel, -400, 400)) * h;
      angle += velocity * h;
      for (int n = 0; n < kModes; ++n) {
        rate[n] += -omega2[n] * (amp[n] - equilibrium[n]) * h;
        rate[n] *= decay[n];
        amp[n] += rate[n] * h;
        if (fabsf(amp[n]) > limit[n]) {
          amp[n] = copysignf(limit[n], amp[n]);
          rate[n] *= .5f;
        }
      }
    }
  }
  void step(const Motion &m, float viscosity, float fill, float dt) {
    step(m.target, m.turning, m.accel, m.gravity, viscosity, fill, dt);
  }
  bool settled(float target) const {
    if (fabsf(target - angle) > .002f || fabsf(velocity) > .006f)
      return false;
    for (int n = 0; n < kModes; ++n)
      if (fabsf(amp[n]) > .08f || fabsf(rate[n]) > .8f)
        return false;
    return true;
  }
};
// Surface geometry for one frame: level solved so the liquid area under the
// displaced surface equals the fill exactly.
struct Shape {
  float c = 1, s = 0, level = 0, left = 0, right = 0, scale = 0, lowest = 0,
        highest = 0;
  float offset[kGrid] = {};
  Shape() = default;
  Shape(const Slosh &f, float fill) { update(f, fill); }
  float displacement(float x) const {
    float u = clamp((x - left) * scale, 0, kGrid - 1);
    int i = std::min(kGrid - 2, (int)u);
    return offset[i] + (offset[i + 1] - offset[i]) * (u - i);
  }
  float surface(float x) const { return level + displacement(x); }
  Point rotate(float x, float y) const {
    return {x * c - y * s, x * s + y * c};
  }
  // Liquid area (px^2) for a given level, integrated over 1px columns; `cover`
  // receives d(area)/d(level) magnitude, i.e. the wetted surface width.
  float area(float at, float *cover = nullptr) const {
    float total = 0, wet = 0;
    float inv_s = fabsf(s) > 1e-6f ? 1 / s : 0,
          inv_c = fabsf(c) > 1e-6f ? 1 / c : 0;
    for (int j = 0; j < 2 * (int)kSpan; ++j) {
      float x = -kSpan + j + .5f, top = -1e9f, bottom = 1e9f;
      // |x*c - y*s| <= half and |x*s + y*c| <= half bound y in this column.
      if (inv_s != 0) {
        float a = (x * c - kHalf) * inv_s, b = (x * c + kHalf) * inv_s;
        top = std::max(top, std::min(a, b));
        bottom = std::min(bottom, std::max(a, b));
      } else if (fabsf(x * c) > kHalf)
        continue;
      if (inv_c != 0) {
        float a = (-kHalf - x * s) * inv_c, b = (kHalf - x * s) * inv_c;
        top = std::max(top, std::min(a, b));
        bottom = std::min(bottom, std::max(a, b));
      } else if (fabsf(x * s) > kHalf)
        continue;
      if (bottom <= top)
        continue;
      float y = at + displacement(x);
      if (y < bottom) {
        total += bottom - std::max(top, y);
        if (y > top)
          wet += 1;
      }
    }
    if (cover)
      *cover = wet;
    return total;
  }
  void update(const Slosh &f, float fill) {
    c = cosf(f.angle);
    s = sinf(f.angle);
    fill = clamp(fill, 0, 1);
    Section sec = flatSection(f.angle, fill);
    left = sec.left;
    right = sec.right;
    float width = sec.width();
    scale = (kGrid - 1) / std::max(width, 1e-3f);
    // Chebyshev recurrence evaluates cos(n*theta) for every mode cheaply.
    static float cosines[kGrid];
    static bool ready = false;
    if (!ready) {
      for (int i = 0; i < kGrid; ++i)
        cosines[i] = cosf(kPi * i / (kGrid - 1));
      ready = true;
    }
    int active = std::min(kModes, std::max(1, (int)(width / 7)));
    // Small meniscus where the surface meets the walls.
    float meniscus = 3.5f * clamp(width / 120, 0, 1), reach = 9;
    lowest = -1e9f;
    highest = 1e9f;
    for (int i = 0; i < kGrid; ++i) {
      float t = cosines[i], prev = 1, cur = t, sum = 0;
      for (int n = 0; n < active; ++n) {
        sum += f.amp[n] * cur;
        float next = 2 * t * cur - prev;
        prev = cur;
        cur = next;
      }
      float x = width * i / (kGrid - 1), wall = std::min(x, width - x);
      if (wall < 6 * reach)
        sum -= meniscus * expf(-wall / reach);
      offset[i] = sum;
      lowest = std::max(lowest, sum);
      highest = std::min(highest, sum);
    }
    float target = fill * 4 * kHalf * kHalf;
    level = sec.level;
    if (fill <= 0 || fill >= 1)
      return;
    // Bracketed Newton: d(area)/d(level) is minus the wetted width.
    float lo = sec.level - 2 * kHalf, hi = sec.level + 2 * kHalf;
    for (int i = 0; i < 16; ++i) {
      float cover, error = area(level, &cover) - target;
      if (fabsf(error) < .25f)
        break;
      if (error > 0)
        lo = level;
      else
        hi = level;
      float next = cover > .5f ? level + error / cover : NAN;
      level = next > lo && next < hi ? next : (lo + hi) * .5f;
    }
  }
  // Surface heights in 16.16 fixed point at x = -kSpan + i/4, i in [0, n).
  void raster(int32_t *out, int n, int32_t &top, int32_t &bottom) const {
    for (int i = 0; i < n; ++i) {
      float u = clamp((i * .25f - kSpan - left) * scale, 0, kGrid - 1);
      int k = std::min(kGrid - 2, (int)u);
      float y = level + offset[k] + (offset[k + 1] - offset[k]) * (u - k);
      out[i] = (int32_t)(y * 65536);
    }
    top = (int32_t)((level + highest) * 65536) - 65536;
    bottom = (int32_t)((level + lowest) * 65536) + 65536;
  }
};
} // namespace liquid
