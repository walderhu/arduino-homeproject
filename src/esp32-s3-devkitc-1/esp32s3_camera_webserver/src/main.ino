#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "../config.h"

// ── Camera pins ───────────────────────────────────────────────────────────────
#if defined(CAMERA_MODEL_FREENOVE_S3) || defined(CAMERA_MODEL_ESP32S3_EYE)
  #define CAM_PIN_PWDN   -1
  #define CAM_PIN_RESET  -1
  #define CAM_PIN_XCLK   15
  #define CAM_PIN_SIOD    4
  #define CAM_PIN_SIOC    5
  #define CAM_PIN_Y9     16
  #define CAM_PIN_Y8     17
  #define CAM_PIN_Y7     18
  #define CAM_PIN_Y6     12
  #define CAM_PIN_Y5     10
  #define CAM_PIN_Y4      8
  #define CAM_PIN_Y3      9
  #define CAM_PIN_Y2     11
  #define CAM_PIN_VSYNC   6
  #define CAM_PIN_HREF    7
  #define CAM_PIN_PCLK   13
#elif defined(CAMERA_MODEL_CUSTOM)
  #define CAM_PIN_PWDN   -1
  #define CAM_PIN_RESET  -1
  #define CAM_PIN_XCLK   15
  #define CAM_PIN_SIOD    4
  #define CAM_PIN_SIOC    5
  #define CAM_PIN_Y9     16
  #define CAM_PIN_Y8     17
  #define CAM_PIN_Y7     18
  #define CAM_PIN_Y6     12
  #define CAM_PIN_Y5     10
  #define CAM_PIN_Y4      8
  #define CAM_PIN_Y3      9
  #define CAM_PIN_Y2     11
  #define CAM_PIN_VSYNC   6
  #define CAM_PIN_HREF    7
  #define CAM_PIN_PCLK   13
#else
  #error "No camera model in config.h"
#endif

// ── HTML UI ───────────────────────────────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Webcam</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{background:#0d0d0d;color:#e0e0e0;font-family:system-ui,sans-serif;display:flex;flex-direction:column;align-items:center;padding:16px;gap:14px}
    h1{font-size:1.2rem;color:#9fe870}
    #wrap{position:relative;width:100%;max-width:800px}
    img{width:100%;border-radius:8px;border:2px solid #2a2a2a;background:#111;display:block}
    #fps{position:absolute;top:8px;right:8px;background:rgba(0,0,0,.6);color:#9fe870;font-size:.75rem;padding:2px 6px;border-radius:4px}
    .panel{width:100%;max-width:800px;background:#1a1a1a;border-radius:8px;padding:14px;display:grid;grid-template-columns:1fr 1fr;gap:10px}
    .row{display:flex;flex-direction:column;gap:4px}
    .row.full{grid-column:1/-1}
    label{font-size:.75rem;color:#888;text-transform:uppercase;letter-spacing:.05em}
    select,input[type=range]{width:100%;background:#111;color:#e0e0e0;border:1px solid #333;border-radius:4px;padding:4px 6px;font-size:.85rem;cursor:pointer}
    input[type=range]{padding:0;accent-color:#9fe870}
    .toggles{display:flex;gap:8px;grid-column:1/-1}
    button{flex:1;padding:7px 0;border:1px solid #333;border-radius:6px;background:#222;color:#e0e0e0;font-size:.85rem;cursor:pointer;transition:background .15s}
    button.on{background:#9fe870;color:#111;border-color:#9fe870;font-weight:600}
    #snap{grid-column:1/-1;padding:8px;background:#2a2a2a;border-radius:6px;font-size:.85rem;cursor:pointer;border:1px solid #444;color:#e0e0e0;transition:background .15s}
    #snap:hover{background:#333}
    a{display:block;text-align:center;font-size:.75rem;color:#555;text-decoration:none;width:100%;max-width:800px}
    a:hover{color:#9fe870}
  </style>
</head>
<body>
  <h1>ESP32-S3 Webcam</h1>

  <div id="wrap">
    <img id="stream" alt="stream">
    <div id="fps">-- fps</div>
  </div>

  <div class="panel">
    <div class="row">
      <label>Разрешение</label>
      <select id="framesize">
        <option value="5">QVGA 320×240</option>
        <option value="8" selected>VGA 640×480</option>
        <option value="9">SVGA 800×600</option>
        <option value="10">XGA 1024×768</option>
        <option value="13">UXGA 1600×1200</option>
      </select>
    </div>
    <div class="row">
      <label>Качество JPEG: <span id="qval">12</span></label>
      <input type="range" id="quality" min="4" max="63" value="12">
    </div>
    <div class="row">
      <label>Яркость: <span id="bval">0</span></label>
      <input type="range" id="brightness" min="-2" max="2" value="0">
    </div>
    <div class="row">
      <label>Контраст: <span id="cval">0</span></label>
      <input type="range" id="contrast" min="-2" max="2" value="0">
    </div>
    <div class="toggles">
      <button id="hmirror" onclick="toggle(this,'hmirror')">Зеркало H</button>
      <button id="vflip"   onclick="toggle(this,'vflip')">Флип V</button>
      <button id="awb"     onclick="toggle(this,'awb')" class="on">AWB</button>
      <button id="agc"     onclick="toggle(this,'agc')" class="on">AGC</button>
    </div>
    <button id="snap" onclick="snapshot()">📸 Снимок</button>
  </div>

  <a id="streamlink" href="#">→ прямая ссылка на поток</a>

  <script>
    const ip  = location.hostname;
    const img = document.getElementById('stream');
    const url = `http://${ip}:81/stream`;

    document.getElementById('streamlink').href = url;
    document.getElementById('streamlink').textContent = '→ ' + url;
    img.src = url;

    // FPS counter
    let last = 0, frames = 0;
    img.addEventListener('load', () => { frames++; });
    setInterval(() => {
      const now = Date.now();
      const fps = frames / ((now - last) / 1000 || 1);
      document.getElementById('fps').textContent = fps.toFixed(1) + ' fps';
      frames = 0; last = now;
    }, 1000);

    function ctrl(name, val) {
      fetch(`/control?var=${name}&val=${val}`);
    }

    // Sliders
    const sliders = {
      quality:    'qval',
      brightness: 'bval',
      contrast:   'cval',
    };
    for (const [id, label] of Object.entries(sliders)) {
      const el = document.getElementById(id);
      el.addEventListener('input', () => {
        document.getElementById(label).textContent = el.value;
        ctrl(id, el.value);
      });
    }

    // Select
    document.getElementById('framesize').addEventListener('change', e => {
      ctrl('framesize', e.target.value);
    });

    // Toggles
    const toggleState = { hmirror: 0, vflip: 0, awb: 1, agc: 1 };
    function toggle(btn, name) {
      toggleState[name] ^= 1;
      btn.classList.toggle('on', !!toggleState[name]);
      ctrl(name, toggleState[name]);
    }

    // Snapshot
    function snapshot() {
      const a = document.createElement('a');
      a.href = `http://${ip}/capture`;
      a.download = `esp32cam_${Date.now()}.jpg`;
      a.click();
    }
  </script>
</body>
</html>
)html";

// ── Globals ───────────────────────────────────────────────────────────────────
static const char BOUNDARY[] = "framebound";
WebServer   server(80);
WiFiServer  streamServer(81);

// ── Camera init ───────────────────────────────────────────────────────────────
static bool initCamera() {
    camera_config_t cfg = {};
    cfg.ledc_channel  = LEDC_CHANNEL_0;
    cfg.ledc_timer    = LEDC_TIMER_0;
    cfg.pin_d0        = CAM_PIN_Y2;
    cfg.pin_d1        = CAM_PIN_Y3;
    cfg.pin_d2        = CAM_PIN_Y4;
    cfg.pin_d3        = CAM_PIN_Y5;
    cfg.pin_d4        = CAM_PIN_Y6;
    cfg.pin_d5        = CAM_PIN_Y7;
    cfg.pin_d6        = CAM_PIN_Y8;
    cfg.pin_d7        = CAM_PIN_Y9;
    cfg.pin_xclk      = CAM_PIN_XCLK;
    cfg.pin_pclk      = CAM_PIN_PCLK;
    cfg.pin_vsync     = CAM_PIN_VSYNC;
    cfg.pin_href      = CAM_PIN_HREF;
    cfg.pin_sccb_sda  = CAM_PIN_SIOD;
    cfg.pin_sccb_scl  = CAM_PIN_SIOC;
    cfg.pin_pwdn      = CAM_PIN_PWDN;
    cfg.pin_reset     = CAM_PIN_RESET;
    cfg.xclk_freq_hz  = 20000000;
    cfg.pixel_format  = PIXFORMAT_JPEG;
    cfg.frame_size    = DEFAULT_FRAMESIZE;
    cfg.jpeg_quality  = DEFAULT_QUALITY;
    cfg.fb_count      = psramFound() ? 2 : 1;
    cfg.grab_mode     = CAMERA_GRAB_LATEST;
    cfg.fb_location   = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    return esp_camera_init(&cfg) == ESP_OK;
}

// ── Route: / ─────────────────────────────────────────────────────────────────
static void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

// ── Route: /capture ───────────────────────────────────────────────────────────
static void handleCapture() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { server.send(500, "text/plain", "Camera error"); return; }
    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// ── Route: /control ───────────────────────────────────────────────────────────
static void handleControl() {
    String var = server.arg("var");
    int    val = server.arg("val").toInt();
    sensor_t* s = esp_camera_sensor_get();
    if (!s) { server.send(500, "text/plain", "No sensor"); return; }

    if      (var == "framesize")  s->set_framesize(s,  (framesize_t)val);
    else if (var == "quality")    s->set_quality(s,    val);
    else if (var == "brightness") s->set_brightness(s, val);
    else if (var == "contrast")   s->set_contrast(s,   val);
    else if (var == "hmirror")    s->set_hmirror(s,    val);
    else if (var == "vflip")      s->set_vflip(s,      val);
    else if (var == "awb")        s->set_whitebal(s,   val);
    else if (var == "agc")        s->set_gain_ctrl(s,  val);

    server.send(200, "text/plain", "OK");
}

// ── MJPEG stream task ─────────────────────────────────────────────────────────
static void streamTask(void* arg) {
    WiFiClient client = *reinterpret_cast<WiFiClient*>(arg);
    delete reinterpret_cast<WiFiClient*>(arg);

    // Consume HTTP request
    while (client.connected() && client.available()) {
        if (client.readStringUntil('\n') == "\r") break;
    }

    client.printf(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace;boundary=%s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",
        BOUNDARY
    );

    while (client.connected()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        client.printf(
            "--%s\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n\r\n",
            BOUNDARY, fb->len
        );
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
    Serial.println("\n=== ESP32-S3 Webcam ===");

    if (!initCamera()) {
        Serial.println("[ERROR] Camera init failed — check model/pins in config.h");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    Serial.println("[OK] Camera");
    Serial.printf("[%s] PSRAM\n", psramFound() ? "OK" : "WARN no");

    sensor_t* s = esp_camera_sensor_get();
    s->set_brightness(s, DEFAULT_BRIGHTNESS);
    s->set_contrast(s,   DEFAULT_CONTRAST);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n[WiFi] %s\n", WiFi.localIP().toString().c_str());

    server.on("/",        handleRoot);
    server.on("/capture", handleCapture);
    server.on("/control", handleControl);
    server.begin();
    streamServer.begin();

    Serial.printf("[WEB]    http://%s\n",           WiFi.localIP().toString().c_str());
    Serial.printf("[STREAM] http://%s:81/stream\n", WiFi.localIP().toString().c_str());
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    server.handleClient();
    WiFiClient c = streamServer.accept();
    if (c) {
        WiFiClient* ptr = new WiFiClient(c);
        xTaskCreate(streamTask, "stream", 8192, ptr, 1, NULL);
    }
}
