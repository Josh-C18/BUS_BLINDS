#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <WiFi.h>
#include <Preferences.h>

// --- Matrix Panel Settings (two chained 80x40 panels -> 160x40) ---
#define PANEL_RES_X 160      // Total width: 2 x 80px panels
#define PANEL_RES_Y 40       // Total height

// --- Zone Layout ---
#define NUM_ZONE_W  44       // Left zone reserved for bus number
#define GAP_W       2        // Gap between number and stop text
#define TEXT_ZONE_X (NUM_ZONE_W + GAP_W)               // Starts at X = 46
#define TEXT_ZONE_W (PANEL_RES_X - TEXT_ZONE_X)        // 114px wide for text

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
#define E_PIN  16

#define LAT_PIN 11
#define OE_PIN  13
#define CLK_PIN 12

MatrixPanel_I2S_DMA *dma_display = nullptr;

WiFiServer server(7654);
WiFiClient currentClient;

const char *ssid     = "iRouter";
const char *password = "PSWEJB84XR";

Preferences prefs;

int    currentNumber = 0;
String currentStop   = "";
bool   hasNumber      = false;
bool   hasStop        = false;

const int MAX_TEXT = 32;
String lineBuffer  = "";

// --- Pre-rendered text canvas (cached to eliminate runtime scaling lag) ---
GFXcanvas1 *stopCanvas = nullptr;
int stopOutW = 0;
int stopOutH = 0;

// State management flags
bool forceRedrawNum  = true;
bool forceRedrawText = true;
int  scrollStep      = 0;
unsigned long lastScrollTick = 0;

// --- NVS Storage ---
bool loadStored() {
  prefs.begin("front", false);
  bool has = false;
  if (prefs.isKey("num")) {
    uint32_t n = prefs.getUInt("num", 0);
    if (n >= 1 && n <= 9999) {
      currentNumber = (int)n;
      hasNumber = true;
      has = true;
    }
  }
  if (prefs.isKey("stop")) {
    String s = prefs.getString("stop", "");
    if (s.length() > 0 && s.length() <= MAX_TEXT) {
      currentStop = s;
      hasStop = true;
      has = true;
    }
  }
  prefs.end();
  return has;
}

void storeNumber(int number) {
  prefs.begin("front", false);
  prefs.putUInt("num", (uint16_t)number);
  prefs.end();
}

void storeText(String text) {
  prefs.begin("front", false);
  prefs.putString("stop", text);
  prefs.end();
}

// --- Pre-render stop text into 1-bit buffer once on arrival ---
void prepareStopCanvas() {
  if (stopCanvas) {
    delete stopCanvas;
    stopCanvas = nullptr;
    stopOutW = 0;
    stopOutH = 0;
  }

  if (!hasStop || currentStop.length() == 0) return;

  GFXcanvas1 tmp(512, 128);
  tmp.setFont(&FreeSansBold12pt7b);
  tmp.fillScreen(0);

  int16_t x1, y1;
  uint16_t w1, h1;
  tmp.getTextBounds(currentStop, 0, 0, &x1, &y1, &w1, &h1);
  if (w1 == 0 || h1 == 0) return;

  tmp.setCursor(-x1, -y1);
  tmp.print(currentStop);

  const int margin = 2;
  int maxH = PANEL_RES_Y - (2 * margin); // 36px target height
  float scaleY = (float)maxH / (float)h1;

  stopOutH = max(1, (int)floor((float)h1 * scaleY));
  stopOutW = max(1, (int)floor((float)w1 * scaleY));

  stopCanvas = new GFXcanvas1(stopOutW, stopOutH);
  stopCanvas->fillScreen(0);

  for (int dy = 0; dy < stopOutH; dy++) {
    int sy = (dy * h1) / stopOutH;
    for (int dx = 0; dx < stopOutW; dx++) {
      int sx = (dx * w1) / stopOutW;
      if (tmp.getPixel(sx, sy)) {
        stopCanvas->drawPixel(dx, dy, 1);
      }
    }
  }
}

void clearAll() {
  prefs.begin("front", false);
  prefs.remove("num");
  prefs.remove("stop");
  prefs.end();
  currentNumber = 0;
  currentStop = "";
  hasNumber = false;
  hasStop = false;
  if (stopCanvas) {
    delete stopCanvas;
    stopCanvas = nullptr;
  }
  forceRedrawNum = true;
  forceRedrawText = true;
  if (dma_display) dma_display->clearScreen();
}

bool isPrintable(char c) {
  return c >= ' ' && c <= '~';
}

// --- Left Zone: Bus Number (Stretched taller & condensed width) ---
void drawLeftNumber() {
  uint16_t white = dma_display->color565(255, 255, 255);

  if (!hasNumber) {
    dma_display->fillRect(0, 0, NUM_ZONE_W, PANEL_RES_Y, 0);
    return;
  }

  String text = String(currentNumber);
  GFXcanvas1 tmp(128, 64);
  tmp.setFont(&FreeSansBold12pt7b);
  tmp.fillScreen(0);

  int16_t x1, y1;
  uint16_t w1, h1;
  tmp.getTextBounds(text, 0, 0, &x1, &y1, &w1, &h1);
  if (w1 == 0 || h1 == 0) return;

  tmp.setCursor(-x1, -y1);
  tmp.print(text);

  int margin = 2;
  int outH = PANEL_RES_Y - (2 * margin); // Force full vertical height (36px)

  // Stretch taller and thin out width ratio (75% aspect scale)
  int unconstrainedW = (w1 * outH) / h1;
  int outW = (unconstrainedW * 3) / 4; 
  if (outW > NUM_ZONE_W - 2) outW = NUM_ZONE_W - 2;
  if (outW < 1) outW = 1;

  int dstX = (NUM_ZONE_W - outW) / 2;
  int dstY = (PANEL_RES_Y - outH) / 2;

  // Single-pass overwrite (no fillRect clear -> zero flicker)
  for (int y = 0; y < PANEL_RES_Y; y++) {
    int canvasY = y - dstY;
    bool validY = (canvasY >= 0 && canvasY < outH);

    for (int x = 0; x < NUM_ZONE_W; x++) {
      int canvasX = x - dstX;
      bool isBit = false;

      if (validY && canvasX >= 0 && canvasX < outW) {
        int sx = (canvasX * w1) / outW;
        int sy = (canvasY * h1) / outH;
        if (tmp.getPixel(sx, sy)) isBit = true;
      }
      dma_display->drawPixel(x, y, isBit ? white : 0);
    }
  }
}

// --- Right Zone: Final Stop Text (Flicker-free single-pass rendering) ---
void drawRightText(int step) {
  uint16_t white = dma_display->color565(255, 255, 255);

  if (!hasStop || !stopCanvas || stopOutW == 0 || stopOutH == 0) {
    dma_display->fillRect(TEXT_ZONE_X, 0, TEXT_ZONE_W, PANEL_RES_Y, 0);
    return;
  }

  int dstY = (PANEL_RES_Y - stopOutH) / 2;

  if (stopOutW <= TEXT_ZONE_W) {
    // Fits inside right zone statically (center horizontally)
    int dstX = TEXT_ZONE_X + (TEXT_ZONE_W - stopOutW) / 2;

    for (int y = 0; y < PANEL_RES_Y; y++) {
      int canvasY = y - dstY;
      bool validY = (canvasY >= 0 && canvasY < stopOutH);

      for (int x = 0; x < TEXT_ZONE_W; x++) {
        int screenX = TEXT_ZONE_X + x;
        int canvasX = x - (dstX - TEXT_ZONE_X);

        bool isBit = validY && (canvasX >= 0 && canvasX < stopOutW) && stopCanvas->getPixel(canvasX, canvasY);
        dma_display->drawPixel(screenX, y, isBit ? white : 0);
      }
    }
  } else {
    // Scroll horizontally with hold delays at ends
    int maxOff = stopOutW - TEXT_ZONE_W;
    int holdSteps = 20; 
    int totalSteps = maxOff + (holdSteps * 2);

    int posInCycle = step % totalSteps;
    int scrollPos = 0;

    if (posInCycle < holdSteps) {
      scrollPos = 0;
    } else if (posInCycle < holdSteps + maxOff) {
      scrollPos = posInCycle - holdSteps;
    } else {
      scrollPos = maxOff;
    }

    for (int y = 0; y < PANEL_RES_Y; y++) {
      int canvasY = y - dstY;
      bool validY = (canvasY >= 0 && canvasY < stopOutH);

      for (int x = 0; x < TEXT_ZONE_W; x++) {
        int screenX = TEXT_ZONE_X + x;
        int canvasX = scrollPos + x;

        bool isBit = validY && (canvasX >= 0 && canvasX < stopOutW) && stopCanvas->getPixel(canvasX, canvasY);
        dma_display->drawPixel(screenX, y, isBit ? white : 0);
      }
    }
  }
}

void updateDisplay() {
  if (forceRedrawNum) {
    forceRedrawNum = false;
    drawLeftNumber();
  }

  unsigned long now = millis();
  bool tickPassed = false;

  if (now - lastScrollTick >= 35) { // 35ms tick rate (~28 FPS smooth scroll)
    lastScrollTick = now;
    scrollStep++;
    tickPassed = true;
  }

  if (forceRedrawText || tickPassed) {
    forceRedrawText = false;
    drawRightText(scrollStep);
  }
}

void setup() {
  Serial.begin(115200);

  HUB75_I2S_CFG::i2s_pins _pins = {
    R1_PIN, G1_PIN, B1_PIN,
    R2_PIN, G2_PIN, B2_PIN,
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
    LAT_PIN, OE_PIN, CLK_PIN
  };

  HUB75_I2S_CFG mxconfig(80, PANEL_RES_Y, 2, _pins);
  mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
  mxconfig.clkphase = false;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(80);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[wifi] Connected to '%s'. IP: %s\n", ssid, WiFi.localIP().toString().c_str());

  server.begin();
  Serial.printf("[wifi] TCP server listening on port 7654\n");

  if (loadStored()) {
    prepareStopCanvas();
    Serial.printf("[boot] Restored number=%d stop='%s'\n",
                  hasNumber ? currentNumber : -1, hasStop ? currentStop.c_str() : "(none)");
  } else {
    dma_display->clearScreen();
  }
}

void loop() {
  if (!currentClient || !currentClient.connected()) {
    WiFiClient newClient = server.accept();
    if (newClient) {
      currentClient = newClient;
      Serial.println("[wifi] Client connected");
    }
  }

  if (currentClient && currentClient.connected()) {
    while (currentClient.available()) {
      char c = currentClient.read();
      if (c == '\n') {
        lineBuffer.trim();
        if (lineBuffer.length() > 0) {
          if (lineBuffer.equalsIgnoreCase("CLR")) {
            clearAll();
            Serial.println("[wifi] CLR — cleared display");
          } else if (lineBuffer.startsWith("NUM ")) {
            String rest = lineBuffer.substring(4);
            rest.trim();
            int n = rest.toInt();
            if (n >= 1 && n <= 9999) {
              currentNumber = n;
              hasNumber = true;
              forceRedrawNum = true;
              storeNumber(n);
              Serial.printf("[wifi] Received number %d\n", n);
            }
          } else if (lineBuffer.startsWith("STOP ")) {
            String s = lineBuffer.substring(5);
            s.trim();
            bool ok = s.length() <= MAX_TEXT;
            for (unsigned int i = 0; i < s.length() && ok; i++) {
              if (!isPrintable(s.charAt(i))) ok = false;
            }
            if (ok) {
              currentStop = s;
              hasStop = true;
              scrollStep = 0;
              prepareStopCanvas();
              forceRedrawText = true;
              storeText(s);
              Serial.printf("[wifi] Received stop: %s\n", s.c_str());
            }
          }
        }
        lineBuffer = "";
      } else if (c != '\r') {
        lineBuffer += c;
      }
    }
  }

  updateDisplay();
  delay(1);
}