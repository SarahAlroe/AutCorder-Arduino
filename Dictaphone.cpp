#include "Arduino.h"
#include "ESP_I2S.h"
#include "wav_header.h"  //Wav header from ESP_I2S
#include "FS.h"
#include "SD_MMC.h"
#include "time.h"
#include "Dictaphone.h"

extern bool SERIAL_DEBUG;

Dictaphone::Dictaphone() {
}

bool Dictaphone::begin() {
  // Setup i2s bus
  i2s.setPinsPdmRx(MIC_CLK, MIC_DATA);
  if (!i2s.begin(I2S_MODE_PDM_RX, RECORD_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    if (SERIAL_DEBUG) {
      Serial.println("Failed to initialize I2S bus!");
    }
    return false;
  }
  return true;
}

void Dictaphone::warmup(){
  for(int i=0; i<160; i++){ // Read out a number of samples to make more or less equivalent with camera
    i2s.read();
  }
}


void Dictaphone::beginRecording(){
  //Reserve large psram buffer (Always guaranteed to be clear, no memory management issues, slow rw fine as limited processing)
  wavBuffer = (uint8_t *)ps_malloc(RECORD_MAX_SIZE);
  
  // While button pressed and max buffer not reached
  recordingLength = PCM_WAV_HEADER_SIZE;  //Offset by the wav header that we will write later when actual size known
  millisecondLength = sizeof(char) * ((RECORD_SAMPLE_RATE / 1000 * (I2S_DATA_BIT_WIDTH_16BIT / 8)) * I2S_SLOT_MODE_MONO);
}

void Dictaphone::continueRecording(){
  if (recordingLength < RECORD_MAX_SIZE) {
    recordingLength += i2s.readBytes((char *)(wavBuffer + recordingLength), millisecondLength * 100);  // Record 1/10 second, offset from where we recorded previously
  }
}

void Dictaphone::processRecording(float limiterFactor){
  // Process the recording
  recordingLength = recordingLength - (millisecondLength * 100);  // Chop off the last bit to avoid the click
  // Get min and max of samples to offset and multiply without clipping.
  int16_t *clipSamples = (int16_t *)wavBuffer;
  int16_t minSample = INT16_MAX;
  int16_t maxSample = INT16_MIN;
  int32_t averageSample = 0;
  for (int i = PCM_WAV_HEADER_SIZE / 2; i < recordingLength / 2; i++) {  //Start after header (which contains random junk at this point)
    minSample = min(minSample, clipSamples[i]);
    maxSample = max(maxSample, clipSamples[i]);
    averageSample += clipSamples[i];
  }
  averageSample = averageSample / (recordingLength / 2);
  minSample = minSample - averageSample;
  maxSample = maxSample - averageSample;
  int32_t sampleMult = INT16_MAX / max(abs(minSample), abs(maxSample));  // As offset we divide target value by current value to get multiplier.

  if (SERIAL_DEBUG) {
    // Report statistics
    Serial.print("Sample minimum: ");
    Serial.println(minSample);

    Serial.print("Sample maximum: ");
    Serial.println(maxSample);

    Serial.print("Sample average: ");
    Serial.println(averageSample);

    Serial.print("Multiplier: ");
    Serial.println(sampleMult);
  }

  if (limiterFactor != 0.0) {
    for (int i = PCM_WAV_HEADER_SIZE / 2; i < recordingLength / 2; i++) {
      float sampleIntermediary = (float)((clipSamples[i] - averageSample) * sampleMult) / (float)INT16_MAX;             // We applpy sample offset and multiplier. Value between -1 and 1
      sampleIntermediary = sampleIntermediary * (1 + limiterFactor - limiterFactor * sqrt(abs(sampleIntermediary)));  // Apply compression
      clipSamples[i] = (int16_t)(sampleIntermediary * (float)INT16_MAX);                                                // Return to int domain
    }
  } else {
    for (int i = PCM_WAV_HEADER_SIZE / 2; i < recordingLength / 2; i++) {
      clipSamples[i] = (clipSamples[i] - averageSample) * sampleMult;
    }
  }

  /* old processing
    // Offset and multiply audio samples (experimentally determined values. offset=0.04*2^15)
    int16_t *samples = (int16_t*) wavBuffer;
    for (int i = 0; i < recordingLength / 2; i++){
    samples[i] = (samples[i]-1310) << 6;
    }*/

  // Then Write Wav header with final size into beginning of our buffer
  const pcm_wav_header_t wavHeader = PCM_WAV_HEADER_DEFAULT(recordingLength, I2S_DATA_BIT_WIDTH_16BIT, RECORD_SAMPLE_RATE, I2S_SLOT_MODE_MONO);
  memcpy(wavBuffer, &wavHeader, PCM_WAV_HEADER_SIZE);
}

bool Dictaphone::saveRecording(String filePrefix){
  // Save the file
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  //String path = "/new/" + filePrefix + String(timeinfo.tm_year - 100) + "-" + String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday) + "-" + String(timeinfo.tm_hour) + "-" + String(timeinfo.tm_min) + "-" + String(timeinfo.tm_sec) + ".wav";
  String hour = (timeinfo.tm_hour < 10 ? "0" : "") + String(timeinfo.tm_hour);
  String minute = (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min);
  String second = (timeinfo.tm_sec < 10 ? "0" : "") + String(timeinfo.tm_sec);
  String path = "/new/" + filePrefix + hour + "-" + minute + "-" + second + ".wav";
  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    if (SERIAL_DEBUG) { Serial.println("Failed to open file for writing!"); }
    free(wavBuffer);
    return false;
  }

  // Write the audio data to the file
  if (file.write(wavBuffer, recordingLength) != recordingLength) {
    if (SERIAL_DEBUG) { Serial.println("Failed to write audio data to file!"); }
    free(wavBuffer);
    file.close();
    return false;
  }

  // Close the file
  file.close();
  free(wavBuffer);
  return true;
}

uint16_t Dictaphone::getSecondsRecorded(){
  return ((recordingLength-PCM_WAV_HEADER_SIZE)/millisecondLength)/1000;
}