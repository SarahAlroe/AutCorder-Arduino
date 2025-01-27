#include "Arduino.h"
#include <esp_log.h>
#include "ESP_I2S.h"
#include "wav_header.h"  //Wav header from ESP_I2S
#include "FS.h"
#include "time.h"
#include "Dictaphone.h"

Dictaphone::Dictaphone(gpio_num_t clkPin, gpio_num_t dataPin) {
}

bool Dictaphone::begin() {
  // Setup i2s bus
  i2s.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
  if (!i2s.begin(I2S_MODE_PDM_RX, RECORD_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
      ESP_LOGE(TAG,"Failed to initialize I2S bus!");
    return false;
  }
  ESP_LOGI(TAG,"I2S initialized on CLK %d, DATA %d", PIN_MIC_CLK, PIN_MIC_DATA);
  return true;
}

void Dictaphone::warmup(){
  for(int i=0; i<150; i++){ // Read out a number of samples to make more or less equivalent with camera
    i2s.read();
  }
}


void Dictaphone::beginRecording(){
  beginRecording(SAVED_SAMPLE_INTERVAL,SAVED_BIT_DEPTH_DIVIDE);
}

void Dictaphone::beginRecording(int16_t sampleInterval, bool halveBitDepth){
  SAVED_SAMPLE_INTERVAL = sampleInterval; // Downsampling multiplier. Keep every nth sample. 1=Keep all, 2=Keep every other.
  SAVED_BIT_DEPTH_DIVIDE = halveBitDepth;
  free(wavBuffer); wavBuffer = NULL; // For safety free wavBuffer if not already done
  //Reserve large psram buffer (Always guaranteed to be clear, no memory management issues, slow rw fine as limited processing)
  wavBuffer = (uint8_t *)ps_malloc(RECORD_MAX_SIZE);
  recordingLength = 0;
}

void Dictaphone::continueRecording(){
  if (recordingLength < RECORD_MAX_SIZE) {
    recordingLength += i2s.readBytes((char *)(wavBuffer + recordingLength), MILLISECOND_LENGTH * 100);  // Record 1/10 second, offset from where we recorded previously
  }
}

void Dictaphone::processRecording(float limiterFactor){
  // Process the recording
  recordingLength = recordingLength - (DISCARD_START + DISCARD_END);  // Chop off the last bit to avoid the click (+100 ms for the first bit)
  // Get min and max of samples to offset and multiply without clipping.
  int16_t *clipSamples = (int16_t *)(wavBuffer + DISCARD_START); // Chop off the first 100ms
  int8_t *clipSamples8 = (int8_t *)(wavBuffer + DISCARD_START);
  int32_t minSample = INT16_MAX; //32 Bit to avoid accidental over or underflows.
  int32_t maxSample = INT16_MIN;
  int64_t averageSample = 0;
  for (int i = 0; i < recordingLength / 2; i += SAVED_SAMPLE_INTERVAL) { // Iterate over sample set (in 16 bit), potentially skipping every other sample
    minSample = min(minSample, (int32_t)clipSamples[i]);
    maxSample = max(maxSample, (int32_t)clipSamples[i]);
    averageSample += clipSamples[i];
  }
  ESP_LOGI(TAG,"Actual sample minimum: %d", minSample);
  ESP_LOGI(TAG,"Actual sample maximum: %d", maxSample);
  averageSample = averageSample / ((int64_t)recordingLength / (2 * SAVED_SAMPLE_INTERVAL)); // *2 because 16 bits
  // Offset minimum and maximum samples by the average sample value to avoid clipping when offsetting and amplifying samples later
  minSample = minSample - averageSample;
  maxSample = maxSample - averageSample;
  int16_t sampleMult = INT16_MAX / max(abs(minSample), abs(maxSample));  // As offset we divide target value by current value to get multiplier.
  sampleMult = max(sampleMult - 1, 1); // Subtract 1, but always keept at 1+ to avoid overflows.

  // Report statistics
  ESP_LOGI(TAG,"Sample average: %d", averageSample);
  ESP_LOGI(TAG,"Subtracted sample minimum: %d", minSample);
  ESP_LOGI(TAG,"Subtracted sample maximum: %d", maxSample);
  ESP_LOGI(TAG,"Sample multiplier: %d", sampleMult);

  if (limiterFactor != 0.0) {
    for (int i = 0; i < recordingLength / 2; i += SAVED_SAMPLE_INTERVAL) {
      float sampleIntermediary = (float)((clipSamples[i] - averageSample) * sampleMult) / (float)INT16_MAX;             // We applpy sample offset and multiplier. Value between -1 and 1
      // sampleIntermediary = sampleIntermediary * (1 - limiterFactor * sqrt(abs(sampleIntermediary))) / (1 - limiterFactor);  // Apply compression
      sampleIntermediary = sampleIntermediary / (sqrt(abs(sampleIntermediary)));  // Apply compression
      if (SAVED_BIT_DEPTH_DIVIDE){
        clipSamples8[i/SAVED_SAMPLE_INTERVAL] = (int8_t)(sampleIntermediary * (float)INT8_MAX); // Return to int domain. Save in sequence of bytes + handle skips
      }else{
        clipSamples[i/SAVED_SAMPLE_INTERVAL] = (int16_t)(sampleIntermediary * (float)INT16_MAX);// Return to int domain. Save in sequence of 16bit, no matter skips
      }
    }
  } else {
    for (int i = 0; i < recordingLength / 2; i += SAVED_SAMPLE_INTERVAL) { // Iterate over sample set in 16 bit, potentially every other sample
      int16_t normalizedSample = (clipSamples[i] - averageSample) * sampleMult; 
      if (SAVED_BIT_DEPTH_DIVIDE){
        clipSamples8[i/SAVED_SAMPLE_INTERVAL] = normalizedSample / 256; // Save in sequence of bytes + handle skips
      }else{
        clipSamples[i/SAVED_SAMPLE_INTERVAL] = normalizedSample; // Save in sequence no matter skips
      }
    }
  }
}

bool Dictaphone::saveRecording(fs::FS &fs, String filePrefix){
  // Save the file
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  //String path = "/new/" + filePrefix + String(timeinfo.tm_year - 100) + "-" + String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday) + "-" + String(timeinfo.tm_hour) + "-" + String(timeinfo.tm_min) + "-" + String(timeinfo.tm_sec) + ".wav";
  String hour = (timeinfo.tm_hour < 10 ? "0" : "") + String(timeinfo.tm_hour);
  String minute = (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min);
  String second = (timeinfo.tm_sec < 10 ? "0" : "") + String(timeinfo.tm_sec);
  String path = "/new/" + filePrefix + hour + "-" + minute + "-" + second + ".wav";
  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file) {
    ESP_LOGE(TAG,"Failed to open file for writing!");
    free(wavBuffer); wavBuffer = NULL;
    return false;
  }
  ESP_LOGI(TAG,"Opened file for writing");

  int savedLength = recordingLength / SAVED_SAMPLE_INTERVAL;
  if (SAVED_BIT_DEPTH_DIVIDE){
    savedLength = savedLength / 2;
  }
  ESP_LOGI(TAG,"Will be saving %d bytes of audio data", savedLength);
  
  pcm_wav_header_t wavHeader;
  if (SAVED_BIT_DEPTH_DIVIDE){
    wavHeader = PCM_WAV_HEADER_DEFAULT(recordingLength, I2S_DATA_BIT_WIDTH_8BIT, RECORD_SAMPLE_RATE / SAVED_SAMPLE_INTERVAL, I2S_SLOT_MODE_MONO);
  }else{
    wavHeader = PCM_WAV_HEADER_DEFAULT(recordingLength, I2S_DATA_BIT_WIDTH_16BIT, RECORD_SAMPLE_RATE / SAVED_SAMPLE_INTERVAL, I2S_SLOT_MODE_MONO); 
  }

  bool failedWrite = file.write((uint8_t *)&wavHeader, PCM_WAV_HEADER_SIZE) != PCM_WAV_HEADER_SIZE; // Write the audio header to the file
  failedWrite &= file.write(wavBuffer + DISCARD_START, savedLength) != savedLength; // Write the audio data to the file
  if (failedWrite) {
    ESP_LOGE(TAG,"Failed to write audio data to file!");
    free(wavBuffer); wavBuffer = NULL;
    file.close();
    return false;
  }

  ESP_LOGI(TAG,"Saved %d bytes header and %d bytes data", PCM_WAV_HEADER_SIZE, savedLength);

  // Close the file
  file.close();
  free(wavBuffer); wavBuffer = NULL;
  return true;
}

uint16_t Dictaphone::getSecondsRecorded(){
  ESP_LOGI(TAG,"Recorded %d seconds of audio.", ((recordingLength)/MILLISECOND_LENGTH)/1000);
  return ((recordingLength)/MILLISECOND_LENGTH)/1000;
}
