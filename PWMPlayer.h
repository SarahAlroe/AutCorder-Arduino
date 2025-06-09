#pragma once

#include "driver/gpio.h"
#include "Arduino.h"

struct patternSegment{
   uint16_t startIntensity;
   uint16_t endIntensity;
   uint16_t duration;
};

class PWMPlayer {
  public:
    PWMPlayer(gpio_num_t pin, bool inverted);

    void play(String filename);
    void tick();
    void stop();
    bool isPlaying();

  private:
    gpio_num_t pin;
    bool isInverted = false;
    bool isPatternPlaying = false;
    bool isLooping = false;
    const int freq = 5000;
    const int resolution = 8;
    
    uint32_t segmentIndex = 0;
    uint32_t segmentCount = 0;
    uint32_t nextSegmentAt = 0;
    struct patternSegment* pwmPattern = NULL;
};
