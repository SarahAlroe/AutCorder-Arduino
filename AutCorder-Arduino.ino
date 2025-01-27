#include "FS.h"
#include "SD_MMC.h"
#include "driver/uart.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <AsyncTelegram2.h>
#include "time.h"
#include "esp_sntp.h"
#include "soc/soc.h"
#include "driver/gpio.h"
#include "Buzzer.h"
#include "PWMPlayer.h"
#include "Dictaphone.h"
#include "Camera.h"
#include <Update.h>
#include "Discord.h"

#define STR_WIFI "wifi"
#define STR_SSID "ssid"
#define STR_PASSWORD "pass"
#define STR_WIFI_TIMEOUT "wifi_timout"
#define STR_TELEGRAM "telegram"
#define STR_CHAT_ID "chat_id"
#define STR_DISCORD "discord"
#define STR_TOKEN "token"
#define STR_ID "id"
#define STR_NAME "name"
#define STR_MELODY "melody"
#define STR_VIBRATION "vibration"
#define STR_LED "led"
#define STR_PHOTO_END "pho_end"
#define STR_AUDIO_END "aud_end"
#define STR_UPLOAD_PROGRESS "upl_prog"
#define STR_UPLOAD_END "upl_end"
#define STR_UPLOAD "upload"
#define STR_HOUR "hour"
#define STR_MINUTE "min"
#define STR_UPLOAD_IMM_DELAY "immediate_delay_min"
#define STR_UPLOAD_RETRY_DELAY "retry_delay_min"
#define STR_UPLOAD_ALLOWED_FROM "allowed_from"
#define STR_UPLOAD_ALLOWED_UNTIL "allowed_to"
#define STR_UPLOAD_KEEP_FILE_AFTER "keep_file_after_upl"
#define STR_PHOTO_ROTATION "picture_rotation"
#define STR_AUDIO_LIMITER "audio_limiter_factor"
#define STR_STATISTICS "collect_statistics"
#define STR_MARKER_SWITCH "marker_switch"
#define STR_ENABLED "enabled"
#define STR_PIN "pin"
#define STR_ON_TEXT "on_text"
#define STR_OFF_TEXT "off_text"

#define FILENAME_CONFIG "/config.json"
#define FILENAME_STATISTICS "/stats.bin"
#define FILENAME_SYNCDAY "/.sync"

#define uS_TO_S_FACTOR 1000000ULL
#define S_TO_MIN_FACTOR 60ULL

// Pin mapping
#define PIN_BAT_LEVEL 1
#define PIN_SHUTTER GPIO_NUM_2
#define PIN_LED GPIO_NUM_3
#define PIN_FEEDBACK GPIO_NUM_44
#define PIN_UTIL_IO GPIO_NUM_8
#define PIN_PERIPH_PWR 14
#define PIN_POWERED 21
#define PIN_CAM_RESET GPIO_NUM_43

#define PIN_MIC_CLK GPIO_NUM_47
#define PIN_MIC_DATA GPIO_NUM_48

#define PIN_USB_DP 20
#define PIN_USB_DM 19

#define PIN_SD_CS 9
#define PIN_SPI_PICO 38
#define PIN_SPI_POCI 40
#define PIN_SPI_CLK 39

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)

#define DEBUG true

static const char* TAG = "Main";

#define WATCHDOG_TIMEOUT 60000UL //Things have gone wrong if we have looped for a minute.

// How long after boot to determine picture or audio recording
const uint16_t BOOT_DELAY = 300;

// Time settings
const char *ntpServer1 = "pool.ntp.org";
const char *time_zone = "CET-1CEST,M3.5.0,M10.5.0/3"; // List: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

WiFiMulti wifiMulti;
JsonDocument configuration;
WiFiClientSecure client;
AsyncTelegram2 myBot(client);
Discord discord;
Buzzer buzzer(PIN_UTIL_IO);
PWMPlayer vibration(PIN_FEEDBACK, false);
PWMPlayer led(PIN_LED, true);
Dictaphone dictaphone(PIN_MIC_CLK,PIN_MIC_DATA);
Camera camera;

bool timeSet = false;
bool transmissionFinished = false;
bool messageDelivered;
bool justCapturedAudio = false;
bool justCapturedPicture = false;
esp_sleep_wakeup_cause_t wakeup_reason;

void setup() {

  if(DEBUG){
    delay(200);
  }

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: ESP_LOGI(TAG,"Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: ESP_LOGI(TAG,"Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: ESP_LOGI(TAG,"Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: ESP_LOGI(TAG,"Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: ESP_LOGI(TAG,"Wakeup caused by ULP program"); break;
    default:  ESP_LOGI(TAG, "Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  initializeGPIO();
  ESP_LOGI(TAG, "Wake pin state: %d", digitalRead(PIN_SHUTTER));

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1 || wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {  // We woke up from external input
    if (esp_sleep_get_ext1_wakeup_status() == BUTTON_PIN_BITMASK(PIN_SHUTTER)){ // Woke up from button, that means take a picture or audio
      if (!camera.begin()) {
        camera.stop();
        delay(200);
        goToSleep();  // If failed to initialize, sleep
      }
      ESP_LOGI(TAG, "Camera began");

      if (!dictaphone.begin()) {
        delay(200);
        goToSleep();  // If failed to initialize, sleep
      }
    
      // After boot we may take a picture or begin recording.
      // Recording begins if the button has been held for more than BOOT_DELAY.
      // Picture may begin any time before if the button has been let go.
      while (millis() < BOOT_DELAY && isCaptureButtonHeld()) {
        // While waiting, start recording out the i2s buffer to discard odd offset first 100ms
        dictaphone.warmup();
        camera.warmup();
      }

      digitalWrite(PIN_LED, HIGH);             // Turn LED on TODO, actually animate
      if (isCaptureButtonHeld()) {
        dictaphone.beginRecording();
        while (isCaptureButtonHeld()) {
          dictaphone.continueRecording();
        }
        justCapturedAudio = true;
      } else {
        if (!camera.takePicture()) {
          camera.stop();
          goToSleep();
        }
        justCapturedPicture = true;
      }
    digitalWrite(PIN_LED, LOW);  // Turn LED off
    }
  }

  // Set up SD card for reading, sleeping if anything fails.
  initializeSDCard();
  initializeConfiguration();

  // Reset time zone as it is lost between deep sleep cycles
  setenv("TZ",time_zone,1);
  tzset();

  if (isNewlyBooted()) {
    ESP_LOGI(TAG,"Newly booted");
    updateFromFS(SD_MMC);   // Check if we have an update file available on SD card, and then perform update. Otherwise move on.
    ensureFilestructure();  // Create directories if not exist
  }

  if (justCapturedAudio) {
    // Indicate finished recording
    buzzer.play(String(configuration[STR_MELODY][STR_AUDIO_END]));
    vibration.play(String(configuration[STR_VIBRATION][STR_AUDIO_END]));
    led.play(configuration[STR_LED][STR_AUDIO_END]);
    while (isFeedbackPlaying()) {
      delay(5);
    }
    dictaphone.processRecording(configuration[STR_AUDIO_LIMITER]);
    dictaphone.saveRecording(SD_MMC, getNamePrefix());
    updateStatistics(0,1,dictaphone.getSecondsRecorded());
  }

  if (justCapturedPicture) {
    // Indicate completion
    buzzer.play(String(configuration[STR_MELODY][STR_PHOTO_END]));
    vibration.play(String(configuration[STR_VIBRATION][STR_PHOTO_END]));
    led.play(configuration[STR_LED][STR_PHOTO_END]);
    while (isFeedbackPlaying()) {
      delay(5);
    }

    camera.savePicture(SD_MMC, getNamePrefix(),configuration[STR_PHOTO_ROTATION]);
    camera.stop();
    updateStatistics(1,0,0);
  }

  // We woke up from a cold boot or from timer, ensure our time is good
  if (isNewlyBooted() || (hasPicturesToUpload() && withinUploadTime()) || isNTPOld()) {
    connectWifi();
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    if (timeinfo.tm_year < 123 || isNTPOld()) {  // tm_year is years since 1900, 123 would be 2023. Or if last sync was a day ago. 
      ESP_LOGI(TAG,"Setting time");
      sntp_set_time_sync_notification_cb(timeavailable);
      configTzTime(time_zone, ntpServer1);
      uint32_t whileTimer = 0;
      while (!timeSet) {
        delay(200);
        whileTimer += 200;
        ESP_LOGV(TAG,".");
        if (whileTimer>WATCHDOG_TIMEOUT){
          ESP_LOGW(TAG,"Setting time hit watchdog timeout");
          goToSleep(); // Failed to get time within two minutes? Sleep and try again later.
        }
      }
    }
    printLocalTime();
  }
  if (hasPicturesToUpload() && withinUploadTime()) {
    connectTelegramAndUploadAvailable();
  }
  goToSleep();
}

void timeavailable(struct timeval *t) {
  timeSet = true;
  ESP_LOGI(TAG,"Got time adjustment from NTP!");
  printLocalTime();
  
  fs::FS &fs = SD_MMC;
  File syncFile = fs.open(FILENAME_SYNCDAY, FILE_WRITE, true);
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  uint8_t currentDay = timeinfo.tm_mday;
  syncFile.write(currentDay);
  syncFile.close();
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    ESP_LOGE(TAG,"Failed to obtain time");
    return;
  }
  ESP_LOGI(TAG,"%d-%d-%d %d:%d:%d", timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec );
}

// Internal RTC has some drift, so resync every day
bool isNTPOld(){
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  if (timeinfo.tm_year < 123){
    return true;
  }
  fs::FS &fs = SD_MMC;
  // If file not exists, we have not synced at all
  if (!fs.exists(FILENAME_SYNCDAY)) {
    return true;
  }
  File syncFile = fs.open(FILENAME_SYNCDAY);
  uint8_t lastSyncDay = syncFile.read();
  syncFile.close();
  uint8_t currentDay = timeinfo.tm_mday;
  return currentDay != lastSyncDay;
}

String getNamePrefix(){
  if (!configuration[STR_MARKER_SWITCH][STR_ENABLED]){
    return "";
  }
  pinMode(configuration[STR_MARKER_SWITCH][STR_PIN], INPUT_PULLUP);
  if (digitalRead(configuration[STR_MARKER_SWITCH][STR_PIN])){
    return configuration[STR_MARKER_SWITCH][STR_ON_TEXT];
  }
  return configuration[STR_MARKER_SWITCH][STR_OFF_TEXT];
}

// Ensure all are called for the tick
bool isFeedbackPlaying(){
  bool isVibrationPlaying = vibration.isPlaying();
  bool isBuzzerPlaying = buzzer.isPlaying();
  bool isLedPlaying = led.isPlaying();
  return isVibrationPlaying || isBuzzerPlaying || isLedPlaying;
}

void initializeSDCard() {
  if (!SD_MMC.setPins(PIN_SPI_CLK, PIN_SPI_PICO, PIN_SPI_POCI)) {
    ESP_LOGE(TAG,"Failed to set SD pins!");
    goToSleep();
  }
  if (!SD_MMC.begin("/sdcard", true)) {
    ESP_LOGE(TAG,"SD Card Mount Failed");
    goToSleep();
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    ESP_LOGE(TAG,"No SD Card attached");
    goToSleep();
  }
}

void initializeGPIO() {
  // Ready GPIO for use
  uart_driver_delete(UART_NUM_0);
  gpio_reset_pin(PIN_FEEDBACK);
  gpio_reset_pin(PIN_CAM_RESET);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_PERIPH_PWR, OUTPUT);
  //pinMode(PIN_FEEDBACK, OUTPUT);
  pinMode(PIN_POWERED, INPUT);
  pinMode(PIN_SHUTTER, INPUT);

  digitalWrite(PIN_PERIPH_PWR, LOW);
  analogReadResolution(12);
}

void initializeConfiguration() {
  configuration[STR_WIFI][0][STR_SSID] = "WIFI SSID Name";
  configuration[STR_WIFI][0][STR_PASSWORD] = "12345678";

  configuration[STR_TELEGRAM][STR_ENABLED] = false;
  configuration[STR_TELEGRAM][STR_CHAT_ID][0] = 0;
  configuration[STR_TELEGRAM][STR_TOKEN] = "";

  configuration[STR_DISCORD][STR_ENABLED] = false;
  configuration[STR_DISCORD][STR_ID] = "1234567890123456789";
  configuration[STR_DISCORD][STR_TOKEN] = "";
  configuration[STR_DISCORD][STR_NAME] = "CareRecorder";

  configuration[STR_WIFI_TIMEOUT] = 4;
  configuration[STR_PHOTO_ROTATION] = 3;
  configuration[STR_AUDIO_LIMITER] = 0.75;
  configuration[STR_STATISTICS] = true;

  configuration[STR_UPLOAD][STR_UPLOAD_IMM_DELAY] = 1;
  configuration[STR_UPLOAD][STR_UPLOAD_RETRY_DELAY] = 20;
  configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_HOUR] = 6;
  configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_MINUTE] = 30;
  configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_UNTIL][STR_HOUR] = 23;
  configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_UNTIL][STR_MINUTE] = 30;
  configuration[STR_UPLOAD][STR_UPLOAD_KEEP_FILE_AFTER] = false;

  configuration[STR_MELODY][STR_PHOTO_END] = "";
  configuration[STR_MELODY][STR_AUDIO_END] = "";
  configuration[STR_MELODY][STR_UPLOAD_PROGRESS] = "";
  configuration[STR_MELODY][STR_UPLOAD_END] = "";

  configuration[STR_VIBRATION][STR_PHOTO_END] = "";
  configuration[STR_VIBRATION][STR_AUDIO_END] = "";
  configuration[STR_VIBRATION][STR_UPLOAD_PROGRESS] = "";
  configuration[STR_VIBRATION][STR_UPLOAD_END] = "";

  configuration[STR_LED][STR_PHOTO_END] = "";
  configuration[STR_LED][STR_AUDIO_END] = "";
  configuration[STR_LED][STR_UPLOAD_PROGRESS] = "";
  configuration[STR_LED][STR_UPLOAD_END] = "";

  configuration[STR_MARKER_SWITCH][STR_ENABLED] = false;
  configuration[STR_MARKER_SWITCH][STR_PIN] = 8;
  configuration[STR_MARKER_SWITCH][STR_ON_TEXT] = "👍 ";
  configuration[STR_MARKER_SWITCH][STR_OFF_TEXT] = "👎 ";

  fs::FS &fs = SD_MMC;

  // Create config if not exist
  if (fs.exists(FILENAME_CONFIG)) {
    File configFile = fs.open(FILENAME_CONFIG);
    DeserializationError error = deserializeJson(configuration, configFile);
    if (error) {
      ESP_LOGE(TAG,"Failed to read file, using default configuration");
    }
  }else{
    File configFile = fs.open(FILENAME_CONFIG, FILE_WRITE, true);
    serializeJson(configuration, configFile);
    configFile.close();
  }
}

struct captureStatistics{
   uint32_t picturesTaken;
   uint32_t audioSegmentsRecorded;
   uint32_t audioSecondsRecorded;
};

void updateStatistics(uint8_t picturesTaken, uint8_t clipsRecorded , uint16_t secondsRecorded){
  // If we don't collect statistics, do nothing.
  if (!configuration[STR_STATISTICS]){
    return;
  }
  captureStatistics stats;
  fs::FS &fs = SD_MMC;
  // Read or create new stats file
  if (fs.exists(FILENAME_STATISTICS)) {
    File statsFile = fs.open(FILENAME_STATISTICS);
    statsFile.read((uint8_t *) &stats, sizeof(captureStatistics));
    statsFile.close();
  }else{
    stats.picturesTaken = 0;
    stats.audioSegmentsRecorded = 0;
    stats.audioSecondsRecorded = 0;
  }
  // Add up new data
  stats.picturesTaken += picturesTaken;
  stats.audioSegmentsRecorded += clipsRecorded;
  stats.audioSecondsRecorded += secondsRecorded;
  // Commit new stats to file
  File statsFile = fs.open(FILENAME_STATISTICS, FILE_WRITE, true);
  statsFile.write((uint8_t *)&stats, sizeof(captureStatistics));
  statsFile.close();
}

bool isCaptureButtonHeld() {
  return (digitalRead(PIN_SHUTTER) == HIGH);
}

bool withinUploadTime() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int currentTime = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  int fromHour = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_HOUR];
  int fromMinute = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_MINUTE];
  int fromTime = fromHour * 60 + fromMinute;
  int toHour = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_UNTIL][STR_HOUR];
  int toMinute = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_UNTIL][STR_MINUTE];
  int toTime = toHour * 60 + toMinute;

  if (fromTime < toTime) {  // If end later than beginning, later than start AND earlier than beginning
    return currentTime > fromTime && currentTime < toTime;
  } else {  // Else if start later than beginning, later than start OR earlier than beginning
    return currentTime > fromTime || currentTime < toTime;
  }
}

// Returns time until next upload span. 0 if we can upload right now.
uint16_t minutesUntilUploadTime() {
  if (withinUploadTime()){
    return 0;
  }
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int currentTime = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int fromHour = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_HOUR];
  int fromMinute = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_MINUTE];
  int fromTime = fromHour * 60 + fromMinute;

  if (fromTime>currentTime){
    return fromTime-currentTime; // Time between now and next upload span.
  }
  return 24*60 - currentTime + fromTime; // Whatever is left of the day plus time until next upload span.
}

bool hasPicturesToUpload() {
  fs::FS &fs = SD_MMC;
  File newDir = fs.open("/new");
  if (!newDir || !newDir.isDirectory()) {
    ESP_LOGE(TAG,"Error scanning new dir");
    return false;
  }
  File file = newDir.openNextFile();
  if (file) {
    file.close();
    ESP_LOGI(TAG,"Has new files to upload");
    return true;
  }
  ESP_LOGI(TAG,"No files to upload");
  return false;
}

void messageSent(bool sent) {
  if (sent) {
    ESP_LOGI(TAG,"Last message was delivered");
  } else {
    ESP_LOGW(TAG,"Last message was NOT delivered");
    messageDelivered = false;
  }
  transmissionFinished = true;
}

void getTelegramMessages() {
  TBMessage msg;
  if (myBot.getNewMessage(msg)) {
    MessageType msgType = msg.messageType;

    // Received a text message
    if (msgType == MessageText) {
      ESP_LOGI(TAG,"Text message received: %s", msg.text);
    }
  }
}

const char* token =  "xxxxxxxxxxx:xxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

void connectTelegramAndUploadAvailable() {
  if (configuration[STR_TELEGRAM][STR_ENABLED]){
    ESP_LOGI(TAG,"Initializing Telegram");
    myBot.setUpdateTime(500);  // Minimum time between telegram bot calls
    myBot.setTelegramToken(configuration[STR_TELEGRAM][STR_TOKEN]);
    myBot.addSentCallback(messageSent, 3000);
    myBot.begin();
  }
  if (configuration[STR_DISCORD][STR_ENABLED]){
    discord.init(configuration[STR_DISCORD][STR_ID], configuration[STR_DISCORD][STR_TOKEN], configuration[STR_DISCORD][STR_NAME]);
  }
  
  if (hasPicturesToUpload()) {
    ESP_LOGI(TAG,"Has pictures to upload");
    fs::FS &fs = SD_MMC;
    File newDir = fs.open("/new");
    if (!newDir || !newDir.isDirectory()) {
      ESP_LOGE(TAG,"Error scanning new dir");
      return;
    }
    File file = newDir.openNextFile();
    while (file) {
    if (file.isDirectory()) {
      continue;
    } else {
      if (configuration[STR_TELEGRAM][STR_ENABLED]) {
        JsonArray chatIds = configuration[STR_TELEGRAM][STR_CHAT_ID].as<JsonArray>();
        if (chatIds[0] == 0) {
          ESP_LOGE(TAG,"Bad Telegram chat id");
          continue;
        }
        for (JsonVariant chatIdJson : chatIds) {
          int64_t chatId = chatIdJson.as<int64_t>();
          File fileToSend = fs.open(file.path());
          ESP_LOGI(TAG,"Uploading: %s of size: %d to: %d", fileToSend.name(),fileToSend.size(),chatId);
          // Check what we are sending, and send it
        
          if (String(fileToSend.name()).endsWith(".jpg")) {
            myBot.sendPhoto(chatId, fileToSend, fileToSend.size(), fileToSend.name(), fileToSend.name());
          } else {
            myBot.sendDocument(chatId, fileToSend, fileToSend.size(), AsyncTelegram2::DocumentType::VOICE, fileToSend.name(), fileToSend.name());
          }
        
          // Wait for upload to finish
          uint32_t whileTimer = 0;
          messageDelivered = true;
          while (!transmissionFinished) {
            getTelegramMessages();
            delay(50);
            whileTimer += 50;
            playProgressFeedback();
            ESP_LOGV(TAG,".");
            if (whileTimer > WATCHDOG_TIMEOUT){
              ESP_LOGW(TAG,"Upload hit watchdog timeout");
              goToSleep(); // Failed to upload within two minutes? Sleep and try again later.
            }
          }
          transmissionFinished = false;
        }
      }
      if (configuration[STR_DISCORD][STR_ENABLED]){
        if (!discord.sendFile(file)){
          ESP_LOGE(TAG,"Failed to send to Discord");
          messageDelivered = false;
        }
      }
      if(messageDelivered){
        // Move the sent file to old pictures or delete depending on config
        if (configuration[STR_UPLOAD][STR_UPLOAD_KEEP_FILE_AFTER]) {
          fs.rename("/new/" + String(file.name()), "/old/" + String(file.name()));
        } else {
          fs.remove("/new/" + String(file.name()));
        }
      }
    }
    file = newDir.openNextFile();
  }
  }
  buzzer.play(configuration[STR_MELODY][STR_UPLOAD_END]);
  vibration.play(configuration[STR_VIBRATION][STR_UPLOAD_END]);
  led.play(configuration[STR_LED][STR_UPLOAD_END]);
}

void playProgressFeedback(){
  if (!buzzer.isPlaying()) {
    buzzer.play(configuration[STR_MELODY][STR_UPLOAD_PROGRESS]);
  }
  if (!vibration.isPlaying()) {
    vibration.play(configuration[STR_VIBRATION][STR_UPLOAD_PROGRESS]);
  }
  if (!led.isPlaying()) {
    led.play(configuration[STR_LED][STR_UPLOAD_PROGRESS]);
  }
}

void connectWifi() {
  client.setCACert(telegram_cert);

  // Connect to wifi
  wifiMulti.setStrictMode(true);
  JsonArray wifiAPs = configuration[STR_WIFI];
  for (JsonVariant wifiAP : wifiAPs) {
    wifiMulti.addAP(wifiAP[STR_SSID], wifiAP[STR_PASSWORD]);
  }

  ESP_LOGI(TAG,"Connecting Wifi...");
  uint32_t connectTime = 0;
  if (wifiMulti.run((configuration[STR_WIFI_TIMEOUT] | 4) * 1000) != WL_CONNECTED){
    goToSleep();
  }
  ESP_LOGI(TAG,"WiFi connected. IP address: %s", WiFi.localIP().toString());
}

void ensureFilestructure() {
  fs::FS &fs = SD_MMC;
  if (!fs.exists("/new")) {
    fs.mkdir("/new");
  }
  if (!fs.exists("/old")) {
    fs.mkdir("/old");
  }
}

boolean isNewlyBooted() {
  return wakeup_reason != ESP_SLEEP_WAKEUP_EXT0 && wakeup_reason != ESP_SLEEP_WAKEUP_EXT1 && wakeup_reason != ESP_SLEEP_WAKEUP_TIMER;
}

void goToSleep() {
  ESP_LOGI(TAG,"Going to sleep");
  // Finish indications
  while (isFeedbackPlaying()) {
    delay(5);
  }

  digitalWrite(PIN_LED, LOW);
  delay(200);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Enable wakeup if we just captured something or there are still old captures to retry uploading.
  // Use different time for each.
  // But if we are outside upload time segment, wait until we get there.

  if (justCapturedPicture || justCapturedAudio) {
    uint64_t minutesToSleep = max((uint64_t)minutesUntilUploadTime(), (uint64_t)configuration[STR_UPLOAD][STR_UPLOAD_IMM_DELAY]);
    minutesToSleep = min(minutesToSleep, 120ULL); // Max 2 hours, known issue https://forum.arduino.cc/t/esp32-deep-sleep-is-24h-sleep-effectively-possible/677173/2
    esp_sleep_enable_timer_wakeup(minutesToSleep * S_TO_MIN_FACTOR * uS_TO_S_FACTOR);
  } else if (hasPicturesToUpload() || isNTPOld()) {
    uint64_t minutesToSleep = max((uint64_t)minutesUntilUploadTime(), (uint64_t)configuration[STR_UPLOAD][STR_UPLOAD_RETRY_DELAY]);
    minutesToSleep = min(minutesToSleep, 120ULL); // Max 2 hours, known issue https://forum.arduino.cc/t/esp32-deep-sleep-is-24h-sleep-effectively-possible/677173/2
    esp_sleep_enable_timer_wakeup(minutesToSleep * S_TO_MIN_FACTOR * uS_TO_S_FACTOR);
  }
  ESP_LOGI(TAG,"Wifi off + wake set");

  esp_sleep_enable_ext1_wakeup_io(BUTTON_PIN_BITMASK(PIN_SHUTTER)|BUTTON_PIN_BITMASK(PIN_POWERED), ESP_EXT1_WAKEUP_ANY_HIGH);
  //esp_sleep_enable_ext0_wakeup(PIN_SHUTTER, HIGH);
  esp_deep_sleep_disable_rom_logging(); // suppress boot messages -> https://esp32.com/viewtopic.php?t=30954
  ESP_LOGI(TAG,"See you.");
  delay(50);
  esp_deep_sleep_start();
}

// Borrowed from SD_Update exampple
// perform the actual update from a given stream
void performUpdate(Stream &updateSource, size_t updateSize) {
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      ESP_LOGI(TAG,"Written: %d successfully", written);
    } else {
      ESP_LOGE(TAG,"Written only : %d / %d. Retry?", written, updateSize);
    }
    if (Update.end()) {
      ESP_LOGI(TAG,"OTA done!");
      if (Update.isFinished()) {
        ESP_LOGI(TAG,"Update successfully completed. Rebooting.");
      } else {
        ESP_LOGE(TAG,"Update not finished? Something went wrong!");
      }
    } else {
      ESP_LOGE(TAG,"Error Occurred. Error #: %d",Update.getError());
    }

  } else {
    ESP_LOGE(TAG,"Not enough space to begin OTA");
  }
}

// check given FS for valid update.bin and perform update if available
void updateFromFS(fs::FS &fs) {
  File updateBin = fs.open("/update.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      ESP_LOGE(TAG,"Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      ESP_LOGI(TAG,"Try to start update");
      performUpdate(updateBin, updateSize);
    } else {
      ESP_LOGE(TAG,"Error, file is empty");
    }

    updateBin.close();

    // when finished remove the binary from sd card to indicate end of the process
    fs.remove("/update.bin");
  } else {
    ESP_LOGE(TAG,"Could not load update.bin from sd root");
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(10);
}
