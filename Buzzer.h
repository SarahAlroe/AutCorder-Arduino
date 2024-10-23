#pragma once

#include "driver/gpio.h"
#include "Arduino.h"

struct note{
   uint16_t frequency;
   uint16_t duration;
};

class Buzzer {
  public:
    Buzzer(gpio_num_t pin);

    void play(String filename);
    void tick();
    void stop();
    void writeTest();
    boolean isPlaying();

  private:
    gpio_num_t pin;
    uint32_t noteIndex = 0;
    uint32_t noteCount = 0;
    uint32_t nextToneAt = 0;
    
    const int freq = 5000;
    const int resolution = 8;
    struct note* melody = NULL;
    boolean isMelodyPlaying = false;
};
