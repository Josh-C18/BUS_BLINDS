#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <math.h>

// --- Matrix Panel Settings (two chained 80x40 panels -> 160x40) ---
#define PANEL_RES_X 160      // Total width: 2 x 80px panels
#define PANEL_RES_Y 40       // Total height
#define PANEL_CHAIN 2        // 2 panels connected

// --- Board Pin Mapping ---
#define R1_PIN 5
#define G1_PIN 4
#define B1_PIN 6

#define R2_PIN 15
#define G2_PIN 7
#define B2_PIN 17

#define A_PIN  8
#define B_PIN  18
#define C_PIN  10
#define D_PIN  9
#define E_PIN  16   // Line E active for 80x40 scanning

#define LAT_PIN 11
#define OE_PIN  13
#define CLK_PIN 12

MatrixPanel_I2S_DMA *dma_display = nullptr;

// --- Fast Trigonometric Lookup Table (LUT) ---
uint8_t sinLUT[256];

void initTrigLUT() {
  for (int i = 0; i < 256; i++) {
    float rad = (i / 256.0f) * 6.28318530718f;
    sinLUT[i] = (uint8_t)(127.5f + 127.5f * sinf(rad));
  }
}

inline uint8_t fastSin(uint8_t idx) { return sinLUT[idx]; }
inline uint8_t fastCos(uint8_t idx) { return sinLUT[(uint8_t)(idx + 64)]; }

// Fast HSV to RGB conversion
void hsvToRgbFast(uint8_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (s == 0) {
    r = g = b = v;
    return;
  }
  uint8_t region = h / 43;
  uint8_t remainder = (h - (region * 43)) * 6;

  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

// --- Animation State ---
uint8_t animStep = 0;
uint8_t currentEffect = 0;
unsigned long lastEffectSwitch = 0;
const unsigned long EFFECT_DURATION = 8000; // 8 seconds per effect
const uint8_t NUM_EFFECTS = 10;

// =============================================================================
// EFFECT 0: Pure Lava & Magma Liquid (100% High Intensity Red/Yellow/White)
// =============================================================================
void renderFullMagmaLava(uint8_t t) {
  uint8_t r, g, b;
  for (int y = 0; y < PANEL_RES_Y; y++) {
    uint8_t w2 = fastSin((y << 3) + (t << 2));
    for (int x = 0; x < PANEL_RES_X; x++) {
      uint8_t w1 = fastSin((x << 2) + (t << 1));
      uint8_t w3 = fastSin(((x + y) << 2) - t);
      uint8_t heat = (w1 + w2 + w3) / 3;

      // Color mapping guarantees no pixel drops below bright red
      r = 255;
      g = heat;
      b = (heat > 180) ? (heat - 180) * 3 : 0;

      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 1: Hyper-Dense Rainbow Plasma Wave (100% Saturated Full-Field)
// =============================================================================
void renderFullSpectrumPlasma(uint8_t t) {
  uint8_t r, g, b;
  for (int y = 0; y < PANEL_RES_Y; y++) {
    uint8_t w2 = fastSin((y << 3) + (t << 1));
    for (int x = 0; x < PANEL_RES_X; x++) {
      uint8_t w1 = fastSin((x << 2) + t);
      uint8_t w3 = fastCos(((x - y) << 2) + (t << 1));

      uint8_t hue = (w1 + w2 + w3) / 3 + (t << 2);
      hsvToRgbFast(hue, 255, 255, r, g, b); // Max Brightness (255)
      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 2: 3D Boiling Liquid Heightmap (Full Panel Dynamic Wave Floor)
// =============================================================================
void renderFull3DBoilingLiquid(uint8_t t) {
  uint8_t r, g, b;
  for (int y = 0; y < PANEL_RES_Y; y++) {
    uint8_t ny = abs(y - 20) << 3;
    for (int x = 0; x < PANEL_RES_X; x++) {
      uint8_t nx = abs(x - 80) << 1;
      uint8_t wave = fastSin(nx + ny - (t << 3));

      // Floor value set to 160 so low points remain brightly lit
      uint8_t val = 160 + ((wave * 95) >> 8);
      uint8_t hue = wave + x + (t << 2);

      hsvToRgbFast(hue, 255, val, r, g, b);
      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 3: Molten Gold & Firestorm Waves (High Amperage Yellow/Orange)
// =============================================================================
void renderMoltenFirestorm(uint8_t t) {
  uint8_t r, g, b;
  for (int y = 0; y < PANEL_RES_Y; y++) {
    for (int x = 0; x < PANEL_RES_X; x++) {
      uint8_t n1 = fastSin((x << 2) + (t << 3));
      uint8_t n2 = fastCos((y << 3) - (t << 2));
      uint8_t mix = (n1 + n2) >> 1;

      r = 255;
      g = 120 + (mix >> 1); // 120..247 Green (creates intense orange/gold)
      b = (mix > 200) ? (mix - 200) * 4 : 20;

      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 4: Full-Screen Oscillating Harmonic Fluid
// =============================================================================
void renderFullHarmonicFluid(uint8_t t) {
  uint8_t r, g, b;
  for (int y = 0; y < PANEL_RES_Y; y++) {
    uint8_t waveY = fastCos((y << 3) + (t << 2));
    for (int x = 0; x < PANEL_RES_X; x++) {
      uint8_t waveX = fastSin((x << 2) - (t << 2));
      uint8_t hue = waveX + waveY + (t << 3);

      hsvToRgbFast(hue, 240, 255, r, g, b);
      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 5: Dynamic Aurora Plasma Curtain (No Dark Space)
// =============================================================================
void renderFullAuroraCurtain(uint8_t t) {
  uint8_t r, g, b;
  for (int x = 0; x < PANEL_RES_X; x++) {
    uint8_t waveA = fastSin((x << 2) + (t << 3));
    uint8_t waveB = fastCos((x * 3) - (t << 2));

    for (int y = 0; y < PANEL_RES_Y; y++) {
      uint8_t val = 180 + ((fastSin((y << 3) + waveA + waveB) * 75) >> 8);
      uint8_t hue = x + y + (t << 3);

      hsvToRgbFast(hue, 220, val, r, g, b);
      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 6: Full-Field Radial Warp Tunnel (100% Pixel Coverage)
// =============================================================================
void renderFullRadialWarp(uint8_t t) {
  uint8_t r, g, b;
  uint8_t cx = 80 + ((fastSin(t << 2) * 20) >> 8);
  uint8_t cy = 20 + ((fastCos(t << 2) * 8) >> 8);

  for (int y = 0; y < PANEL_RES_Y; y++) {
    uint8_t dy = abs(y - cy);
    for (int x = 0; x < PANEL_RES_X; x++) {
      uint8_t dx = abs(x - cx);
      uint8_t radius = (dx > dy) ? (dx + (dy >> 1)) : (dy + (dx >> 1));

      uint8_t ring = fastSin((radius << 3) - (t << 4));
      uint8_t val = 170 + ((ring * 85) >> 8); // Minimum brightness 170
      uint8_t hue = (radius << 1) + (t << 3);

      hsvToRgbFast(hue, 255, val, r, g, b);
      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 7: DNA Helix Supercharged with Full Rainbow Lava Background
// =============================================================================
void renderFullDNAStrandLava(uint8_t t) {
  uint8_t r, g, b;
  for (int x = 0; x < PANEL_RES_X; x++) {
    uint8_t strand1 = 20 + ((fastSin((x << 2) + (t << 3)) * 14) >> 8);
    uint8_t strand2 = 20 - ((fastSin((x << 2) + (t << 3)) * 14) >> 8);

    for (int y = 0; y < PANEL_RES_Y; y++) {
      bool isNode1 = (abs(y - strand1) <= 1);
      bool isNode2 = (abs(y - strand2) <= 1);

      if (isNode1 || isNode2) {
        r = 255; g = 255; b = 255; // Highlight strands in bright white
      } else {
        // Background filled with active rainbow wave
        uint8_t bgHue = x + y + (t << 3);
        hsvToRgbFast(bgHue, 255, 180, r, g, b);
      }
      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 8: Chromatic Supernova Ripple (High-Intensity Pulsing Field)
// =============================================================================
void renderFullSupernovaRipple(uint8_t t) {
  uint8_t r, g, b;
  for (int y = 0; y < PANEL_RES_Y; y++) {
    for (int x = 0; x < PANEL_RES_X; x++) {
      uint8_t sweep = fastSin((x << 2) + (y << 3) - (t << 4));
      uint8_t hue = (x << 1) + (t << 3);

      // Flash between bright saturated color and pure white bursts
      if (sweep > 200) {
        r = 255; g = 255; b = 255;
      } else {
        hsvToRgbFast(hue, 255, 190 + (sweep >> 2), r, g, b);
      }
      dma_display->drawPixelRGB888(x, y, r, g, b);
    }
  }
}

// =============================================================================
// EFFECT 9: PEAK LOAD TEST MODE (100% Solid White / Maximum Amperage Draw)
// =============================================================================
void renderMaxAmperageBurnIn(uint8_t t) {
  // Drives Red, Green, and Blue diodes on ALL 6,400 pixels to 100% max current
  uint16_t white = dma_display->color565(255, 255, 255);
  dma_display->fillRect(0, 0, PANEL_RES_X, PANEL_RES_Y, white);
}

// =============================================================================
// Setup & Loop
// =============================================================================
void setup() {
  Serial.begin(115200);

  initTrigLUT();

  HUB75_I2S_CFG::i2s_pins _pins = {
    R1_PIN, G1_PIN, B1_PIN,
    R2_PIN, G2_PIN, B2_PIN,
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
    LAT_PIN, OE_PIN, CLK_PIN
  };

  HUB75_I2S_CFG mxconfig(80, PANEL_RES_Y, PANEL_CHAIN, _pins);
  mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
  mxconfig.clkphase = false;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();

  // MAX PWM Duty Cycle (255) for maximum current output
  dma_display->setBrightness8(255);

  lastEffectSwitch = millis();
}

void loop() {
  unsigned long now = millis();

  if (now - lastEffectSwitch >= EFFECT_DURATION) {
    lastEffectSwitch = now;
    currentEffect = (currentEffect + 1) % NUM_EFFECTS;
  }

  animStep++;

  switch (currentEffect) {
    case 0: renderFullMagmaLava(animStep);          break; // Lava/Magma
    case 1: renderFullSpectrumPlasma(animStep);     break; // Full Rainbow Plasma
    case 2: renderFull3DBoilingLiquid(animStep);     break; // Boiling Liquid Surface
    case 3: renderMoltenFirestorm(animStep);        break; // Molten Firestorm
    case 4: renderFullHarmonicFluid(animStep);      break; // Oscillating Fluid
    case 5: renderFullAuroraCurtain(animStep);      break; // Aurora Wave Curtain
    case 6: renderFullRadialWarp(animStep);         break; // Radial Warp Field
    case 7: renderFullDNAStrandLava(animStep);      break; // DNA Helix + Lava BG
    case 8: renderFullSupernovaRipple(animStep);    break; // Supernova Ripples
    case 9: renderMaxAmperageBurnIn(animStep);      break; // MAX POWER TEST (Solid White)
  }

  delay(1);
}
