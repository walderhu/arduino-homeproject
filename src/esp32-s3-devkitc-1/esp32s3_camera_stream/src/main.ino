#include "../config.h"
#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

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
#define PART_BOUNDARY "framebound"
static const char BOUNDARY[] = PART_BOUNDARY;

#ifndef STREAM_TARGET_FPS
#define STREAM_TARGET_FPS 0
#endif

#ifndef STREAM_CLOSE_AFTER_MS
#define STREAM_CLOSE_AFTER_MS 500
#endif

static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Camera</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body {
      width: 100%;
      height: 100%;
      overflow: hidden;
    }
    body {
      background: #111; color: #eee;
      font-family: system-ui, sans-serif;
    }
    img {
      display: block;
      width: 100vw;
      height: 100vh;
      object-fit: contain;
      background: #000;
    }
    .info {
      position: fixed;
      left: 10px;
      bottom: 10px;
      padding: 6px 8px;
      border-radius: 4px;
      background: rgba(0, 0, 0, 0.55);
      font-size: 0.78rem;
      color: #bbb;
    }
    a { color: #9fe870; }
  </style>
</head>
<body>
  <img id="stream" alt="loading...">
  <div class="info">
    <a id="link" href="#">loading...</a>
  </div>
  <script>
    const streamUrl = 'http://' + window.location.hostname + ':81/stream';
    const img = document.getElementById('stream');
    function connectStream() { img.src = streamUrl + '?t=' + Date.now(); }
    img.onerror = () => setTimeout(connectStream, 250);
    connectStream();
    const link = document.getElementById('link');
    link.href  = streamUrl;
    link.textContent = streamUrl;
  </script>
</body>
</html>
)html";

// ── Globals ───────────────────────────────────────────────────────────────────
static httpd_handle_t cameraHttpd = NULL;
static httpd_handle_t streamHttpd = NULL;

static void logLine(const char *line) {
    Serial.println(line);
    Serial0.println(line);
}

static void logPrintf(const char *format, ...) {
    char buffer[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logLine(buffer);
}

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

// ── HTTP camera server ────────────────────────────────────────────────────────
static esp_err_t indexHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t jpgHandler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char len[16];
    snprintf(len, sizeof(len), "%u", static_cast<unsigned int>(fb->len));
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Length", len);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char *>(fb->buf), fb->len);
    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t streamHandler(httpd_req_t *req) {
    static const char *streamType = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
    static const char *streamBoundary = "\r\n--" PART_BOUNDARY "\r\n";
    static const char *streamPart = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

    esp_err_t res = httpd_resp_set_type(req, streamType);
    if (res != ESP_OK)
        return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    char partBuf[64];
    uint32_t statsStartMs = millis();
    uint32_t statsFrames = 0;
    uint32_t statsBytes = 0;
    uint32_t maxCaptureMs = 0;
    uint32_t maxSendMs = 0;

    while (true) {
        uint32_t captureStartMs = millis();
        camera_fb_t *fb = esp_camera_fb_get();
        uint32_t captureMs = millis() - captureStartMs;
        if (!fb) {
            logLine("[Stream] Camera capture failed");
            return ESP_FAIL;
        }
        if (captureMs > maxCaptureMs)
            maxCaptureMs = captureMs;

        size_t partLen =
            snprintf(partBuf, sizeof(partBuf), streamPart, static_cast<unsigned int>(fb->len));
        uint32_t sendStartMs = millis();
        res = httpd_resp_send_chunk(req, streamBoundary, strlen(streamBoundary));
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, partBuf, partLen);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(fb->buf), fb->len);
        uint32_t sendMs = millis() - sendStartMs;
        if (sendMs > maxSendMs)
            maxSendMs = sendMs;

        size_t frameLen = fb->len;
        esp_camera_fb_return(fb);

        if (res != ESP_OK)
            break;

        statsFrames++;
        statsBytes += frameLen;

        uint32_t statsElapsedMs = millis() - statsStartMs;
        if (statsElapsedMs >= 5000) {
            float fps = statsFrames * 1000.0f / statsElapsedMs;
            uint32_t avgKb = statsFrames > 0 ? (statsBytes / statsFrames) / 1024 : 0;
            logPrintf("[Stream] fps=%.1f avg=%luKB maxCapture=%lums maxSend=%lums", fps,
                      static_cast<unsigned long>(avgKb), static_cast<unsigned long>(maxCaptureMs),
                      static_cast<unsigned long>(maxSendMs));
            statsStartMs = millis();
            statsFrames = 0;
            statsBytes = 0;
            maxCaptureMs = 0;
            maxSendMs = 0;
        }
    }

    return res;
}

static void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;

    httpd_uri_t indexUri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = indexHandler,
        .user_ctx = NULL,
    };
    httpd_uri_t jpgUri = {
        .uri = "/jpg",
        .method = HTTP_GET,
        .handler = jpgHandler,
        .user_ctx = NULL,
    };
    httpd_uri_t streamUri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = streamHandler,
        .user_ctx = NULL,
    };

    if (httpd_start(&cameraHttpd, &config) == ESP_OK) {
        httpd_register_uri_handler(cameraHttpd, &indexUri);
        httpd_register_uri_handler(cameraHttpd, &jpgUri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&streamHttpd, &config) == ESP_OK) {
        httpd_register_uri_handler(streamHttpd, &streamUri);
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial0.begin(115200);
    Serial.setDebugOutput(true);
    delay(1500);

    logLine("\n=== ESP32-S3 Camera Stream ===");
    logLine("[BOOT] App started");
    logPrintf("[BOOT] millis=%lu", static_cast<unsigned long>(millis()));

    if (!initCamera()) {
        logLine("[ERROR] Camera init failed — check model/pins in config.h");
        while (true)
            delay(1000);
    }
    logLine("[OK] Camera initialized");
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_vflip(sensor, 1);
        sensor->set_hmirror(sensor, 1);
        logLine("[OK] Camera image rotated 180 degrees");
    }
    if (psramFound())
        logLine("[OK] PSRAM found");
    else
        logLine("[WARN] No PSRAM — quality/fps limited");

    WiFi.mode(WIFI_OFF);
    delay(100);
#if WIFI_AP_MODE
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    bool apOk = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL, 0, 1);
    if (!apOk) {
        logLine("[ERROR] WiFi AP start failed");
        while (true)
            delay(1000);
    }
    logPrintf("[WiFi] AP: %s pass=%s", WIFI_AP_SSID, WIFI_AP_PASS);
    logPrintf("[WiFi] AP IP: %s", WiFi.softAPIP().toString().c_str());
#else
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    logLine("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        Serial0.print(".");
    }
    logPrintf("[WiFi] Connected: %s", WiFi.localIP().toString().c_str());
#endif

    startCameraServer();

#if WIFI_AP_MODE
    logPrintf("[HTTP]  http://%s", WiFi.softAPIP().toString().c_str());
    logPrintf("[Stream] http://%s:81/stream", WiFi.softAPIP().toString().c_str());
#else
    logPrintf("[HTTP]  http://%s", WiFi.localIP().toString().c_str());
    logPrintf("[Stream] http://%s:81/stream", WiFi.localIP().toString().c_str());
#endif
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    static uint32_t lastStatusMs = 0;
    if (millis() - lastStatusMs > 30000) {
        lastStatusMs = millis();
#if WIFI_AP_MODE
        logPrintf("[HTTP]  http://%s", WiFi.softAPIP().toString().c_str());
        logPrintf("[Stream] http://%s:81/stream stations=%d", WiFi.softAPIP().toString().c_str(),
                  WiFi.softAPgetStationNum());
#else
        if (WiFi.status() == WL_CONNECTED) {
            logPrintf("[HTTP]  http://%s", WiFi.localIP().toString().c_str());
            logPrintf("[Stream] http://%s:81/stream", WiFi.localIP().toString().c_str());
        } else {
            logPrintf("[WiFi] Disconnected, status=%d", WiFi.status());
        }
#endif
    }

    delay(10);
}
