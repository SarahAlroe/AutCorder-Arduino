#pragma once

#include "Arduino.h"
#include "esp_camera.h"
#include "Pinout.h"

class Camera {
public:
  Camera();
  esp_err_t begin();        // Initialize I2S
  void warmup();       // Read and discard some pictures (should have short execution time)
  bool takePicture();  // Take a picture
  bool savePicture(fs::FS &fs, String filePrefix, uint8_t rotation);  // Save it out to an SD card
  void stop();

private:
  camera_fb_t *fb;
  uint16_t warmupCount = 0;
};
