# BUS REAR — Number Display

An ESP32 drives a single 64×64 HUB75 LED matrix showing the rear bus number.
A Python script sends the number over WiFi to the ESP32 at runtime — **no re-flashing required**.

## Files
- `main/main.ino` — Arduino code for the ESP32. Connects to your WiFi and runs a TCP server.
- `display.py` — Python client that connects to the ESP32 and sends a bus number.

## Wiring
Single 64×64 panel, SHIFTREG driver, `clkphase = false`. Pin mapping is at the top of `main.ino`.

## Setup

1. Install the Arduino library **ESP32-HUB75-MatrixPanel-I2S-DMA** (Library Manager).
2. Change the SSID/password in `main/main.ino` to match your home WiFi:

   ```cpp
   const char *ssid     = "i-Router";
   const char *password = "PSWEJB84XR";
   ```

3. Upload `main/main.ino` to the ESP32 **once**.
4. Open the Arduino Serial Monitor (115200 baud). It prints the IP the ESP32
   was assigned by your router, e.g.:

   ```
   [wifi] Connected to 'iRouter'. IP: 192.168.1.42
   ```

## Usage

```bash
python3 display.py              # interactive — asks for a number to display
python3 display.py 42           # show bus number 42 then exit
python3 display.py --ip 192.168.1.42 99
python3 display.py --loop 2 99  # send 99 every 2 seconds (Ctrl+C to stop)
```

In interactive mode, type a number (1-999) and press Enter. The ESP32 renders it on the panel instantly. Send as many times as you like — the ESP32 keeps whatever number you last sent. Run `q` to quit.
