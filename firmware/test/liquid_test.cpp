#include "../src/liquid.h"
#include <cassert>
#include <cstdio>
int main() {
  liquid::Slosh f;
  for (float angle : {0.f, .5f, 1.57f, 3.14f, 4.71f, 6.28f}) {
    f.angle = angle;
    for (float fill : {.1f, .35f, .65f, .9f}) {
      liquid::Shape s(f, fill);
      assert(fabsf(liquid::area(s.polygon(s.level)) / 230400 - fill) < .001f);
    }
  }
  for (int i = 0; i < 12000; ++i) {
    float target = 2.8f * sinf(i * .017f);
    f.step(target, .45f, 1.f / 60);
    assert(std::isfinite(f.angle));
    for (float h : f.heights)
      assert(std::isfinite(h) && fabsf(h) < 400);
  }
  liquid::Shape s(f, .65f);
  assert(fabsf(liquid::area(s.polygon(s.level)) / 230400 - .65f) < .001f);
  for (int i = 0; i < 3600; ++i)
    f.step(0, .45f, 1.f / 60);
  assert(fabsf(f.angle) < .001f);
  for (float h : f.heights)
    assert(fabsf(h) < .01f);
  puts("Liquid: tilt/inversion volume, sustained movement and settling passed");
}
