#include "PWMPlayer.h"
#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"

static const char* TAG = "PWMPlayer";

PWMPlayer::PWMPlayer(gpio_num_t pin, bool inverted) {
  this->pin = pin;
  this->isInverted = inverted;
}

void PWMPlayer::play(String filename) {
  if (!filename || filename == "") {
    return;
  }
  isPatternPlaying = true;
  segmentIndex = 0;
  segmentCount = 0;
  nextSegmentAt = 0;

  fs::FS &fs = SD_MMC;
  File patternFile = fs.open(filename);
  if (!patternFile) {
    ESP_LOGW(TAG, "File not found");
    return;
  }
  patternFile.read((uint8_t *)&segmentCount, sizeof segmentCount);
  free(pwmPattern);
  pwmPattern = NULL;
  pwmPattern = (struct patternSegment *)malloc(sizeof(patternSegment) * segmentCount);
  patternFile.read((uint8_t *)pwmPattern, sizeof(patternSegment) * segmentCount);
  patternFile.close();
  gpio_reset_pin(pin);
  //gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  ledcAttach(pin, freq, resolution);
  ESP_LOGI(TAG, "Playing: %s, Segments: %d, Segment size: %d", filename, segmentCount, sizeof(segmentCount));
  this->tick();
}

void PWMPlayer::tick() {
  if (isPatternPlaying && nextSegmentAt < millis()) {
    if (segmentIndex >= segmentCount) {
      this->stop();
      return;
    }
    uint8_t startIntensity = pwmPattern[segmentIndex].startIntensity;
    uint8_t endIntensity = pwmPattern[segmentIndex].endIntensity;
    if (isInverted) {
      startIntensity = 255 - startIntensity;
      endIntensity = 255 - endIntensity;
    }
    ledcFade(pin, startIntensity, endIntensity, pwmPattern[segmentIndex].duration);
    ESP_LOGI(TAG, "Pattern start: %d, End: %d, Duration: %d", pwmPattern[segmentIndex].startIntensity, pwmPattern[segmentIndex].endIntensity, pwmPattern[segmentIndex].duration);
    nextSegmentAt = millis() + pwmPattern[segmentIndex].duration;
    segmentIndex++;
  }
}

boolean PWMPlayer::isPlaying() {
  if (isPatternPlaying){tick();}
  return isPatternPlaying;
}

void PWMPlayer::stop() {
  free(pwmPattern);
  pwmPattern = NULL;
  ledcWrite(pin, 0);
  ledcDetach(pin);
  gpio_reset_pin(pin);
  pinMode(pin, OUTPUT);
  if (isInverted) {
    digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, LOW);
  }
  ESP_LOGI(TAG, "Stopping play");
  isPatternPlaying = false;
}
