#include "FS.h"
#include "SD_MMC.h"
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

#define CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7 1
#define CONFIG_ESP_BROWNOUT_DET 1

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

#define LED_GPIO_NUM 14
#define LED_GPIO_NUM GPIO_NUM_14
#define BUZZER_PIN GPIO_NUM_19
#define VIBRATION_PIN GPIO_NUM_20
#define SD_CS 9
#define SD_MOSI 38
#define SD_CLK 39
#define SD_MISO 40
#define WATCHDOG_TIMEOUT 60000UL //Things have gone wrong if we have looped for a minute.

gpio_num_t WAKE_GPIO_NUM = GPIO_NUM_19;

// USB DEBUG?
extern const bool SERIAL_DEBUG = false;

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
Buzzer buzzer(BUZZER_PIN);
PWMPlayer vibration(VIBRATION_PIN, false);
PWMPlayer led(LED_GPIO_NUM, true);
Dictaphone dictaphone;
Camera camera;

bool timeSet = false;
bool uploadFinished = false;
bool justCapturedAudio = false;
bool justCapturedPicture = false;
esp_sleep_wakeup_cause_t wakeup_reason;

void setup() {
  esp_log_level_set("*", ESP_LOG_ERROR); // set all components to ERROR level -> https://esp32.com/viewtopic.php?t=30954
  if (SERIAL_DEBUG){ WAKE_GPIO_NUM = GPIO_NUM_8; }
  wakeup_reason = esp_sleep_get_wakeup_cause();

  if (SERIAL_DEBUG) {
    // If we have serial, enable it and print boot info
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    switch (wakeup_reason) {
      case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
      case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
      case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
      case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
      case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;
      default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
    }
    Serial.print("Wake pin state: ");
    Serial.println(digitalRead(WAKE_GPIO_NUM));
    //gpio_dump_io_configuration(stdout, (1ULL << WAKE_GPIO_NUM) | (1ULL << PWDN_GPIO_NUM));
  }

  initializeGPIO();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {  // We woke up from the button being pressed, that means take a picture or audio
    if (!dictaphone.begin()) {
      goToSleep();  // If failed to initialize, sleep
    }
    if (!camera.begin()) {
      camera.stop();
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

    digitalWrite(LED_GPIO_NUM, LOW);             // Turn LED on TODO, actually animate
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
    digitalWrite(LED_GPIO_NUM, HIGH);  // Turn LED off
  }

  // Set up SD card for reading, sleeping if anything fails.
  initializeSDCard();
  initializeConfiguration();

  // Reset time zone as it is lost between deep sleep cycles
  setenv("TZ",time_zone,1);
  tzset();

  if (isNewlyBooted()) {
    if (SERIAL_DEBUG) { Serial.print("Newly booted"); }
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
    dictaphone.saveRecording(getNamePrefix());
    updateStatistics(0,1,dictaphone.getSecondsRecorded());
    goToSleep();
  }

  if (justCapturedPicture) {
    // Indicate completion
    buzzer.play(String(configuration[STR_MELODY][STR_PHOTO_END]));
    vibration.play(String(configuration[STR_VIBRATION][STR_PHOTO_END]));
    led.play(configuration[STR_LED][STR_PHOTO_END]);
    while (isFeedbackPlaying()) {
      delay(5);
    }

    camera.savePicture(getNamePrefix(),configuration[STR_PHOTO_ROTATION]);
    camera.stop();
    updateStatistics(1,0,0);
    goToSleep();
  }

  // We woke up from a cold boot or from timer, ensure our time is good
  if (isNewlyBooted() || (hasPicturesToUpload() && withinUploadTime()) || isNTPOld()) {
    connectWifi();
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    if (timeinfo.tm_year < 123 || isNTPOld()) {  // tm_year is years since 1900, 123 would be 2023. Or if last sync was a day ago. 
      if (SERIAL_DEBUG) { Serial.println("Setting time"); }
      sntp_set_time_sync_notification_cb(timeavailable);
      configTzTime(time_zone, ntpServer1);
      uint32_t whileTimer = 0;
      while (!timeSet) {
        delay(200);
        whileTimer += 200;
        if (SERIAL_DEBUG) {Serial.print("."); }
        if (whileTimer>WATCHDOG_TIMEOUT){
          goToSleep(); // Failed to get time within two minutes? Sleep and try again later.
        }
      }
    }
    if (SERIAL_DEBUG) { printLocalTime(); }
  }
  if (hasPicturesToUpload() && withinUploadTime()) {
    connectTelegramAndUploadAvailable();
  }
  goToSleep();
}


void timeavailable(struct timeval *t) {
  timeSet = true;
  
  if (SERIAL_DEBUG) {
    Serial.println("Got time adjustment from NTP!");
    printLocalTime();
  }
  
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
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
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
  if (!SD_MMC.setPins(SD_CLK, SD_MOSI, SD_MISO)) {
    if (SERIAL_DEBUG) { Serial.println("Failed to set SD pins!"); }
    goToSleep();
  }
  if (!SD_MMC.begin("/sdcard", true)) {
    if (SERIAL_DEBUG) { Serial.println("SD Card Mount Failed"); }
    goToSleep();
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    if (SERIAL_DEBUG) { Serial.println("No SD Card attached"); }
    goToSleep();
  }
}

void initializeGPIO() {
  // Ready GPIO for use
  gpio_deep_sleep_hold_dis();  // Has been held during sleep to enable pulling down
  gpio_hold_dis(PWDN_GPIO_NUM);

  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, HIGH);  // Start off (LED behind inverting transistor)
  if (!SERIAL_DEBUG) {               // Only enable vibration pin if we are not actually connecting over usb
    // Configure USB pins for GPIO. May not be necessary
    gpio_config_t usb_phy_conf = {
      .pin_bit_mask = (1ULL << GPIO_NUM_19) | (1ULL << GPIO_NUM_20),
      .mode = GPIO_MODE_INPUT_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&usb_phy_conf);
    gpio_hold_dis(VIBRATION_PIN);
    pinMode(VIBRATION_PIN, OUTPUT);
    digitalWrite(VIBRATION_PIN, LOW);  // Start off
  }

  pinMode(WAKE_GPIO_NUM, INPUT_PULLDOWN);
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
  configuration[STR_DISCORD][STR_NAME] = "AutCorder";

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
      if (SERIAL_DEBUG) { Serial.println(F("Failed to read file, using default configuration")); }
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
  return (digitalRead(WAKE_GPIO_NUM) == HIGH);
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
    if (SERIAL_DEBUG) { Serial.println("Error scanning new dir"); }
    return false;
  }
  File file = newDir.openNextFile();
  if (file) {
    return true;
  }
  return false;
}

void messageSent(bool sent) {
  uploadFinished = true;
  if (sent) {
    if (SERIAL_DEBUG) {Serial.println("Last message was delivered");}
  } else {
    if (SERIAL_DEBUG) {Serial.println("Last message was NOT delivered");}
  }
}

void getTelegramMessages() {
  TBMessage msg;
  if (myBot.getNewMessage(msg)) {
    MessageType msgType = msg.messageType;

    // Received a text message
    if (msgType == MessageText) {
      if (SERIAL_DEBUG) {
        String msgText = msg.text;
        Serial.print("Text message received: ");
        Serial.println(msgText);
      }
    }
  }
}

void connectTelegramAndUploadAvailable() {
  if (configuration[STR_TELEGRAM][STR_ENABLED]){
    myBot.setUpdateTime(500);  // Minimum time between telegram bot calls
    myBot.setTelegramToken(configuration[STR_TELEGRAM][STR_TOKEN]);
    myBot.addSentCallback(messageSent, 3000);
    myBot.begin();
  }
  if (configuration[STR_DISCORD][STR_ENABLED]){
    discord.init(configuration[STR_DISCORD][STR_ID], configuration[STR_DISCORD][STR_TOKEN], configuration[STR_DISCORD][STR_NAME]);
  }
  
  if (hasPicturesToUpload()) {

    fs::FS &fs = SD_MMC;
    File newDir = fs.open("/new");
    if (!newDir || !newDir.isDirectory()) {
      if (SERIAL_DEBUG) { Serial.println("Error scanning new dir"); }
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
        if (SERIAL_DEBUG) { Serial.println("Bad Telegram chat id"); }
        continue;
      }
      for (JsonVariant chatIdJson : chatIds) {
        int64_t chatId = chatIdJson.as<int64_t>();
        File fileToSend = fs.open(file.path());
        if (SERIAL_DEBUG) {
          Serial.print("Uploading: ");
          Serial.print(fileToSend.name());
          Serial.print("  of size: ");
          Serial.print(fileToSend.size());
          Serial.print("  to: ");
          Serial.println(chatId);
        }
        // Check what we are sending, and send it
      
        if (String(fileToSend.name()).endsWith(".jpg")) {
          myBot.sendPhoto(chatId, fileToSend, fileToSend.size(), fileToSend.name(), fileToSend.name());
        } else {
          myBot.sendDocument(chatId, fileToSend, fileToSend.size(), AsyncTelegram2::DocumentType::VOICE, fileToSend.name(), fileToSend.name());
        }
      
        // Wait for upload to finish
        uint32_t whileTimer = 0;
        while (!uploadFinished) {
          getTelegramMessages();
          delay(10);
          whileTimer += 10;
          playProgressFeedback();
          if (SERIAL_DEBUG) {
            delay(50);
            whileTimer += 50;
            Serial.print(".");
          }
          if (whileTimer > WATCHDOG_TIMEOUT){
            goToSleep(); // Failed to upload within two minutes? Sleep and try again later.
          }
        }
        uploadFinished = false;
      }
    }
    if (configuration[STR_DISCORD][STR_ENABLED]){
      if (!discord.sendFile(file)){
        if (SERIAL_DEBUG) { Serial.println("Failed to send to Discord. Going to sleep"); }
        goToSleep();
      }
    }
    // Move the sent file to old pictures or deletedepending on config
    if (configuration[STR_UPLOAD][STR_UPLOAD_KEEP_FILE_AFTER]) {
      fs.rename("/new/" + String(file.name()), "/old/" + String(file.name()));
    } else {
      fs.remove("/new/" + String(file.name()));
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

  if (SERIAL_DEBUG) { Serial.println("Connecting Wifi..."); }
  uint32_t connectTime = 0;
  //while (WiFi.status() != WL_CONNECTED) {
  if (wifiMulti.run((configuration[STR_WIFI_TIMEOUT] | 4) * 1000) != WL_CONNECTED){
    goToSleep();
  }

  if (SERIAL_DEBUG) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
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
  return wakeup_reason != ESP_SLEEP_WAKEUP_EXT0 && wakeup_reason != ESP_SLEEP_WAKEUP_TIMER;
}

void goToSleep() {
  if (SERIAL_DEBUG) { Serial.print("Going to sleep"); }
  // Finish indications
  while (isFeedbackPlaying()) {
    delay(5);
  }

  digitalWrite(LED_GPIO_NUM, HIGH);
  //digitalWrite(VIBRATION_PIN, LOW);
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
  if (SERIAL_DEBUG) { Serial.println("Wifi off + wake set"); }

  /*gpio_reset_pin(GPIO_NUM_0);
  gpio_reset_pin(GPIO_NUM_1);
  gpio_reset_pin(GPIO_NUM_2);
  gpio_reset_pin(GPIO_NUM_3);
  gpio_reset_pin(GPIO_NUM_4);
  gpio_reset_pin(GPIO_NUM_5);
  gpio_reset_pin(GPIO_NUM_6);
  gpio_reset_pin(GPIO_NUM_7);
  gpio_reset_pin(GPIO_NUM_8);
  gpio_reset_pin(GPIO_NUM_9);
  gpio_reset_pin(GPIO_NUM_10);
  gpio_reset_pin(GPIO_NUM_11);
  gpio_reset_pin(GPIO_NUM_12);
  gpio_reset_pin(GPIO_NUM_13);
  gpio_reset_pin(GPIO_NUM_14);
  gpio_reset_pin(GPIO_NUM_15);
  gpio_reset_pin(GPIO_NUM_16);
  gpio_reset_pin(GPIO_NUM_17);
  gpio_reset_pin(GPIO_NUM_18);
  //gpio_reset_pin(GPIO_NUM_19);
  //gpio_reset_pin(GPIO_NUM_20);
  gpio_reset_pin(GPIO_NUM_21);
  //gpio_reset_pin(GPIO_NUM_22);
  //gpio_reset_pin(GPIO_NUM_23);
  //gpio_reset_pin(GPIO_NUM_24);
  //gpio_reset_pin(GPIO_NUM_25);
  gpio_reset_pin(GPIO_NUM_26);
  gpio_reset_pin(GPIO_NUM_27);
  gpio_reset_pin(GPIO_NUM_28);
  gpio_reset_pin(GPIO_NUM_29);
  gpio_reset_pin(GPIO_NUM_30);
  gpio_reset_pin(GPIO_NUM_31);
  gpio_reset_pin(GPIO_NUM_32);
  gpio_reset_pin(GPIO_NUM_33);
  gpio_reset_pin(GPIO_NUM_34);
  gpio_reset_pin(GPIO_NUM_35);
  gpio_reset_pin(GPIO_NUM_36);
  gpio_reset_pin(GPIO_NUM_37);
  gpio_reset_pin(GPIO_NUM_38);
  gpio_reset_pin(GPIO_NUM_39);
  gpio_reset_pin(GPIO_NUM_40);
  gpio_reset_pin(GPIO_NUM_41);
  gpio_reset_pin(GPIO_NUM_42);
  gpio_reset_pin(GPIO_NUM_43);
  gpio_reset_pin(GPIO_NUM_44);
  gpio_reset_pin(GPIO_NUM_45);
  gpio_reset_pin(GPIO_NUM_46);
  gpio_reset_pin(GPIO_NUM_47);
  gpio_reset_pin(GPIO_NUM_48);*/

  /*gpio_reset_pin(PWDN_GPIO_NUM);
  rtc_gpio_set_direction(PWDN_GPIO_NUM, RTC_GPIO_MODE_INPUT_OUTPUT);
  rtc_gpio_pulldown_dis(PWDN_GPIO_NUM);
  rtc_gpio_pullup_en(PWDN_GPIO_NUM);

  gpio_reset_pin(WAKE_GPIO_NUM);
  rtc_gpio_set_direction(WAKE_GPIO_NUM, RTC_GPIO_MODE_INPUT_OUTPUT);
  rtc_gpio_pulldown_dis(WAKE_GPIO_NUM);
  rtc_gpio_pullup_en(WAKE_GPIO_NUM);

  gpio_reset_pin(VIBRATION_PIN);
  rtc_gpio_set_direction(VIBRATION_PIN, RTC_GPIO_MODE_INPUT_OUTPUT);
  rtc_gpio_pullup_dis(VIBRATION_PIN);
  rtc_gpio_pulldown_en(VIBRATION_PIN);*/

  /*gpio_reset_pin(PWDN_GPIO_NUM);
  gpio_set_direction(PWDN_GPIO_NUM, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_pull_mode(PWDN_GPIO_NUM, GPIO_PULLUP_ONLY);
  gpio_pulldown_dis(PWDN_GPIO_NUM);
  gpio_pullup_en(PWDN_GPIO_NUM);
  gpio_hold_en(PWDN_GPIO_NUM);

  gpio_reset_pin(WAKE_GPIO_NUM);
  gpio_set_direction(WAKE_GPIO_NUM, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_pull_mode(WAKE_GPIO_NUM, GPIO_PULLUP_ONLY);
  gpio_pulldown_dis(WAKE_GPIO_NUM);
  gpio_pullup_en(WAKE_GPIO_NUM);
  gpio_hold_en(WAKE_GPIO_NUM);*/

  /*gpio_set_direction(VIBRATION_PIN, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_pull_mode(VIBRATION_PIN, GPIO_PULLDOWN_ONLY);
  gpio_pullup_dis(VIBRATION_PIN);
  gpio_pulldown_en(VIBRATION_PIN);*/
  //gpio_hold_en(VIBRATION_PIN);
  //Serial.println("pins");

  // Set pin voltages and hold for sleep
  if (!SERIAL_DEBUG) {
    pinMode(VIBRATION_PIN, OUTPUT);
    digitalWrite(VIBRATION_PIN, LOW);  // Keep off for sleep
    gpio_hold_en(VIBRATION_PIN);
  }

  pinMode(PWDN_GPIO_NUM, OUTPUT);  // Stong pullup
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  gpio_hold_en(PWDN_GPIO_NUM);  // Will wake if not enabled

  //pinMode(WAKE_GPIO_NUM, OUTPUT);
  //digitalWrite(WAKE_GPIO_NUM, HIGH);
  //gpio_hold_en(WAKE_GPIO_NUM); // Will wake if not enabled
  gpio_deep_sleep_hold_en();  // Not tested, not supposed to work without

  if (SERIAL_DEBUG) { Serial.println("hold"); }

  esp_sleep_enable_ext0_wakeup(WAKE_GPIO_NUM, HIGH);  //We pull up the gpio and wait for it to be pulled down
  esp_deep_sleep_disable_rom_logging(); // suppress boot messages -> https://esp32.com/viewtopic.php?t=30954
  if (SERIAL_DEBUG) {
    //gpio_dump_io_configuration(stdout, (1ULL << WAKE_GPIO_NUM) | (1ULL << PWDN_GPIO_NUM));
    Serial.println(" now");
  }
  delay(50);
  esp_deep_sleep_start();
}

// Borrowed from SD_Update exampple
// perform the actual update from a given stream
void performUpdate(Stream &updateSource, size_t updateSize) {
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      if (SERIAL_DEBUG) {Serial.println("Written : " + String(written) + " successfully");}
    } else {
      if (SERIAL_DEBUG) {Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");}
    }
    if (Update.end()) {
      if (SERIAL_DEBUG) {Serial.println("OTA done!");}
      if (Update.isFinished()) {
        if (SERIAL_DEBUG) {Serial.println("Update successfully completed. Rebooting.");}
      } else {
        if (SERIAL_DEBUG) {Serial.println("Update not finished? Something went wrong!");}
      }
    } else {
      if (SERIAL_DEBUG) {Serial.println("Error Occurred. Error #: " + String(Update.getError()));}
    }

  } else {
    if (SERIAL_DEBUG) {Serial.println("Not enough space to begin OTA");}
  }
}

// check given FS for valid update.bin and perform update if available
void updateFromFS(fs::FS &fs) {
  File updateBin = fs.open("/update.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      if (SERIAL_DEBUG) {Serial.println("Error, update.bin is not a file");}
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      if (SERIAL_DEBUG) {Serial.println("Try to start update");}
      performUpdate(updateBin, updateSize);
    } else {
      if (SERIAL_DEBUG) {Serial.println("Error, file is empty");}
    }

    updateBin.close();

    // when finished remove the binary from sd card to indicate end of the process
    fs.remove("/update.bin");
  } else {
    if (SERIAL_DEBUG) {Serial.println("Could not load update.bin from sd root");}
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(10);
}
