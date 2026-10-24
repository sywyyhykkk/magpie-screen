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
  for (int i = 0; i < 1200; ++i) {
    float a = i * .01f;
    turn.update(sinf(a), cosf(a), 0, 1.f / 100);
  }
  assert(fabsf(turn.target - 11.99f) < .1f && fabsf(turn.spin - 1) < .05f);
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
