#include "../config.h"
#include "esp_camera.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

// ── Camera pins ───────────────────────────────────────────────────────────────
#if defined(CAMERA_MODEL_FREENOVE_S3) || defined(CAMERA_MODEL_ESP32S3_EYE)
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_Y9 16
#define CAM_PIN_Y8 17
#define CAM_PIN_Y7 18
#define CAM_PIN_Y6 12
#define CAM_PIN_Y5 10
#define CAM_PIN_Y4 8
#define CAM_PIN_Y3 9
#define CAM_PIN_Y2 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

#elif defined(CAMERA_MODEL_CUSTOM)
// Edit these to match your board's schematic:
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_Y9 16
#define CAM_PIN_Y8 17
#define CAM_PIN_Y7 18
#define CAM_PIN_Y6 12
#define CAM_PIN_Y5 10
#define CAM_PIN_Y4 8
#define CAM_PIN_Y3 9
#define CAM_PIN_Y2 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

#else
#error "No camera model defined — edit config.h"
#endif

// ── Constants ─────────────────────────────────────────────────────────────────
static const char BOUNDARY[] = "framebound";

static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Camera</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #111; color: #eee;
      font-family: system-ui, sans-serif;
      display: flex; flex-direction: column;
      align-items: center; padding: 20px; gap: 12px;
    }
    h1 { font-size: 1.3rem; }
    img { max-width: 100%; border: 2px solid #444; border-radius: 6px; }
    .info { font-size: 0.78rem; color: #777; }
    a { color: #9fe870; }
  </style>
</head>
<body>
  <h1>ESP32-S3 Live Stream</h1>
  <img id="stream" alt="loading...">
  <div class="info">
    Direct stream URL: <a id="link" href="#">loading...</a>
  </div>
  <script>
    const ip   = window.location.hostname;
    const url  = 'http://' + ip + ':81/stream';
    document.getElementById('stream').src = url;
    const link = document.getElementById('link');
    link.href  = url;
    link.textContent = url;
  </script>
</body>
</html>
)html";

// ── Globals ───────────────────────────────────────────────────────────────────
WebServer server(80);
WiFiServer streamServer(81);

// ── Camera init ───────────────────────────────────────────────────────────────
static bool initCamera() {
    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_PIN_Y2;
    cfg.pin_d1 = CAM_PIN_Y3;
    cfg.pin_d2 = CAM_PIN_Y4;
    cfg.pin_d3 = CAM_PIN_Y5;
    cfg.pin_d4 = CAM_PIN_Y6;
    cfg.pin_d5 = CAM_PIN_Y7;
    cfg.pin_d6 = CAM_PIN_Y8;
    cfg.pin_d7 = CAM_PIN_Y9;
    cfg.pin_xclk = CAM_PIN_XCLK;
    cfg.pin_pclk = CAM_PIN_PCLK;
    cfg.pin_vsync = CAM_PIN_VSYNC;
    cfg.pin_href = CAM_PIN_HREF;
    cfg.pin_sccb_sda = CAM_PIN_SIOD;
    cfg.pin_sccb_scl = CAM_PIN_SIOC;
    cfg.pin_pwdn = CAM_PIN_PWDN;
    cfg.pin_reset = CAM_PIN_RESET;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = FRAME_SIZE;
    cfg.jpeg_quality = JPEG_QUALITY;
    cfg.fb_count = psramFound() ? 2 : 1;
    cfg.grab_mode = CAMERA_GRAB_LATEST;
    cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

    return esp_camera_init(&cfg) == ESP_OK;
}

// ── MJPEG stream ──────────────────────────────────────────────────────────────
static void handleStream(void *arg) {
    WiFiClient client = *reinterpret_cast<WiFiClient *>(arg);
    delete reinterpret_cast<WiFiClient *>(arg);

    // Consume HTTP request headers
    while (client.connected() && client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r")
            break;
    }

    client.printf("HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace;boundary=%s\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n",
                  BOUNDARY);

    while (client.connected()) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            delay(10);
            continue;
        }

        client.printf("--%s\r\n"
                      "Content-Type: image/jpeg\r\n"
                      "Content-Length: %u\r\n\r\n",
                      BOUNDARY, fb->len);
        client.write(fb->buf, fb->len);
        client.print("\r\n");

        esp_camera_fb_return(fb);
    }

    client.stop();
    vTaskDelete(NULL);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=== ESP32-S3 Camera Stream ===");

    if (!initCamera()) {
        Serial.println("[ERROR] Camera init failed — check model/pins in config.h");
        while (true)
            delay(1000);
    }
    Serial.println("[OK] Camera initialized");
    if (psramFound())
        Serial.println("[OK] PSRAM found");
    else
        Serial.println("[WARN] No PSRAM — quality/fps limited");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());

    server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
    server.begin();
    streamServer.begin();

    Serial.printf("[HTTP]  http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[Stream] http://%s:81/stream\n", WiFi.localIP().toString().c_str());
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    server.handleClient();

    WiFiClient client = streamServer.accept();
    if (client) {
        // Each viewer gets own FreeRTOS task — non-blocking
        WiFiClient *clientPtr = new WiFiClient(client);
        xTaskCreate(handleStream, "stream", 8192, clientPtr, 1, NULL);
    }
}
