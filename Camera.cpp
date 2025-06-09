#include "esp32-hal-gpio.h"
#include "Arduino.h"
#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include "Camera.h"
#include "time.h"
#include "driver/gpio.h"

static const char* TAG = "Camera";

Camera::Camera() {
}

sensor_t *s;

esp_err_t Camera::begin() {
  pinMode(PIN_CAM_RESET, OUTPUT);
  digitalWrite(PIN_CAM_RESET, LOW);
  delay(2);
  // Init the camera
  /*gpio_reset_pin(PIN_CAM_Y2);
  gpio_reset_pin(PIN_CAM_Y3);
  gpio_reset_pin(PIN_CAM_Y4);
  gpio_reset_pin(PIN_CAM_Y5);
  gpio_reset_pin(PIN_CAM_Y6);
  gpio_reset_pin(PIN_CAM_Y7);
  gpio_reset_pin(PIN_CAM_Y8);
  gpio_reset_pin(PIN_CAM_Y9);
  gpio_reset_pin(PIN_CAM_XVCLK);
  gpio_reset_pin(PIN_CAM_PCLK);
  gpio_reset_pin(PIN_CAM_VSYNC);
  gpio_reset_pin(PIN_CAM_HREF);
  gpio_reset_pin(PIN_CAM_SIO_D);
  gpio_reset_pin(PIN_CAM_SIO_C);
  gpio_reset_pin(PIN_CAM_RESET);*/

  camera_config_t config;
  config.pin_d0 = PIN_CAM_Y2;
  config.pin_d1 = PIN_CAM_Y3;
  config.pin_d2 = PIN_CAM_Y4;
  config.pin_d3 = PIN_CAM_Y5;
  config.pin_d4 = PIN_CAM_Y6;
  config.pin_d5 = PIN_CAM_Y7;
  config.pin_d6 = PIN_CAM_Y8;
  config.pin_d7 = PIN_CAM_Y9;
  config.pin_xclk = PIN_CAM_XVCLK;
  config.pin_pclk = PIN_CAM_PCLK;
  config.pin_vsync = PIN_CAM_VSYNC;
  config.pin_href = PIN_CAM_HREF;
  config.pin_sccb_sda = PIN_CAM_SIO_D;
  config.pin_sccb_scl = PIN_CAM_SIO_C;
  config.pin_reset = PIN_CAM_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  /*# if HW_VERSION<20
    config.pin_pwdn = PIN_CAM_PWDN;
  # endif*/

  config.frame_size = FRAMESIZE_QVGA; // Start with lowest quality to speed up warmup

  if (psramFound()) {
    ESP_LOGI(TAG,"PSRAM Found");
    config.frame_size = FRAMESIZE_SXGA;  // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    ESP_LOGI(TAG,"No PSRAM Found");
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err == ESP_OK) {
    ESP_LOGI(TAG,"Initialized camera");
  }else{
    ESP_LOGE(TAG,"Camera init failed with error 0x%x", err);
    return err;
  }

  s = esp_camera_sensor_get();
  s->set_brightness(s, 0);  // -2 to 2
  s->set_contrast(s, 0);    // -2 to 2
  s->set_saturation(s, 0);  // -2 to 2
  //s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, 1);                    // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);                    // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);                     // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, 1);               // 0 = disable , 1 = enable
  s->set_aec2(s, 1);                        // 0 = disable , 1 = enable
  s->set_ae_level(s, 0);                    // -2 to 2
  //s->set_aec_value(s, 1200);                // 0 to 1200
  s->set_gain_ctrl(s, 1);                   // 0 = disable , 1 = enable
  //s->set_agc_gain(s, 30);                   // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)6);  // 0 to 6
  s->set_bpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                     // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                        // 0 = disable , 1 = enable
  //s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
  //s->set_vflip(s, 0);          // 0 = disable , 1 = enable
  s->set_dcw(s, 0);  // 0 = disable , 1 = enable
  //s->set_colorbar(s, 0);       // 0 = disable , 1 = enable

  fb = NULL;
  return ESP_OK;
}

void Camera::warmup() {
  // Warm up the camera. Appears to take out some of the noise.
  // OLD did this 8 times, 10ms delay
  fb = esp_camera_fb_get();
  if (fb){
    esp_camera_fb_return(fb);
  }
  warmupCount ++;
}

bool Camera::takePicture() {
  // Take Picture with Camera
  ESP_LOGI(TAG,"Taking picture");
  while(warmupCount < 8){
    warmup();
  }

  //s->set_framesize(s, FRAMESIZE_UXGA);
  //delay(2);
  fb = esp_camera_fb_get();
  if (!fb) {
    ESP_LOGE(TAG,"Camera capture failed");
    return false;
  }
  return true;
}
  
bool Camera::savePicture(fs::FS &fs, String filePrefix, uint8_t rotation) {
  // Save the image
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  // Path where new picture will be saved in SD Card
  //String path = "/new/" + filePrefix + String(timeinfo.tm_year - 100) + "-" + String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday) + "-" + String(timeinfo.tm_hour) + "-" + String(timeinfo.tm_min) + "-" + String(timeinfo.tm_sec) + ".jpg";
  String hour = (timeinfo.tm_hour<10?"0":"") + String(timeinfo.tm_hour);
  String minute = (timeinfo.tm_min<10?"0":"") + String(timeinfo.tm_min);
  String second = (timeinfo.tm_sec<10?"0":"") + String(timeinfo.tm_sec);
  String path = "/new/" + filePrefix + hour + "-" + minute + "-" + second + ".jpg";
  ESP_LOGI(TAG,"Picture file name: %s", path.c_str());

  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file) {
    ESP_LOGE(TAG,"Failed to open file in writing mode");
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
  ESP_LOGI(TAG,"Saved file to path: %s", path.c_str());
  file.close();
  return true;
}

void Camera::stop(){
  //Deinit camera
  esp_camera_fb_return(fb);
  esp_camera_return_all();
  esp_camera_deinit();

  # if HW_VERSION<20
    gpio_pulldown_dis(PIN_CAM_PWDN);
    gpio_pullup_en(PIN_CAM_PWDN);
  # endif
}