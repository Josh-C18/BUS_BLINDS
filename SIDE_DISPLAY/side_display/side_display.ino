#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <WiFi.h>
#include <Preferences.h>

// --- Matrix Panel Settings (single 80x40 panel) ---
#define PANEL_RES_X 80      // Width of panel
#define PANEL_RES_Y 40      // Height of panel
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
#define E_PIN  16   // Line E active for 64x64 / 80x40 scanning

#define LAT_PIN 11
#define OE_PIN  13
#define CLK_PIN 12

MatrixPanel_I2S_DMA *dma_display = nullptr;

// Define TCP server port at initialization
WiFiServer server(7654);
WiFiClient currentClient;

const char *ssid     = "iRouter";
const char *password = "PSWEJB84XR";

// Persistent store for the last stop string (survives power cycles)
Preferences prefs;

const int MAX_TEXT = 16;
String lineBuffer = "";
String currentText = "";
bool havePending = false;

// Load the stored text from NVS. Returns true if a value exists.
bool loadStoredText() {
  prefs.begin("stop", false);
  bool hasValue = prefs.isKey("txt");
  if (hasValue) {
    String stored = prefs.getString("txt", "");
    if (stored.length() > 0 && stored.length() <= MAX_TEXT) {
      currentText = stored;
    } else {
      hasValue = false;
    }
  }
  prefs.end();
  return hasValue;
}

// Persist the given text so it survives a power cycle.
void storeText(String text) {
  prefs.begin("stop", false);
  prefs.putString("txt", text);
  prefs.end();
}

// Remove the stored text from memory and wipe the display.
void clearStoredText() {
  prefs.begin("stop", false);
  prefs.remove("txt");
  prefs.end();
  currentText = "";
  if (dma_display) {
    dma_display->clearScreen();
  }
}

// Check whether a character is printable ASCII (space .. tilde).
bool isPrintable(char c) {
  return c >= ' ' && c <= '~';
}

void drawTextCentered(String text) {
  if (text.length() == 0) {
    dma_display->clearScreen();
    return;
  }

  // 1. Create memory canvas large enough for full text surface
  GFXcanvas1 canvas(512, 128);
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setTextSize(1);
  canvas.setTextColor(1);
  canvas.fillScreen(0);

  // 2. Measure text bounds
  int16_t x1, y1;
  uint16_t w1, h1;
  canvas.getTextBounds(text, 0, 0, &x1, &y1, &w1, &h1);

  if (w1 == 0 || h1 == 0) return;

  // 3. Render text onto canvas offset by (-x1, -y1) so top-left sits at (0,0)
  canvas.setCursor(-x1, -y1);
  canvas.print(text);

  // 4. Calculate aspect-preserved scaling to fit within matrix margins
  const int margin = 2;
  const int maxW = PANEL_RES_X - (2 * margin);
  const int maxH = PANEL_RES_Y - (2 * margin);

  float scaleX = (float)maxW / (float)w1;
  float scaleY = (float)maxH / (float)h1;
  float scale = min(scaleX, scaleY);

  int outW = max(1, (int)floor((float)w1 * scale));
  int outH = max(1, (int)floor((float)h1 * scale));

  // 5. Calculate centered destination position on matrix
  int dstX = (PANEL_RES_X - outW) / 2;
  int dstY = (PANEL_RES_Y - outH) / 2;

  // 6. Draw scaled pixels onto Matrix
  dma_display->clearScreen();
  uint16_t white = dma_display->color565(255, 255, 255);

  for (int dy = 0; dy < outH; dy++) {
    int sy = (dy * h1) / outH;
    for (int dx = 0; dx < outW; dx++) {
      int sx = (dx * w1) / outW;
      if (canvas.getPixel(sx, sy)) {
        dma_display->drawPixel(dstX + dx, dstY + dy, white);
      }
    }
  }
}

void applyPendingText() {
  if (!havePending) return;
  havePending = false;
  drawTextCentered(currentText);
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

  if (loadStoredText()) {
    Serial.printf("[boot] Restored stop: %s\n", currentText.c_str());
    drawTextCentered(currentText);
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
            clearStoredText();
            Serial.println("[wifi] CLR — cleared the display");
          } else {
            bool ok = lineBuffer.length() <= MAX_TEXT;
            for (unsigned int i = 0; i < lineBuffer.length() && ok; i++) {
              if (!isPrintable(lineBuffer.charAt(i))) {
                ok = false;
              }
            }
            if (!ok) {
              Serial.printf("[wifi] Ignoring invalid text: %s\n", lineBuffer.c_str());
            } else {
              currentText = lineBuffer;
              havePending = true;
              storeText(currentText);
              Serial.printf("[wifi] Received stop: %s\n", currentText.c_str());
            }
          }
        }
        lineBuffer = "";
      } else if (c != '\r') {
        lineBuffer += c;
      }
    }
  }

  applyPendingText();
  delay(1);
}