#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <WiFi.h>
#include <Preferences.h>

// --- Matrix Panel Settings (Single 64x64 Panel) ---
#define PANEL_RES_X 64      // Width of panel
#define PANEL_RES_Y 64      // Height of panel
#define PANEL_CHAIN 1       // 1 panel connected

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
#define E_PIN  16   // Line E active for 64x64 scanning

#define LAT_PIN 11
#define OE_PIN  13
#define CLK_PIN 12

MatrixPanel_I2S_DMA *dma_display = nullptr;

// Define TCP server port at initialization
WiFiServer server(7654);
WiFiClient currentClient;

const char *ssid     = "iRouter";
const char *password = "PSWEJB84XR";

// Persistent store for the last bus number (survives power cycles)
Preferences prefs;

uint16_t lastNumber = 0;
String lineBuffer = "";
bool havePending = false;
bool hasStoredNumber = false;

// Load the stored number from NVS. Returns true if a number exists.
bool loadStoredNumber() {
  prefs.begin("bus", false);
  bool hasValue = prefs.isKey("num");
  if (hasValue) {
    uint32_t stored = prefs.getUInt("num", 0);
    if (stored < 1 || stored > 999) {
      hasValue = false;
    } else {
      lastNumber = (uint16_t)stored;
      hasStoredNumber = true;
    }
  }
  prefs.end();
  return hasValue;
}

// Persist the given number so it survives a power cycle.
void storeNumber(int number) {
  prefs.begin("bus", false);
  prefs.putUInt("num", (uint16_t)number);
  prefs.end();
}

// Remove the stored number from memory and wipe the display.
void clearStoredNumber() {
  prefs.begin("bus", false);
  prefs.remove("num");
  prefs.end();
  lastNumber = 0;
  hasStoredNumber = false;
  dma_display->clearScreen();
}

void drawBusNumberCentered(int number) {
  String text = String(number);

  // 1. Render glyph onto 1-bit memory canvas
  GFXcanvas1 canvas(64, 64);
  canvas.setFont(&FreeSansBold18pt7b);
  canvas.setTextSize(1);
  canvas.setTextColor(1);
  canvas.fillScreen(0);

  int16_t x1, y1;
  uint16_t w1, h1;
  canvas.getTextBounds(text, 0, 0, &x1, &y1, &w1, &h1);

  int baseCursorX = (64 - (int)w1) / 2 - x1;
  int baseCursorY = (64 - (int)h1) / 2 - y1;
  canvas.setCursor(baseCursorX, baseCursorY);
  canvas.print(text);

  // 2. Scan active pixel bounding box
  int minX = 64, maxX = -1, minY = 64, maxY = -1;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      if (canvas.getPixel(x, y)) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }

  if (maxX < minX || maxY < minY) return;

  int srcW = maxX - minX + 1;
  int srcH = maxY - minY + 1;

  // 3. Exact target vertical bounds (4px top margin, 4px bottom margin -> 56px tall)
  int dstY = 4;
  int dstH = 56;

  // 4. Calculate target width
  int dstW = (srcW * dstH) / srcH;

  if (number < 10) {
    if (dstW < 32) dstW = 32;
  } else {
    if (dstW > 60) dstW = 60;
  }

  // 5. Calculate horizontal center position
  int dstX = (64 - dstW) / 2;

  if (number == 1) {
    dstX -= 2;
  }

  // 6. Draw scaled pixels onto matrix
  dma_display->clearScreen();
  uint16_t white = dma_display->color565(255, 255, 255);

  for (int dy = 0; dy < dstH; dy++) {
    int sy = minY + (dy * srcH) / dstH;
    for (int dx = 0; dx < dstW; dx++) {
      int sx = minX + (dx * srcW) / dstW;
      if (canvas.getPixel(sx, sy)) {
        dma_display->drawPixel(dstX + dx, dstY + dy, white);
      }
    }
  }
}

void applyPendingNumber() {
  if (!havePending) return;
  havePending = false;
  drawBusNumberCentered(lastNumber);
}

void setup() {
  Serial.begin(115200);

  HUB75_I2S_CFG::i2s_pins _pins = {
    R1_PIN, G1_PIN, B1_PIN,
    R2_PIN, G2_PIN, B2_PIN,
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
    LAT_PIN, OE_PIN, CLK_PIN
  };

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, _pins);
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

  // Restore last stored number on boot (if any). Blank otherwise.
  if (loadStoredNumber()) {
    Serial.printf("[boot] Restored bus number %d\n", lastNumber);
    drawBusNumberCentered(lastNumber);
  } else {
    // Boot blank — display is on but waiting for a bus number to be received.
    dma_display->clearScreen();
  }
}

void loop() {
  // Accept new client if none is currently connected
  if (!currentClient || !currentClient.connected()) {
    WiFiClient newClient = server.accept();
    if (newClient) {
      currentClient = newClient;
      Serial.println("[wifi] Client connected");
    }
  }

  // Read incoming byte stream
  if (currentClient && currentClient.connected()) {
    while (currentClient.available()) {
      char c = currentClient.read();
      if (c == '\n') {
        lineBuffer.trim();
        if (lineBuffer.length() > 0) {
          if (lineBuffer.equalsIgnoreCase("CLR")) {
            clearStoredNumber();
            Serial.println("[wifi] CLR — cleared stored number");
          } else {
            int parsed = lineBuffer.toInt();
            if (parsed < 1 || parsed > 999) {
              Serial.printf("[wifi] Ignoring invalid number: %s\n", lineBuffer.c_str());
            } else {
              havePending = true;
              lastNumber = parsed;
              storeNumber(parsed); // persist so it survives a power cycle
              Serial.printf("[wifi] Received bus number %d\n", parsed);
            }
          }
        }
        lineBuffer = "";
      } else if (c != '\r') {
        lineBuffer += c;
      }
    }
  }

  applyPendingNumber();
  delay(1); // Small delay for system task stability
}