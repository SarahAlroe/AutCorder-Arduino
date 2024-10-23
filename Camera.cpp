#include "Arduino.h"
#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include "driver/gpio.h"
#include "Camera.h"
#include "time.h"

extern bool SERIAL_DEBUG;

Camera::Camera() {
}

bool Camera::begin() {
  // Init the camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;  // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (SERIAL_DEBUG) { Serial.println("Init'ed camera"); }
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    if (SERIAL_DEBUG) { Serial.printf("Camera init failed with error 0x%x", err); }
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_brightness(s, 0);  // -2 to 2
  s->set_contrast(s, 0);    // -2 to 2
  s->set_saturation(s, 0);  // -2 to 2
  //s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, 1);                    // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);                    // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);                     // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, 1);               // 0 = disable , 1 = enable
  s->set_aec2(s, 1);                        // 0 = disable , 1 = enable
  s->set_ae_level(s, 2);                    // -2 to 2
  s->set_aec_value(s, 1200);                // 0 to 1200
  s->set_gain_ctrl(s, 1);                   // 0 = disable , 1 = enable
  s->set_agc_gain(s, 30);                   // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)6);  // 0 to 6
  s->set_bpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                     // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                        // 0 = disable , 1 = enable
  //s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
  //s->set_vflip(s, 0);          // 0 = disable , 1 = enable
  s->set_dcw(s, 0);  // 0 = disable , 1 = enable
  //s->set_colorbar(s, 0);       // 0 = disable , 1 = enable

  if (SERIAL_DEBUG) { Serial.println("Taking picture"); }

  fb = NULL;
  return true;
}

void Camera::warmup() {
  // Warm up the camera. Appears to take out some of the noise.
  // OLD did this 8 times, 10ms delay
  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);
  warmupCount ++;
}

bool Camera::takePicture() {
  // Take Picture with Camera
  while(warmupCount < 8){
    warmup();
  }
  fb = esp_camera_fb_get();
  if (!fb) {
    if (SERIAL_DEBUG) { Serial.println("Camera capture failed"); }
    return false;
  }
  return true;
}
  
bool Camera::savePicture(String filePrefix, uint8_t rotation) {
  // Save the image
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  // Path where new picture will be saved in SD Card
  //String path = "/new/" + filePrefix + String(timeinfo.tm_year - 100) + "-" + String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday) + "-" + String(timeinfo.tm_hour) + "-" + String(timeinfo.tm_min) + "-" + String(timeinfo.tm_sec) + ".jpg";
  String hour = (timeinfo.tm_hour<10?"0":"") + String(timeinfo.tm_hour);
  String minute = (timeinfo.tm_min<10?"0":"") + String(timeinfo.tm_min);
  String second = (timeinfo.tm_sec<10?"0":"") + String(timeinfo.tm_sec);
  String path = "/new/" + filePrefix + hour + "-" + minute + "-" + second + ".jpg";
  if (SERIAL_DEBUG) { Serial.printf("Picture file name: %s\n", path.c_str()); }

  fs::FS &fs = SD_MMC;
  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file) {
    if (SERIAL_DEBUG) { Serial.println("Failed to open file in writing mode"); }
    return false;
  }
  const byte orientationFlag[] = {1,8,3,6};
  byte jpegHeader[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46,
    0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0xe1, 0x00, 0x62,
    0x45, 0x78, 0x69, 0x66, 0x00, 0x00, 0x4d, 0x4d,
    0x00, 0x2a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x05,
    0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x06, 0x00, 0x00, 0x01, 0x1a, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x4a,
    0x01, 0x1b, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x52, 0x01, 0x28, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00,
    0x02, 0x13, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
  };
  jpegHeader[49] = orientationFlag[rotation];
  file.write(jpegHeader, 0x78);           // Jpeg header with orientation property set //TODO extract to confi
  file.write(fb->buf + 0x14, fb->len - 0x14);  // payload (image), payload length
  if (SERIAL_DEBUG) { Serial.printf("Saved file to path: %s\n", path.c_str()); }
  file.close();
  return true;
}

void Camera::stop(){
  //Deinit camera
  esp_camera_fb_return(fb);
  esp_camera_return_all();
  esp_camera_deinit();

  gpio_pulldown_dis(PWDN_GPIO_NUM);
  gpio_pullup_en(PWDN_GPIO_NUM);
}