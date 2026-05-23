#include <Arduino.h>
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

// ── Frame protocol ────────────────────────────────────────────────────────────
// [ 4 bytes magic ][ 4 bytes uint32 frame length ][ N bytes JPEG ]
static const uint8_t MAGIC[4] = {0xFF, 0xAA, 0xBB, 0xFF};

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
    cfg.frame_size    = FRAME_SIZE;
    cfg.jpeg_quality  = JPEG_QUALITY;
    cfg.fb_count      = psramFound() ? 2 : 1;
    cfg.grab_mode     = CAMERA_GRAB_LATEST;
    cfg.fb_location   = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    return esp_camera_init(&cfg) == ESP_OK;
}

// ── Camera task (FreeRTOS) ────────────────────────────────────────────────────
static void cameraTask(void* arg) {
#if TARGET_FPS > 0
    constexpr TickType_t FRAME_TICKS = pdMS_TO_TICKS(1000 / TARGET_FPS);
#endif
    while (true) {
#if TARGET_FPS > 0
        TickType_t start = xTaskGetTickCount();
#endif
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        if (Serial) {
            uint32_t len = fb->len;
            // xTaskCreate tasks are not WDT-monitored — safe to block here
            Serial.write(MAGIC, 4);
            Serial.write(reinterpret_cast<const uint8_t*>(&len), 4);
            Serial.write(fb->buf, fb->len);
        }

        esp_camera_fb_return(fb);

#if TARGET_FPS > 0
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed < FRAME_TICKS) vTaskDelay(FRAME_TICKS - elapsed);
#endif
    }
}

void setup() {
    Serial.begin(0);
    delay(800);

    if (!initCamera()) {
        pinMode(2, OUTPUT);
        while (true) { digitalWrite(2, !digitalRead(2)); delay(200); }
    }

    // Fix flipped image
    sensor_t* s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);

    xTaskCreate(cameraTask, "cam", 8192, NULL, 5, NULL);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
