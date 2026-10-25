#include "../src/liquid.h"
#include <cassert>
#include <cstdio>
// Independent fine-grained area of the liquid under the surface, for checking.
static float referenceArea(const liquid::Shape &s) {
  double total = 0;
  for (int j = 0; j < 4800; ++j) {
    float X = -240 + (j + .5f) * .1f;
    for (int i = 0; i < 480; ++i) {
      float Y = -240 + i + .5f;
      float x = X * s.c + Y * s.s, y = -X * s.s + Y * s.c;
      if (y >= s.surface(x))
        total += .1;
    }
  }
  return total;
}
static float settleTime(float viscosity) {
  liquid::Slosh f;
  for (int i = 0; i < 30; ++i)
    f.step(i * .03f, 1.8f, 0, 1, viscosity, .5f, 1.f / 60);
  for (int i = 0; i < 6000; ++i) {
    if (f.settled(.9f))
      return i / 60.f;
    f.step(.9f, 0, 0, 1, viscosity, .5f, 1.f / 60);
  }
  return 1e9f;
}
// Peak surface displacement while shaking the upright board sideways at 3 Hz.
static float shakePeak(float viscosity, liquid::Motion &m) {
  m = liquid::Motion();
  liquid::Slosh f;
  float peak = 0, worst = 0;
  for (int i = 0; i < 300; ++i) {
    float ax = sinf(2 * liquid::kPi * 3 * i * .01f);
    m.update(ax, 1, 0, .01f, 0);
    f.step(m, viscosity, .5f, .01f);
    worst = std::max(worst, fabsf(m.turning));
    liquid::Shape s(f, .5f);
    peak = std::max(peak, std::max(fabsf(s.lowest), fabsf(s.highest)));
  }
  m.turning = worst;
  return peak;
}
int main() {
  // Synthetic, exactly representable axis errors; not measurements from a board.
  constexpr float kSyntheticBiasXG = .0625f, kSyntheticBiasYG = -.03125f;
  // Subtract sensor-axis bias before angle, magnitude and slosh calculations.
  for (int i = 0; i < 360; ++i) {
    float angle = i * liquid::kPi / 180;
    liquid::Motion biased, reference;
    biased.setAccelBias(kSyntheticBiasXG, kSyntheticBiasYG);
    for (int n = 0; n < 100; ++n) {
      biased.update(sinf(angle) + kSyntheticBiasXG, cosf(angle) + kSyntheticBiasYG, 0, .01f, 0);
      reference.update(sinf(angle), cosf(angle), 0, .01f, 0);
    }
    assert(fabsf(remainderf(biased.target - reference.target, 2 * liquid::kPi)) < .00001f);
    assert(fabsf(biased.gravity - reference.gravity) < .00001f);
  }
  // Construct sensor vectors from known tilts plus the synthetic axis errors.
  // Residual tilts must survive calibration; the algorithm must not snap them flat.
  constexpr float kResidualTiltDegrees[4] = {.1f, -.2f, .15f, -.1f};
  for (int q = 0; q < 4; ++q) {
    float expected = -q * liquid::kPi / 2 + kResidualTiltDegrees[q] * liquid::kPi / 180;
    float sensorX = sinf(expected) + kSyntheticBiasXG;
    float sensorY = cosf(expected) + kSyntheticBiasYG;
    liquid::Motion m;
    m.setAccelBias(kSyntheticBiasXG, kSyntheticBiasYG);
    liquid::Slosh f;
    for (int i = 0; i < 1800; ++i) {
      m.update(sensorX, sensorY, 0, .01f, 0);
      f.step(m, .45f, 475.f / 480, .01f);
    }
    assert(fabsf(remainderf(m.target - expected, 2 * liquid::kPi)) < .00001f);
    assert(f.settled(m.target));
  }
  liquid::Motion editBias;
  editBias.reset(6 * liquid::kPi + .1f);
  editBias.setAccelBias(kSyntheticBiasXG, kSyntheticBiasYG);
  editBias.update(kSyntheticBiasXG, 1 + kSyntheticBiasYG, 0, .01f, 0);
  assert(fabsf(editBias.target - 6 * liquid::kPi) < .00001f);
  assert(editBias.spin == 0 && editBias.accel == 0);
  editBias.setLevelOffset(2);
  editBias.update(kSyntheticBiasXG, 1 + kSyntheticBiasYG, 0, .01f, 0);
  assert(fabsf(remainderf(editBias.target, 2 * liquid::kPi) - 2 * liquid::kPi / 180) < .00001f);
  editBias.update(kSyntheticBiasXG, kSyntheticBiasYG, 1, .01f, 0);
  assert(editBias.holding);
  // Reducing the maximum fill changes height, never the resting surface angle.
  for (float quota : {.25f, .65f, 1.f}) for (int q = 0; q < 4; ++q) {
    float angle = q * liquid::kPi / 2;
    for (float cap : {1.f, 475.f / 480}) {
      liquid::Motion motion;
      liquid::Slosh fluid;
      motion.update(sinf(angle), cosf(angle), 0, .01f, 0);
      fluid.reset(motion.target);
      for (int i = 0; i < 1200; ++i) {
        motion.update(sinf(angle), cosf(angle), 0, .01f, 0);
        fluid.step(motion, .45f, quota * cap, .01f);
      }
      assert(fabsf(remainderf(fluid.angle - angle, 2 * liquid::kPi)) < .0001f);
      liquid::Shape shape(fluid, quota * cap);
      float mid = (shape.left + shape.right) / 2;
      float span = (shape.right - shape.left) * .3f;
      assert(fabsf(shape.surface(mid + span) - shape.surface(mid - span)) < .01f);
    }
  }
  // Distinct synthetic corrections exercise each direction independently,
  // at half-full and near-full quota alike.
  constexpr float kDirectionalCorrectionsDegrees[4] = {-2.5f, 1.25f, 4.f, -1.5f};
  liquid::Motion fourWay;
  fourWay.setLevelOffsets(kDirectionalCorrectionsDegrees[0], kDirectionalCorrectionsDegrees[1],
                          kDirectionalCorrectionsDegrees[2], kDirectionalCorrectionsDegrees[3]);
  for (int q = 0; q < 4; ++q) for (float quota : {.5f, 475.f / 480}) {
    float ideal = -q * liquid::kPi / 2;
    float bias = -kDirectionalCorrectionsDegrees[q] * liquid::kPi / 180;
    liquid::Motion m;
    m.setLevelOffsets(kDirectionalCorrectionsDegrees[0], kDirectionalCorrectionsDegrees[1],
                      kDirectionalCorrectionsDegrees[2], kDirectionalCorrectionsDegrees[3]);
    liquid::Slosh f;
    for (int i = 0; i < 2400; ++i) {
      m.update(sinf(ideal + bias), cosf(ideal + bias), 0, .01f, 0);
      f.step(m, .45f, quota, .01f);
    }
    assert(fabsf(remainderf(m.target - ideal, 2 * liquid::kPi)) < .0001f);
    assert(f.settled(m.target));
    liquid::Shape s(f, quota);
    assert(fabsf(s.area(s.level) / 230400 - quota) < .0001f);
  }
  // Smooth directional compensation has no discontinuity at quarter turns
  // or the wrap boundary, and applying settings at rest adds no angular speed.
  float prior = fourWay.calibration.correction(-4 * liquid::kPi);
  for (int i = 1; i <= 14400; ++i) {
    float a = -4 * liquid::kPi + i * liquid::kPi / 1800;
    float current = fourWay.calibration.correction(a);
    assert(fabsf(current - prior) < .0003f);
    prior = current;
  }
  fourWay.update(1, 0, 0, .01f, 0);
  fourWay.setLevelOffsets(1, 2, 3, 4);
  fourWay.update(1, 0, 0, .01f, 0);
  assert(fabsf(fourWay.target - (liquid::kPi / 2 + 4 * liquid::kPi / 180)) < .0001f);
  assert(fabsf(fourWay.spin) < .0001f);
  // Signed sensor bias cancels in the actual motion -> fluid pipeline.
  for (float biasDegrees : {-12.5f, 0.f, 8.3f}) {
    liquid::Motion calibrated;
    liquid::Slosh level;
    float bias = biasDegrees * liquid::kPi / 180;
    calibrated.setLevelOffset(-biasDegrees);
    for (int i = 0; i < 1200; ++i) {
      calibrated.update(sinf(bias), cosf(bias), 0, .01f, 0);
      level.step(calibrated, .45f, .5f, .01f);
    }
    assert(fabsf(calibrated.target) < .0001f);
    assert(level.settled(0) && fabsf(level.angle) < .002f);
    liquid::Shape levelShape(level, .5f);
    assert(fabsf(levelShape.rotate(100, levelShape.level).y -
                 levelShape.rotate(-100, levelShape.level).y) < .1f);
    assert(fabsf(levelShape.area(levelShape.level) / 230400 - .5f) < .0001f);
    // Saving a new offset shifts the reference, never the measured spin.
    calibrated.setLevelOffset(5 - biasDegrees);
    calibrated.update(sinf(bias), cosf(bias), 0, .01f, 0);
    assert(fabsf(calibrated.target - 5 * liquid::kPi / 180) < .0001f);
    assert(fabsf(calibrated.spin) < .0001f && fabsf(calibrated.accel) < .0001f);
    // Calibration also works while in-plane gravity is too small to sample.
    calibrated.update(0, 0, 1, .01f, 0);
    calibrated.setLevelOffset(-biasDegrees);
    calibrated.update(0, 0, 1, .01f, 0);
    assert(calibrated.holding && fabsf(calibrated.target) < .0001f);
    calibrated.setLevelOffset(0);
    calibrated.update(sinf(bias), cosf(bias), 0, .01f, 0);
    assert(fabsf(calibrated.target - bias) < .0001f);
  }
  liquid::Slosh f;
  for (int n = 0; n < liquid::kModes; ++n)
    f.amp[n] = (n % 3 - 1) * 9.f / (n + 1);
  for (float angle : {0.f, .5f, .7854f, 1.57f, 3.14f, 4.71f, -5.34f}) {
    f.angle = angle;
    for (float fill : {.02f, .1f, .35f, .65f, .9f, .99f}) {
      liquid::Shape s(f, fill);
      float target = fill * 230400;
      assert(fabsf(s.area(s.level) - target) < 1);
      assert(fabsf(referenceArea(s) / 230400 - fill) < .002f);
      // Raster bounds bracket every surface sample (row fast paths rely on it).
      int32_t heights[2801], top, bottom;
      s.raster(heights, 2801, top, bottom);
      for (int32_t h : heights)
        assert(h >= top && h <= bottom);
      assert(bottom - top < 200 * 65536);
      auto sec = liquid::flatSection(angle, fill);
      for (float x : {sec.left, sec.right}) {
        auto p = s.rotate(x, sec.level);
        assert(fabsf(p.x) < 240.1f && fabsf(p.y) < 240.1f);
        assert(fabsf(fmaxf(fabsf(p.x), fabsf(p.y)) - 240) < .1f);
      }
    }
  }
  // Sustained shaking stays finite and below wave-breaking steepness.
  f = liquid::Slosh();
  liquid::Motion m;
  for (int i = 0; i < 12000; ++i) {
    float a = 2.8f * sinf(i * .017f) + .3f * sinf(i * .9f);
    m.update(sinf(a), cosf(a) * (1 + .4f * sinf(i * .31f)), 0, 1.f / 60);
    f.step(m, .45f, .65f, 1.f / 60);
    assert(std::isfinite(f.angle) && std::isfinite(f.velocity));
    for (int n = 0; n < liquid::kModes; ++n)
      assert(std::isfinite(f.amp[n]) && fabsf(f.amp[n]) < 80);
  }
  liquid::Shape s(f, .65f);
  assert(fabsf(s.area(s.level) / 230400 - .65f) < .0001f);
  // Splashes move the surface but not the volume.
  liquid::Slosh drop;
  drop.splash(220);
  for (int i = 0; i < 6; ++i)
    drop.step(0, 0, 0, 1, .45f, .4f, 1.f / 60);
  liquid::Shape wavy(drop, .4f);
  assert(fabsf(wavy.displacement(0)) > 1);
  assert(fabsf(wavy.area(wavy.level) / 230400 - .4f) < .0001f);
  // Everything settles back to a flat surface once gravity stops moving.
  for (int i = 0; i < 3600; ++i)
    f.step(0, 0, 0, 1, .45f, .65f, 1.f / 60);
  assert(f.settled(0) && fabsf(f.angle) < .002f);
  // Thicker liquid comes to rest sooner than water-like liquid.
  float thin = settleTime(0), medium = settleTime(.45f), thick = settleTime(1);
  assert(thick < medium && medium < thin && thin < 60);
  // The gravity filter unwraps continuously and holds when lying flat.
  liquid::Motion turn;
  turn.setLevelOffset(15);
  for (int i = 0; i < 1200; ++i) {
    float a = i * .01f;
    turn.update(sinf(a), cosf(a), 0, 1.f / 100);
  }
  assert(fabsf(turn.target - (11.99f + 15 * liquid::kPi / 180)) < .1f && fabsf(turn.spin - 1) < .05f);
  float held = turn.target;
  for (int i = 0; i < 100; ++i)
    turn.update(.05f, -.05f, 1, 1.f / 100);
  assert(turn.holding && fabsf(turn.target - held) < .01f);
  // A real turn measured by the gyroscope keeps its rotational effects.
  liquid::Motion gyroTurn;
  for (int i = 0; i < 300; ++i) {
    float a = i * .01f;
    gyroTurn.update(sinf(a), cosf(a), 0, 1.f / 100, 1);
  }
  assert(fabsf(gyroTurn.turning - 1) < .05f);
  // Shaking sideways swings gravity without turning the board: waves stay
  // well below the screen size and thick liquid stays calmer than water.
  float water = shakePeak(0, turn), honey = shakePeak(1, turn);
  assert(fabsf(turn.turning) < .05f && fabsf(turn.accel) < 1);
  assert(water > 10 && water < 80 && honey < water * .6f);
  printf("Liquid: volume, chord, shaking, splash, settling (%.1fs/%.1fs/%.1fs), "
         "gravity filter and sideways shake (%.0fpx/%.0fpx) passed\n",
         thin, medium, thick, water, honey);
}
