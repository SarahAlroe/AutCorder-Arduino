#include "Buzzer.h"
#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
#include <ArduinoJson.h>

static const char* TAG = "Buzzer";

Buzzer::Buzzer(gpio_num_t pin) {
  this->pin = pin;
  // LEDc attached channel defaults to 0
}

void Buzzer::play(String filename) {
  if (!filename || filename == "") {
    return;
  }
  ESP_LOGI(TAG,"Playing: %s", filename);
  isMelodyPlaying = true;
  noteIndex = 0;
  noteCount = 0;
  nextToneAt = 0;

  fs::FS &fs = SD_MMC;
  File melodyFile = fs.open(filename);
  melodyFile.read((byte *) &noteCount, sizeof(noteCount));
  free(melody); melody = NULL;
  melody = (struct note *)malloc(sizeof(melody) * noteCount);
  melodyFile.read((byte *) melody, sizeof(note) * noteCount);
  melodyFile.close();
  ledcAttach(pin, freq, resolution);
  this->tick();
}

void Buzzer::tick() {
  if (isMelodyPlaying && nextToneAt < millis()) {
    if (noteIndex >= noteCount) {
      this->stop();
      return;
    }
    ledcWriteTone(pin, melody[noteIndex].frequency);
    ESP_LOGI(TAG,"Tone: %d Hz, Duration: %d ms", melody[noteIndex].frequency, melody[noteIndex].duration);
    nextToneAt = millis() + melody[noteIndex].duration;
    noteIndex++;
  }
}

boolean Buzzer::isPlaying() {
  if (isMelodyPlaying) {tick();}
  return isMelodyPlaying;
}

void Buzzer::stop() {
  ledcWriteTone(pin, 0);
  ledcDetach(pin);
  gpio_reset_pin(pin);
  free(melody); melody = NULL;
  isMelodyPlaying = false;
}

void Buzzer::writeTest() {
  note note1;
  note note2;
  note1.frequency = 1047;
  note1.duration = 100;
  note2.frequency = 1397;
  note2.duration = 400;
  fs::FS &fs = SD_MMC;
  File testFile = fs.open("/test.mel", FILE_WRITE, true);
  noteCount = 2;
  testFile.write((byte *)&noteCount, sizeof(noteCount));
  testFile.write((byte *)&note1, sizeof(note1));
  testFile.write((byte *)&note2, sizeof(note2));
  testFile.close();
}
