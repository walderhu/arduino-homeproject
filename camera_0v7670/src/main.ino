#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../config.h"
#include "BMP.h"
#include "OV7670.h"

// OV7670 -> ESP32-WROOM pin map, avoiding boot strap pins.
constexpr int PIN_SIOD = 21;  // OV7670 SIOD/SDA
constexpr int PIN_SIOC = 22;  // OV7670 SIOC/SCL
constexpr int PIN_VSYNC = 25; // OV7670 VSYNC
constexpr int PIN_HREF = 26;  // OV7670 HREF
constexpr int PIN_PCLK = 33;  // OV7670 PCLK
constexpr int PIN_XCLK = 32;  // OV7670 XCLK

constexpr int PIN_D0 = 34; // OV7670 D0 / Y2
constexpr int PIN_D1 = 35; // OV7670 D1 / Y3
constexpr int PIN_D2 = 36; // OV7670 D2 / Y4
constexpr int PIN_D3 = 39; // OV7670 D3 / Y5
constexpr int PIN_D4 = 18; // OV7670 D4 / Y6
constexpr int PIN_D5 = 19; // OV7670 D5 / Y7
constexpr int PIN_D6 = 23; // OV7670 D6 / Y8
constexpr int PIN_D7 = 27; // OV7670 D7 / Y9

WebServer server(80);
OV7670 *camera = nullptr;
uint8_t bmpHeader[BMP::headerSize];
uint8_t downsampledFrame[80 * 60 * 2];
bool snapshotReady = false;

const char INDEX_HTML[] PROGMEM = R"html(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 OV7670 snapshot</title>
  <style>
    body { margin: 0; font-family: system-ui, sans-serif; background: #111; color: #eee; }
    main { max-width: 760px; margin: 0 auto; padding: 18px; }
    canvas { width: 640px; max-width: 100%; image-rendering: pixelated; background: #000; }
    code { color: #9fe870; }
  </style>
</head>
<body>
  <main>
    <h1>ESP32 OV7670 live</h1>
    <p>Live endpoint: <code>/rawstream</code></p>
    <canvas id="frame" width="80" height="60"></canvas>
  </main>
  <script>
    const width = 80;
    const height = 60;
    const frameBytes = width * height * 2;
    const canvas = document.getElementById('frame');
    const ctx = canvas.getContext('2d', { alpha: false });
    const image = ctx.createImageData(width, height);
    let pending = new Uint8Array(0);

    function drawFrame(bytes) {
      const pixels = image.data;
      for (let i = 0, p = 0; i < frameBytes; i += 2, p += 4) {
        const value = bytes[i] | (bytes[i + 1] << 8);
        pixels[p] = ((value >> 11) & 0x1f) * 255 / 31;
        pixels[p + 1] = ((value >> 5) & 0x3f) * 255 / 63;
        pixels[p + 2] = (value & 0x1f) * 255 / 31;
        pixels[p + 3] = 255;
      }
      ctx.putImageData(image, 0, 0);
    }

    async function startStream() {
      const response = await fetch('/rawstream', { cache: 'no-store' });
      const reader = response.body.getReader();
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;

        const merged = new Uint8Array(pending.length + value.length);
        merged.set(pending);
        merged.set(value, pending.length);

        const completeFrames = Math.floor(merged.length / frameBytes);
        if (completeFrames > 0) {
          const latestOffset = (completeFrames - 1) * frameBytes;
          drawFrame(merged.subarray(latestOffset, latestOffset + frameBytes));
          pending = merged.slice(completeFrames * frameBytes);
        } else {
          pending = merged;
        }
      }
      setTimeout(startStream, 500);
    }

    startStream().catch(() => setTimeout(startStream, 500));
  </script>
</body>
</html>
)html";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleHealth() {
    server.send(200, "text/plain", snapshotReady ? "snapshot ready" : "snapshot not ready");
}

void handleCamera() {
    if (camera == nullptr) {
        server.send(503, "text/plain", "camera not ready");
        return;
    }

    Serial.println("HTTP snapshot request");
    camera->oneFrame();
    snapshotReady = true;
    Serial.println("HTTP snapshot captured");

    WiFiClient client = server.client();
    const size_t imageBytes = camera->xres * camera->yres * 2;
    server.setContentLength(BMP::headerSize + imageBytes);
    server.send(200, "image/bmp", "");
    client.write(bmpHeader, BMP::headerSize);
    client.write(camera->frame, imageBytes);
}

void handleStream() {
    if (camera == nullptr) {
        server.send(503, "text/plain", "camera not ready");
        return;
    }

    WiFiClient client = server.client();
    client.setNoDelay(true);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println();

    const size_t imageBytes = camera->xres * camera->yres * 2;
    while (client.connected()) {
        camera->oneFrame();
        client.println("--frame");
        client.println("Content-Type: image/bmp");
        client.printf("Content-Length: %u\r\n",
                      static_cast<unsigned>(BMP::headerSize + imageBytes));
        client.println();
        client.write(bmpHeader, BMP::headerSize);
        client.write(camera->frame, imageBytes);
        client.println();
        delay(1);
    }
}

void handleRawStream() {
    if (camera == nullptr) {
        server.send(503, "text/plain", "camera not ready");
        return;
    }

    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/octet-stream");
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println();

    const size_t imageBytes = sizeof(downsampledFrame);
    uint32_t frameCounter = 0;
    while (client.connected()) {
        const uint32_t captureStart = millis();
        camera->oneFrame();
        const uint32_t captureMs = millis() - captureStart;
        size_t dst = 0;
        for (int y = 0; y < camera->yres; y += 2) {
            const size_t row = y * camera->xres * 2;
            for (int x = 0; x < camera->xres; x += 2) {
                const size_t src = row + x * 2;
                downsampledFrame[dst++] = camera->frame[src];
                downsampledFrame[dst++] = camera->frame[src + 1];
            }
        }
        const uint32_t writeStart = millis();
        const size_t written = client.write(downsampledFrame, imageBytes);
        const uint32_t writeMs = millis() - writeStart;
        frameCounter++;
        if (frameCounter % 30 == 0) {
            Serial.printf("rawstream frame=%u capture=%u ms write=%u ms bytes=%u/%u\n",
                          static_cast<unsigned>(frameCounter), static_cast<unsigned>(captureMs),
                          static_cast<unsigned>(writeMs), static_cast<unsigned>(written),
                          static_cast<unsigned>(imageBytes));
        }
        delay(1);
    }
}

void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);

    Serial.printf("Connecting to Wi-Fi SSID \"%s\"\n", SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }

    Serial.println();
    Serial.print("Open in browser: http://");
    Serial.println(WiFi.localIP());
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("ESP32-WROOM OV7670 bitluni one snapshot test");

    Serial.println("Initializing OV7670 bitluni I2S driver");
    camera =
        new OV7670(OV7670::Mode::QQVGA_RGB565, PIN_SIOD, PIN_SIOC, PIN_VSYNC, PIN_HREF, PIN_XCLK,
                   PIN_PCLK, PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D4, PIN_D5, PIN_D6, PIN_D7);

    Serial.printf("Camera mode: %dx%d RGB565\n", camera->xres, camera->yres);
    BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);

    snapshotReady = true;

    connectWifi();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/camera.bmp", HTTP_GET, handleCamera);
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/rawstream", HTTP_GET, handleRawStream);
    server.begin();
    Serial.println("HTTP server started");
}

void loop() { server.handleClient(); }
