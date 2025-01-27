#pragma once

#include "Arduino.h"
#include "esp_camera.h"

#define PIN_CAM_RESET GPIO_NUM_43
#define PIN_CAM_SIO_D GPIO_NUM_17
#define PIN_CAM_SIO_C GPIO_NUM_41
#define PIN_CAM_HREF GPIO_NUM_18
#define PIN_CAM_VSYNC GPIO_NUM_42
#define PIN_CAM_XVCLK GPIO_NUM_11
#define PIN_CAM_PCLK GPIO_NUM_12
#define PIN_CAM_Y2 GPIO_NUM_6
#define PIN_CAM_Y3 GPIO_NUM_15
#define PIN_CAM_Y4 GPIO_NUM_16
#define PIN_CAM_Y5 GPIO_NUM_7
#define PIN_CAM_Y6 GPIO_NUM_5
#define PIN_CAM_Y7 GPIO_NUM_10
#define PIN_CAM_Y8 GPIO_NUM_4
#define PIN_CAM_Y9 GPIO_NUM_13

class Camera {
public:
  Camera();
  bool begin();        // Initialize I2S
  void warmup();       // Read and discard some pictures (should have short execution time)
  bool takePicture();  // Take a picture
  bool savePicture(fs::FS &fs, String filePrefix, uint8_t rotation);  // Save it out to an SD card
  void stop();

private:
  camera_fb_t *fb;
  uint16_t warmupCount = 0;
};
