#pragma once

#include "Arduino.h"
#include "esp_camera.h"

#define PWDN_GPIO_NUM GPIO_NUM_3
#define RESET_GPIO_NUM 21
#define XCLK_GPIO_NUM 11
#define SIOD_GPIO_NUM 17
#define SIOC_GPIO_NUM 41
#define Y9_GPIO_NUM 13
#define Y8_GPIO_NUM 4
#define Y7_GPIO_NUM 10
#define Y6_GPIO_NUM 5
#define Y5_GPIO_NUM 7
#define Y4_GPIO_NUM 16
#define Y3_GPIO_NUM 15
#define Y2_GPIO_NUM 6
#define VSYNC_GPIO_NUM 42
#define HREF_GPIO_NUM 18
#define PCLK_GPIO_NUM 12

class Camera {
public:
  Camera();
  bool begin();        // Initialize I2S
  void warmup();       // Read and discard some pictures (should have short execution time)
  bool takePicture();  // Take a picture
  bool savePicture(String filePrefix, uint8_t rotation);  // Save it out to an SD card
  void stop();

private:
  camera_fb_t *fb;
  uint16_t warmupCount = 0;
};
