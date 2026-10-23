# Third-party notices

The root LICENSE applies to project-original material supplied under that license. It does not replace or restrict the licenses of the third-party materials listed below. Previously MIT-licensed Magpie Screen material retains its original permissions; the prior notice is preserved in LICENSES/magpie-screen-prior-MIT.txt.

- Magpie logo (`src/assets/magpie.svg`), flying magpie SVG (`src/FlyingMagpie.vue`), wing motion and palette reference: yetone/magpie, MIT, Copyright (c) 2026 yetone. Source: build/icon/magpie-small.svg; internal/gui/assets/plugins/routing/client.js FLIER; routing.css; app.css. License: LICENSES/magpie-MIT.txt.
- ChatGPT / Claude / Cursor icons: copied only the selected three SVGs from yetone/magpie site/public/icons/{openai.svg,claude-color.svg,cursor.svg}, under that repository's MIT license. Names and marks belong to their respective owners and identify compatibility; no endorsement is implied.
- peterroe/magpie-usage-display: hardware and architecture reference; no firmware/fonts copied. Original MIT notice Copyright (c) 2026 Jay Yao is retained in LICENSES/magpie-usage-display-MIT.txt.
- Firmware hardware initialization is based on Waveshare's ESP32-S3-Touch-AMOLED-2.16 Arduino examples. Arduino_GFX, SensorLib, XPowersLib and ArduinoJson remain version-pinned PlatformIO dependencies; their notices are retained in their packages.

- Firmware UI icons in `ui_assets.h` are rasterized from the same MIT-licensed SVG assets in `src/assets/`. Grayscale text glyphs are generated with the macOS STHeiti Medium system font, following the existing Chinese-label build workflow; the font file itself is not bundled.
