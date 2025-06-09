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
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"
#include <Update.h>
#include "driver/rtc_io.h"

#include "Pinout.h"
#include "Buzzer.h"
#include "PWMPlayer.h"
#include "Dictaphone.h"
#include "Camera.h"
#include "JsonConsts.h"
#include "Discord.h"
#include "Whisper.h"

#define FILENAME_CONFIG "/config.json"
#define FILENAME_ERROR "/error.log"
#define FILENAME_STATISTICS "/stats.bin"
#define FS_MOUNT_POINT "/sdcard"

#define uS_TO_S_FACTOR 1000000ULL
#define S_TO_MIN_FACTOR 60ULL

#define HW_DEBUG false
#define HAS_USB_MSC true

#if HAS_USB_MSC
#include <USB.h>
#include <USBMSC.h>
#endif

static const char *TAG = "Main";

#define WATCHDOG_TIMEOUT 60000UL  //Things have gone wrong if we have looped for a minute.

// Time settings
const char *ntpServer1 = "pool.ntp.org";
const char *time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";  // List: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

WiFiMulti wifiMulti;
JsonDocument configuration;
WiFiClientSecure client;
Whisper whisper;
AsyncTelegram2 myBot(client);
Discord discord;
PWMPlayer vibration(PIN_FEEDBACK, false);
PWMPlayer led(PIN_LED, false);
Dictaphone dictaphone(PIN_MIC_CLK, PIN_MIC_DATA);
Camera camera;
Buzzer buzzer(PIN_UTIL_IO);

#if HW_VERSION >= 20 && HAS_USB_MSC
// USB Mass Storage Class (MSC) object
USBMSC msc;
#endif

bool timeSet = false;
bool transmissionFinished = false;
bool messageDelivered;
bool justCapturedAudio = false;
bool justCapturedPicture = false;
esp_sleep_wakeup_cause_t wakeup_reason;
esp_reset_reason_t reset_reason;
bool wasUSBPlugged = false;
bool wasUSBEjected = false;
bool hasWIFI = false;

const uint8_t TYPE_VIBRATION = 0;
const uint8_t TYPE_MELODY = 1;

// How long after boot to determine picture or audio recording
RTC_DATA_ATTR uint16_t altFuncDelay = 500;            // Time before entering recording mode
RTC_DATA_ATTR bool shouldRefresh = true;              // Should the device refresh SD, time, etc?
RTC_DATA_ATTR time_t latestNTPUpdate = 0;             // Time of latest NTP Update
RTC_DATA_ATTR uint16_t uploadFromTime = 0;            // From when uploads are allowed
RTC_DATA_ATTR uint16_t uploadToTime = 0;              // Until when uploads are allowed
RTC_DATA_ATTR bool hasStuffToUpload = false;          // Do we know we have pictures or audio waiting to go?
RTC_DATA_ATTR bool knowsUploadState = false;          // Do we know we have pictures waiting to go?
RTC_DATA_ATTR uint16_t uploadImmediateDelay = 0;      // Minutes to delay upload immediately after capture
RTC_DATA_ATTR uint16_t uploadRetryDelay = 0;          // Minutes to delay retrying upload
RTC_DATA_ATTR uint8_t feedbackType = TYPE_VIBRATION;  // Type of feedback to use

RTC_DATA_ATTR time_t latestBatteryUpdate = 0;      // Time of latest Battery Warning
RTC_DATA_ATTR uint16_t latestBatteryVoltage = 0;   // Latest read battery voltage in mV
RTC_DATA_ATTR uint8_t batteryWarnPercentage = 20;  // Percentage to warn battery at
RTC_DATA_ATTR uint8_t batteryWarnDelay = 24;       // Hour delay betweeen warnings


void printWakeupReason(esp_sleep_wakeup_cause_t wakeup_reason) {
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: ESP_LOGI(TAG, "Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: ESP_LOGI(TAG, "Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: ESP_LOGI(TAG, "Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: ESP_LOGI(TAG, "Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: ESP_LOGI(TAG, "Wakeup caused by ULP program"); break;
    default: ESP_LOGI(TAG, "Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void printResetReason(esp_reset_reason_t reset_reason) {
  switch (reset_reason) {
    case 1: ESP_LOGI(TAG, "Vbat power on reset"); break;
    case 3: ESP_LOGI(TAG, "Software reset digital core"); break;
    case 4: ESP_LOGI(TAG, "Legacy watch dog reset digital core"); break;
    case 5: ESP_LOGI(TAG, "Deep Sleep reset digital core"); break;
    case 6: ESP_LOGI(TAG, "Reset by SLC module, reset digital core"); break;
    case 7: ESP_LOGI(TAG, "Timer Group0 Watch dog reset digital core"); break;
    case 8: ESP_LOGI(TAG, "Timer Group1 Watch dog reset digital core"); break;
    case 9: ESP_LOGI(TAG, "RTC Watch dog Reset digital core"); break;
    case 10: ESP_LOGI(TAG, "Instrusion tested to reset CPU"); break;
    case 11: ESP_LOGI(TAG, "Time Group reset CPU"); break;
    case 12: ESP_LOGI(TAG, "Software reset CPU"); break;
    case 13: ESP_LOGI(TAG, "RTC Watch dog Reset CPU"); break;
    case 14: ESP_LOGI(TAG, "for APP CPU, reseted by PRO CPU"); break;
    case 15: ESP_LOGI(TAG, "Reset when the vdd voltage is not stable"); break;
    case 16: ESP_LOGI(TAG, "RTC Watch dog reset digital core and rtc module"); break;
    default: ESP_LOGI(TAG, "NO_MEAN");
  }
}

void setup() {
  wakeup_reason = esp_sleep_get_wakeup_cause();
  reset_reason = esp_reset_reason();

  initializeGPIO();

  // Debug info
  printResetReason(reset_reason);
  printWakeupReason(wakeup_reason);
  ESP_LOGI(TAG, "Shutter pin state: %d", digitalRead(PIN_SHUTTER));
  ESP_LOGI(TAG, "Power pin state: %d", digitalRead(PIN_POWERED));

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1 || wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {                                                                                                  // We woke up from external input
    if ((esp_sleep_get_ext1_wakeup_status() == BUTTON_PIN_BITMASK(PIN_SHUTTER) && (digitalRead(PIN_POWERED) == LOW || HAS_USB_MSC == false)) || wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {  // Woke up from button (while not usb powered), that means take a picture or audio
      digitalWrite(PIN_LED, HIGH);
      ESP_LOGI(TAG, "Beginning camera");
      esp_err_t cameraBegin = camera.begin();
      if (cameraBegin != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed");
        camera.stop();
        logErrToSD("cameraBegin", cameraBegin);
        goToSleep();  // If failed to initialize, sleep
      }
      ESP_LOGI(TAG, "Camera began");

      ESP_LOGI(TAG, "Beginning dictaphone");
      if (!dictaphone.begin()) {
        ESP_LOGE(TAG, "Dictaphone init failed");
        logErrToSD("dictaphoneBegin", 0);
        goToSleep();  // If failed to initialize, sleep
      }
      ESP_LOGI(TAG, "Dictaphone began");

      // After boot we may take a picture or begin recording.
      // Recording begins if the button has been held for more than BOOT_DELAY.
      // Picture may begin any time before if the button has been let go.
      while (millis() < altFuncDelay && isCaptureButtonHeld()) {
        // While waiting, start recording out the i2s buffer to discard odd offset first 100ms
        dictaphone.warmup();
        camera.warmup();
      }
      ESP_LOGI(TAG, "Warmup");

      if (isCaptureButtonHeld()) {
        ESP_LOGI(TAG, "Recording audio");
        camera.stop();
        dictaphone.beginRecording();
        while (isCaptureButtonHeld()) {
          dictaphone.continueRecording();
        }
        justCapturedAudio = true;
        ESP_LOGI(TAG, "Just captured audio");
        if (HW_DEBUG) {
          blinkLed(3);
        }
      } else {
        ESP_LOGI(TAG, "Taking pictures");
        if (!camera.takePicture()) {
          camera.stop();
          logErrToSD("cameraTakePicture", 0);
          goToSleep();
        }
        justCapturedPicture = true;
        ESP_LOGI(TAG, "Just captured picture");
        if (HW_DEBUG) {
          blinkLed(5);
        }
      }
      digitalWrite(PIN_LED, LOW);  // Turn LED off
    }
  }

  // Reset time zone as it is lost between deep sleep cycles
  ESP_LOGI(TAG, "Setting timezone");
  setenv("TZ", time_zone, 1);
  tzset();

  if (isNewlyBooted()) {
    ESP_LOGI(TAG, "Newly booted");
    updateFromFS(SD_MMC);   // Check if we have an update file available on SD card, and then perform update. Otherwise move on.
    ensureFilestructure();  // Create directories if not exist
  }

  if (justCapturedAudio) {
    // Indicate finished recording
    ESP_LOGI(TAG, "Just captured audio");
    initializeSDCard();
    initializeConfiguration();
    if (feedbackType == TYPE_VIBRATION) {
      vibration.play(configuration[STR_FEEDBACK][STR_AUDIO_END]);
    } else {
      buzzer.play(configuration[STR_FEEDBACK][STR_AUDIO_END]);
    }
    led.play(configuration[STR_LED][STR_AUDIO_END]);
    while (isFeedbackPlaying()) {
      delay(5);
    }
    dictaphone.processRecording(configuration[STR_AUDIO_LIMITER]);
    dictaphone.saveRecording(SD_MMC, getNamePrefix());
    updateStatistics(0, 1, dictaphone.getSecondsRecorded());
    goToSleep();  // Sleep early
  }

  if (justCapturedPicture) {
    // Indicate completion
    ESP_LOGI(TAG, "Just captured picture");
    initializeSDCard();
    initializeConfiguration();
    if (feedbackType == TYPE_VIBRATION) {
      vibration.play(String(configuration[STR_FEEDBACK][STR_PHOTO_END]));
    } else {
      buzzer.play(String(configuration[STR_FEEDBACK][STR_PHOTO_END]));
    }
    led.play(configuration[STR_LED][STR_PHOTO_END]);
    while (isFeedbackPlaying()) {
      delay(5);
    }

    camera.savePicture(SD_MMC, getNamePrefix(), configuration[STR_PHOTO_ROTATION]);
    updateStatistics(1, 0, 0);
    camera.stop();
    goToSleep();  // Sleep early
  }

  // We woke up from a cold boot or from timer, ensure our time is good
  if (isNewlyBooted() || (hasPicturesToUpload() && withinUploadTime()) || isNTPOld()) {
    ESP_LOGI(TAG, "Connecting wifi");
    initializeSDCard();
    initializeConfiguration();
    hasWIFI = connectWifi();
    if (hasWIFI) {
      struct tm timeinfo;
      getLocalTime(&timeinfo);
      if (isNTPOld()) {
        ESP_LOGI(TAG, "Setting time");
        sntp_set_time_sync_notification_cb(timeavailable);
        configTzTime(time_zone, ntpServer1);
        uint32_t whileTimer = 0;
        while (!timeSet) {
          delay(100);
          whileTimer += 100;
          ESP_LOGV(TAG, ".");
          if (whileTimer > WATCHDOG_TIMEOUT) {
            ESP_LOGW(TAG, "Setting time hit watchdog timeout");
            logErrToSD("setNTPTime", 0);
            goToSleep();  // Failed to get time within two minutes? Sleep and try again later.
          }
        }
      }
    }
    printLocalTime();
  }
  latestBatteryVoltage = getBatteryVoltage();
  if (shouldWarnBattery() && hasWIFI) {
    connectAndSendBatteryUpdate();
  }
  shouldRefresh = false;  // We have ensured filestructure, updated firmware and set time. Settled in for now.
  if (hasPicturesToUpload() && withinUploadTime() && hasWIFI) {
    connectTelegramAndUploadAvailable();
  }

#if HW_VERSION >= 20 && HAS_USB_MSC
  if (digitalRead(PIN_POWERED) && wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) {
    initializeSDCard();
    initializeConfiguration();
    // Disconnect wifi, as it is no longer needed
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    msc.vendorID("ESP32");
    msc.productID("AutCorder");
    msc.productRevision(HW_VERSION_TXT);
    msc.onRead(onUSBRead);
    msc.onWrite(onUSBWrite);
    msc.onStartStop(onUSBStartStop);
    msc.mediaPresent(true);
    msc.begin(SD_MMC.numSectors(), SD_MMC.sectorSize());

    ESP_LOGI(TAG, "Initializing USB");

    USB.begin();
    USB.onEvent(usbEventCallback);

    ESP_LOGI(TAG, "Card Size: %lluMB\n", SD_MMC.totalBytes() / 1024 / 1024);
    ESP_LOGI(TAG, "Sector: %d\tCount: %d\n", SD_MMC.sectorSize(), SD_MMC.numSectors());
  }
  while (digitalRead(PIN_POWERED) && (wasUSBPlugged || millis() < 2 * 60 * 1000) && (!wasUSBEjected) && (millis() < 4 * 60 * 60 * 1000)) {
    // Only keep usb active while powered, data within a minute, not yet ejected from machine, timeout 4 hours
    if (!led.isPlaying()) {
      led.play(configuration[STR_LED][STR_ACTIVITY]);
    }
    delay(10);
  }
  shouldRefresh = true;  // After SD card has changed, may need to reload values / FW update
#endif
  ESP_LOGI(TAG, "Old reset and wakeup reasons: ");
  printResetReason(reset_reason);
  printWakeupReason(wakeup_reason);
  goToSleep();
}

void blinkLed(int count) {
  while (count > 0) {
    digitalWrite(PIN_LED, HIGH);
    delay(500);
    digitalWrite(PIN_LED, LOW);
    delay(500);
    count--;
  }
}

void logErrToSD(const char *tag, esp_err_t error) {
  initializeSDCard();
  fs::FS &fs = SD_MMC;
  File errorFile = fs.open(FILENAME_ERROR, FILE_APPEND, true);
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  errorFile.printf("%d-%d-%d %d:%d:%d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  errorFile.print(" - ");
  errorFile.print(tag);
  errorFile.print(": ");
  errorFile.printf("%d", error);
  errorFile.print("\n");
  errorFile.close();
}

uint16_t getBatteryVoltage() {
  analogReadResolution(12);
  return (uint16_t)(analogReadMilliVolts(PIN_BAT_LEVEL) * 2.0);
}

#if HAS_USB_MSC

static int32_t onUSBWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize) {
    return false;  // disk error
  }
  ESP_LOGV(TAG, "Write lba: %ld\toffset: %ld\tbufsize: %ld", lba, offset, bufsize);
  for (int x = 0; x < bufsize / secSize; x++) {
    uint8_t blkbuffer[secSize];
    memcpy(blkbuffer, (uint8_t *)buffer + secSize * x, secSize);
    if (!SD_MMC.writeRAW(blkbuffer, lba + x)) {
      return false;
    }
  }
  return bufsize;
}

static int32_t onUSBRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize) {
    return false;  // disk error
  }
  ESP_LOGV(TAG, "Read lba: %ld\toffset: %ld\tbufsize: %ld\tsector: %lu", lba, offset, bufsize, secSize);
  for (int x = 0; x < bufsize / secSize; x++) {
    if (!SD_MMC.readRAW((uint8_t *)buffer + (x * secSize), lba + x)) {
      return false;  // outside of volume boundary
    }
  }
  wasUSBPlugged = true;
  return bufsize;
}

static bool onUSBStartStop(uint8_t power_condition, bool start, bool load_eject) {
  ESP_LOGI(TAG, "Start/Stop power: %u\tstart: %d\teject: %d", power_condition, start, load_eject);
  // If we are stopping and ejecting the drive, sleep the device
  if (start == false && load_eject == true) {
    wasUSBEjected = true;
  }
  return true;
}

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == ARDUINO_USB_EVENTS) {
    arduino_usb_event_data_t *data = (arduino_usb_event_data_t *)event_data;
    switch (event_id) {
      case ARDUINO_USB_STARTED_EVENT: ESP_LOGI(TAG, "USB PLUGGED"); break;
      case ARDUINO_USB_STOPPED_EVENT: ESP_LOGI(TAG, "USB UNPLUGGED"); break;
      case ARDUINO_USB_SUSPEND_EVENT: ESP_LOGI(TAG, "USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en); break;
      case ARDUINO_USB_RESUME_EVENT: ESP_LOGI(TAG, "USB RESUMED"); break;

      default: break;
    }
  }
}

#endif

void timeavailable(struct timeval *t) {
  timeSet = true;
  ESP_LOGI(TAG, "Got time adjustment from NTP!");
  printLocalTime();
  latestNTPUpdate = time(NULL);
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    ESP_LOGE(TAG, "Failed to obtain time");
    return;
  }
  ESP_LOGI(TAG, "%d-%d-%d %d:%d:%d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// Internal RTC has some drift, so resync every day
bool isNTPOld() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  if (timeinfo.tm_year < 123) {
    return true;
  }
  double secondsSinceNTP = difftime(latestNTPUpdate, time(NULL));
  return secondsSinceNTP > 24 * 60 * 60;  // If more than 24 hours since last update
}

String getNamePrefix() {
  if (configuration[STR_UTIL][STR_TYPE] == STR_DISABLED) {
    return "";
  }
  pinMode(PIN_UTIL_IO, INPUT_PULLUP);
  if (digitalRead(PIN_UTIL_IO)) {
    return configuration[STR_UTIL][STR_ON_TEXT];
  }
  return configuration[STR_UTIL][STR_OFF_TEXT];
}

// Ensure all are called for the tick
bool isFeedbackPlaying() {
  bool isVibrationPlaying = vibration.isPlaying();
  bool isBuzzerPlaying = buzzer.isPlaying();
  bool isLedPlaying = led.isPlaying();
  return isVibrationPlaying || isBuzzerPlaying || isLedPlaying;
}

bool isSDInitialized = false;
void initializeSDCard() {
  if (isSDInitialized) {
    return;
  }
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  if (!SD_MMC.setPins(PIN_SPI_CLK, PIN_SPI_PICO, PIN_SPI_POCI)) {
    ESP_LOGE(TAG, "Failed to set SD pins!");
    goToSleep();
  }
  if (!SD_MMC.begin(FS_MOUNT_POINT, true)) {
    ESP_LOGE(TAG, "SD Card Mount Failed");
    goToSleep();
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    ESP_LOGE(TAG, "No SD Card attached");
    goToSleep();
  }
  isSDInitialized = true;
}

bool isGPIOInitialized = false;
void initializeGPIO() {
  if (isGPIOInitialized) {
    return;
  }

  uart_driver_delete(UART_NUM_0);
  gpio_reset_pin(U0_RXD);
  gpio_reset_pin(U0_TXD);

  rtc_gpio_deinit(PIN_SHUTTER);

  // Other isolated rtc gpio pins
  rtc_gpio_hold_dis(PIN_LED);
  rtc_gpio_hold_dis(PIN_UTIL_IO);
  rtc_gpio_hold_dis(PIN_SD_CS);

  rtc_gpio_hold_dis(PIN_CAM_RESET);
  rtc_gpio_hold_dis(PIN_CAM_SIO_D);
  rtc_gpio_hold_dis(PIN_CAM_SIO_C);
  rtc_gpio_hold_dis(PIN_CAM_HREF);
  rtc_gpio_hold_dis(PIN_CAM_XVCLK);
  rtc_gpio_hold_dis(PIN_CAM_PCLK);
  rtc_gpio_hold_dis(PIN_CAM_Y2);
  rtc_gpio_hold_dis(PIN_CAM_Y3);
  rtc_gpio_hold_dis(PIN_CAM_Y4);
  rtc_gpio_hold_dis(PIN_CAM_Y5);
  rtc_gpio_hold_dis(PIN_CAM_Y6);
  rtc_gpio_hold_dis(PIN_CAM_Y7);
  rtc_gpio_hold_dis(PIN_CAM_Y8);
  rtc_gpio_hold_dis(PIN_CAM_Y9);

#if HW_VERSION >= 20
  rtc_gpio_hold_dis(PIN_PERIPH_PWR);
  rtc_gpio_hold_dis(PIN_BAT_LEVEL);
#else  // HW Versions 1 have Camera PWDN pins
  gpio_deep_sleep_hold_dis();  // Has been held during sleep to enable pulling down
  gpio_hold_dis(PIN_CAM_PWDN);
#endif

  // Ready GPIO for use
  pinMode(PIN_FEEDBACK, OUTPUT);
  digitalWrite(PIN_FEEDBACK, LOW);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_SHUTTER, INPUT_PULLDOWN);

#if HW_VERSION >= 20
  pinMode(PIN_POWERED, INPUT);
  pinMode(PIN_PERIPH_PWR, INPUT_PULLDOWN);  // Actually an output, but avoids brownouts
#endif
  isGPIOInitialized = true;
}

bool isConfigInitialized = false;
void initializeConfiguration() {
  if (isConfigInitialized) {
    return;
  }

  configuration[STR_WIFI][0][STR_SSID] = "WIFI SSID Name";
  configuration[STR_WIFI][0][STR_PASSWORD] = "12345678";

  configuration[STR_TELEGRAM][STR_ENABLED] = false;
  configuration[STR_TELEGRAM][STR_CHAT_ID][0] = 0;
  configuration[STR_TELEGRAM][STR_TOKEN] = "";

  configuration[STR_DISCORD][STR_ENABLED] = false;
  configuration[STR_DISCORD][STR_CHAT_ID] = "1234567890123456789";
  configuration[STR_DISCORD][STR_TOKEN] = "";
  configuration[STR_DISCORD][STR_NAME] = "AutCorder";
  
  configuration[STR_WHISPER][STR_ENABLED] = false;
  configuration[STR_WHISPER][STR_DOMAIN] = "";
  configuration[STR_WHISPER][STR_PATH] = "";
  configuration[STR_WHISPER][STR_LANGUAGE] = "";
  configuration[STR_WHISPER][STR_TOKEN] = "";

  configuration[STR_ALT_FUNC_DELAY] = 400;
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

  configuration[STR_BATTERY][STR_WARN_DELAY] = 24;
  configuration[STR_BATTERY][STR_WARN_PERCENTAGE] = 20;

  configuration[STR_FEEDBACK][STR_TYPE] = STR_VIBRATION;
  configuration[STR_FEEDBACK][STR_PHOTO_END] = "";
  configuration[STR_FEEDBACK][STR_AUDIO_END] = "";
  configuration[STR_FEEDBACK][STR_UPLOAD_END] = "";
  configuration[STR_FEEDBACK][STR_ACTIVITY] = "";

  configuration[STR_LED][STR_PHOTO_END] = "";
  configuration[STR_LED][STR_AUDIO_END] = "";
  configuration[STR_LED][STR_UPLOAD_END] = "";
  configuration[STR_LED][STR_ACTIVITY] = "";

  configuration[STR_UTIL][STR_TYPE] = STR_DISABLED;
  configuration[STR_UTIL][STR_ON_TEXT] = "👍 ";
  configuration[STR_UTIL][STR_OFF_TEXT] = "👎 ";
  configuration[STR_UTIL][STR_INVERTED] = false;

  fs::FS &fs = SD_MMC;

  // Create config if not exist
  if (fs.exists(FILENAME_CONFIG)) {
    File configFile = fs.open(FILENAME_CONFIG);
    DeserializationError error = deserializeJson(configuration, configFile);
    if (error) {
      ESP_LOGE(TAG, "Failed to read file, using default configuration");
    }
  } else {
    File configFile = fs.open(FILENAME_CONFIG, FILE_WRITE, true);
    serializeJson(configuration, configFile);
    configFile.close();
  }
  altFuncDelay = configuration[STR_ALT_FUNC_DELAY];

  // Load upload times into memory
  int fromHour = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_HOUR];
  int fromMinute = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_FROM][STR_MINUTE];
  uploadFromTime = fromHour * 60 + fromMinute;
  int toHour = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_UNTIL][STR_HOUR];
  int toMinute = configuration[STR_UPLOAD][STR_UPLOAD_ALLOWED_UNTIL][STR_MINUTE];
  uploadToTime = toHour * 60 + toMinute;

  uploadImmediateDelay = (uint16_t)configuration[STR_UPLOAD][STR_UPLOAD_IMM_DELAY];
  uploadRetryDelay = (uint16_t)configuration[STR_UPLOAD][STR_UPLOAD_RETRY_DELAY];

  batteryWarnPercentage = configuration[STR_BATTERY][STR_WARN_PERCENTAGE];
  batteryWarnDelay = configuration[STR_BATTERY][STR_WARN_DELAY] = 24;

  if (configuration[STR_FEEDBACK][STR_TYPE] == STR_VIBRATION) {
    feedbackType = TYPE_VIBRATION;
  } else {
    feedbackType = TYPE_MELODY;
  }


  isConfigInitialized = true;
}

struct captureStatistics {
  uint32_t picturesTaken;
  uint32_t audioSegmentsRecorded;
  uint32_t audioSecondsRecorded;
};

void updateStatistics(uint8_t picturesTaken, uint8_t clipsRecorded, uint16_t secondsRecorded) {
  // If we don't collect statistics, do nothing.
  if (!configuration[STR_STATISTICS]) {
    return;
  }
  captureStatistics stats;
  fs::FS &fs = SD_MMC;
  // Read or create new stats file
  if (fs.exists(FILENAME_STATISTICS)) {
    File statsFile = fs.open(FILENAME_STATISTICS);
    statsFile.read((uint8_t *)&stats, sizeof(captureStatistics));
    statsFile.close();
  } else {
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

  if (uploadFromTime < uploadToTime) {  // If end later than beginning, later than start AND earlier than beginning
    return currentTime > uploadFromTime && currentTime < uploadToTime;
  } else {  // Else if start later than beginning, later than start OR earlier than beginning
    return currentTime > uploadFromTime || currentTime < uploadToTime;
  }
}

bool shouldWarnBattery() {
  double secondsSinceBattery = difftime(latestBatteryUpdate, time(NULL));
  if (secondsSinceBattery < batteryWarnDelay * 60 * 60) {  // If less than batteryWarnDelay hours since last update
    return false;
  }
  return getBatteryPercentage() < batteryWarnPercentage;
}

uint8_t getBatteryPercentage() {
  long batteryPercentage = map(latestBatteryVoltage, 3600, 4200, 0, 100);  // TODO, more accurate mapping https://blog.ampow.com/lipo-voltage-chart/
  if (batteryPercentage < 0) {
    return 0;
  }
  if (batteryPercentage > 100) {
    return 100;
  }
  return batteryPercentage;
}

void connectAndSendBatteryUpdate() {
  if (configuration[STR_TELEGRAM][STR_ENABLED]) {
    ESP_LOGI(TAG, "Initializing Telegram");
    myBot.setUpdateTime(500);  // Minimum time between telegram bot calls
    myBot.setTelegramToken(configuration[STR_TELEGRAM][STR_TOKEN]);
    myBot.addSentCallback(messageSent, 3000);
    myBot.begin();
  }
  if (configuration[STR_DISCORD][STR_ENABLED]) {
    discord.init(configuration[STR_DISCORD][STR_CHAT_ID], configuration[STR_DISCORD][STR_TOKEN], configuration[STR_DISCORD][STR_NAME]);
  }
  String batteryMessage = "*Warning:* Battery low!  \n🔋 ";
  batteryMessage += String(getBatteryPercentage());
  batteryMessage += "%  \n⚡️ ";
  batteryMessage += String(latestBatteryVoltage);
  batteryMessage += "mV  \n";
  if (configuration[STR_TELEGRAM][STR_ENABLED]) {
    JsonArray chatIds = configuration[STR_TELEGRAM][STR_CHAT_ID].as<JsonArray>();
    if (chatIds[0] == 0) {
      ESP_LOGE(TAG, "Bad Telegram chat id");
      return;
    }
    for (JsonVariant chatIdJson : chatIds) {
      int64_t chatId = chatIdJson.as<int64_t>();
      myBot.setFormattingStyle(AsyncTelegram2::FormatStyle::MARKDOWN);
      myBot.sendTo(chatId, batteryMessage);

      // Wait for upload to finish
      uint32_t whileTimer = 0;
      while (!transmissionFinished) {
        getTelegramMessages();
        delay(50);
        whileTimer += 50;
        ESP_LOGV(TAG, ".");
        if (whileTimer > WATCHDOG_TIMEOUT) {
          ESP_LOGW(TAG, "Upload hit watchdog timeout");
          goToSleep();  // Failed to upload within two minutes? Sleep and try again later.
        }
      }
      transmissionFinished = false;
    }
  }
  if (configuration[STR_DISCORD][STR_ENABLED]) {
    if (!discord.sendMessage(batteryMessage)) {
      ESP_LOGE(TAG, "Failed to send to Discord");
      messageDelivered = false;
    }
  }
  latestBatteryUpdate = time(NULL);
}

// Returns time until next upload span. 0 if we can upload right now.
uint16_t minutesUntilUploadTime() {
  if (withinUploadTime()) {
    return 0;
  }
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int currentTime = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  if (uploadFromTime > currentTime) {
    return uploadFromTime - currentTime;  // Time between now and next upload span.
  }
  return 24 * 60 - currentTime + uploadFromTime;  // Whatever is left of the day plus time until next upload span.
}

bool hasPicturesToUpload() {
  if (knowsUploadState) {
    return hasStuffToUpload;
  }
  initializeSDCard();
  fs::FS &fs = SD_MMC;
  File newDir = fs.open("/new");
  if (!newDir || !newDir.isDirectory()) {
    ESP_LOGE(TAG, "Error scanning new dir");
    hasStuffToUpload = false;
    return false;
  }
  File file = newDir.openNextFile();
  if (file) {
    file.close();
    ESP_LOGI(TAG, "Has new files to upload");
    hasStuffToUpload = true;
    return true;
  }
  ESP_LOGI(TAG, "No files to upload");
  hasStuffToUpload = false;
  return false;
}

void messageSent(bool sent) {
  if (sent) {
    ESP_LOGI(TAG, "Last message was delivered");
  } else {
    ESP_LOGW(TAG, "Last message was NOT delivered");
    //messageDelivered = false;
  }
  transmissionFinished = true;
}

void getTelegramMessages() {
  TBMessage msg;
  if (myBot.getNewMessage(msg)) {
    MessageType msgType = msg.messageType;

    // Received a text message
    if (msgType == MessageText) {
      ESP_LOGI(TAG, "Text message received: %s", msg.text);
    }
  }
}

const char *token = "xxxxxxxxxxx:xxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

void connectTelegramAndUploadAvailable() {
  if (configuration[STR_TELEGRAM][STR_ENABLED]) {
    ESP_LOGI(TAG, "Initializing Telegram");
    myBot.setUpdateTime(500);  // Minimum time between telegram bot calls
    myBot.setTelegramToken(configuration[STR_TELEGRAM][STR_TOKEN]);
    myBot.addSentCallback(messageSent, 3000);
    myBot.begin();
  }
  if (configuration[STR_DISCORD][STR_ENABLED]) {
    discord.init(configuration[STR_DISCORD][STR_CHAT_ID], configuration[STR_DISCORD][STR_TOKEN], configuration[STR_DISCORD][STR_NAME]);
  }
  if (configuration[STR_WHISPER][STR_ENABLED]){
    whisper.init(configuration[STR_WHISPER][STR_DOMAIN], configuration[STR_WHISPER][STR_PATH], configuration[STR_WHISPER][STR_LANGUAGE], configuration[STR_WHISPER][STR_TOKEN]);
  }

  if (hasPicturesToUpload()) {
    ESP_LOGI(TAG, "Has stuff to upload");
    fs::FS &fs = SD_MMC;
    File newDir = fs.open("/new");
    if (!newDir || !newDir.isDirectory()) {
      ESP_LOGE(TAG, "Error scanning new dir");
      return;
    }
    File file = newDir.openNextFile();
    while (file) {
      if (file.isDirectory()) {
        continue;
      } else {
        String caption = file.name();
        if (configuration[STR_WHISPER][STR_ENABLED] && String(file.name()).endsWith(".wav")){
          caption = whisper.transcribeFile(file);
        }
        if (configuration[STR_TELEGRAM][STR_ENABLED]) {
          JsonArray chatIds = configuration[STR_TELEGRAM][STR_CHAT_ID].as<JsonArray>();
          if (chatIds[0] == 0) {
            ESP_LOGE(TAG, "Bad Telegram chat id");
            continue;
          }
          for (JsonVariant chatIdJson : chatIds) {
            int64_t chatId = chatIdJson.as<int64_t>();
            File fileToSend = fs.open(file.path());
            ESP_LOGI(TAG, "Uploading: %s of size: %d to: %d", fileToSend.name(), fileToSend.size(), chatId);
            // Check what we are sending, and send it

            if (String(fileToSend.name()).endsWith(".jpg")) {
              myBot.sendPhoto(chatId, fileToSend, fileToSend.size(), fileToSend.name(), caption.c_str());
            } else {
              myBot.sendDocument(chatId, fileToSend, fileToSend.size(), AsyncTelegram2::DocumentType::VOICE, fileToSend.name(), caption.c_str());
            }

            // Wait for upload to finish
            uint32_t whileTimer = 0;
            messageDelivered = true;
            while (!transmissionFinished) {
              getTelegramMessages();
              delay(50);
              whileTimer += 50;
              playProgressFeedback();
              ESP_LOGV(TAG, ".");
              if (whileTimer > WATCHDOG_TIMEOUT) {
                ESP_LOGW(TAG, "Upload hit watchdog timeout");
                goToSleep();  // Failed to upload within two minutes? Sleep and try again later.
              }
            }
            transmissionFinished = false;
          }
        }
        if (configuration[STR_DISCORD][STR_ENABLED]) {
          if (!discord.sendFile(file)) {
            ESP_LOGE(TAG, "Failed to send to Discord");
            messageDelivered = false;
          } else {
            discord.sendMessage(caption);
          }
        }
        if (messageDelivered) {
          // Move the sent file to old pictures or delete depending on config
          if (configuration[STR_UPLOAD][STR_UPLOAD_KEEP_FILE_AFTER]) {
            ESP_LOGI(TAG, "Moving %s to /old/", String(file.name()));
            fs.rename("/new/" + String(file.name()), "/old/" + String(file.name()));
          } else {
            ESP_LOGI(TAG, "Deleting %s", String(file.name()));
            fs.remove("/new/" + String(file.name()));
          }
        }
      }
      file = newDir.openNextFile();
    }
  }
  if (feedbackType == TYPE_VIBRATION) {
    vibration.play(configuration[STR_FEEDBACK][STR_UPLOAD_END]);
  } else {
    buzzer.play(configuration[STR_FEEDBACK][STR_UPLOAD_END]);
  }
  led.play(configuration[STR_LED][STR_UPLOAD_END]);
}

void playProgressFeedback() {
  if (feedbackType == TYPE_VIBRATION) {
    if (!vibration.isPlaying()) {
      vibration.play(configuration[STR_FEEDBACK][STR_ACTIVITY]);
    }
  } else {
    if (!buzzer.isPlaying()) {
      buzzer.play(configuration[STR_FEEDBACK][STR_ACTIVITY]);
    }
  }
  if (!led.isPlaying()) {
    led.play(configuration[STR_LED][STR_ACTIVITY]);
  }
}

bool connectWifi() {
  client.setCACert(telegram_cert);

  // Connect to wifi
  wifiMulti.setStrictMode(true);
  JsonArray wifiAPs = configuration[STR_WIFI];
  for (JsonVariant wifiAP : wifiAPs) {
    wifiMulti.addAP(wifiAP[STR_SSID], wifiAP[STR_PASSWORD]);
  }

  ESP_LOGI(TAG, "Connecting Wifi...");
  uint32_t connectTime = 0;
  if (wifiMulti.run((configuration[STR_WIFI_TIMEOUT] | 4) * 1000) != WL_CONNECTED) {
    ESP_LOGE(TAG, "Failed to connect to WIFI");
    return false;
  }
  ESP_LOGI(TAG, "WiFi connected. IP address: %s", WiFi.localIP().toString());
  return true;
}

void ensureFilestructure() {
  initializeSDCard();
  fs::FS &fs = SD_MMC;
  if (!fs.exists("/new")) {
    fs.mkdir("/new");
  }
  if (!fs.exists("/old")) {
    fs.mkdir("/old");
  }
}

boolean isNewlyBooted() {
  return (wakeup_reason != ESP_SLEEP_WAKEUP_EXT0 && wakeup_reason != ESP_SLEEP_WAKEUP_EXT1 && wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) || shouldRefresh;
}

void goToSleep() {
  ESP_LOGI(TAG, "Going to sleep");

  // Finish indications
  while (isFeedbackPlaying()) {
    delay(5);
  }

  digitalWrite(PIN_LED, LOW);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Enable wakeup if we just captured something or there are still old captures to retry uploading.
  // Use different time for each.
  // But if we are outside upload time segment, wait until we get there.

  if (justCapturedPicture || justCapturedAudio || shouldRefresh) {
    uint64_t minutesToSleep = max((uint64_t)minutesUntilUploadTime(), (uint64_t)uploadImmediateDelay);
    // This appears to be nonsense, or at least limited to esp32, not s3
    minutesToSleep = min(minutesToSleep, 120ULL);  // Max 2 hours, known issue https://forum.arduino.cc/t/esp32-deep-sleep-is-24h-sleep-effectively-possible/677173/2
    esp_sleep_enable_timer_wakeup(minutesToSleep * S_TO_MIN_FACTOR * uS_TO_S_FACTOR);
  } else if (hasPicturesToUpload() || isNTPOld()) {
    uint64_t minutesToSleep = max((uint64_t)minutesUntilUploadTime(), (uint64_t)uploadRetryDelay);
    // This appears to be nonsense, or at least limited to esp32, not s3
    minutesToSleep = min(minutesToSleep, 120ULL);  // Max 2 hours, known issue https://forum.arduino.cc/t/esp32-deep-sleep-is-24h-sleep-effectively-possible/677173/2
    esp_sleep_enable_timer_wakeup(minutesToSleep * S_TO_MIN_FACTOR * uS_TO_S_FACTOR);
  } /*else{
    esp_sleep_enable_timer_wakeup(24 * 60 * 60 * uS_TO_S_FACTOR); // Wake up at least once every 24 hours, just to check in
  }*/
  ESP_LOGI(TAG, "Wifi off + wake set");
  SD_MMC.end();

#if HW_VERSION < 20
  esp_sleep_enable_ext0_wakeup(PIN_SHUTTER, HIGH);
#else
#if HAS_USB_MSC
  esp_sleep_enable_ext1_wakeup_io(BUTTON_PIN_BITMASK(PIN_SHUTTER) | BUTTON_PIN_BITMASK(PIN_POWERED), ESP_EXT1_WAKEUP_ANY_HIGH);
#else
  esp_sleep_enable_ext0_wakeup(PIN_SHUTTER, HIGH);
#endif
#endif
  esp_deep_sleep_disable_rom_logging();  // suppress boot messages -> https://esp32.com/viewtopic.php?t=30954

  digitalWrite(PIN_PERIPH_PWR, HIGH);

  // Isolate gpio pins to minimize current draw
  rtc_gpio_isolate(PIN_LED);
  rtc_gpio_isolate(PIN_UTIL_IO);
  rtc_gpio_isolate(PIN_SD_CS);

  rtc_gpio_isolate(PIN_CAM_RESET);
  rtc_gpio_isolate(PIN_CAM_SIO_D);
  rtc_gpio_isolate(PIN_CAM_SIO_C);
  rtc_gpio_isolate(PIN_CAM_HREF);
  rtc_gpio_isolate(PIN_CAM_XVCLK);
  rtc_gpio_isolate(PIN_CAM_PCLK);
  rtc_gpio_isolate(PIN_CAM_Y2);
  rtc_gpio_isolate(PIN_CAM_Y3);
  rtc_gpio_isolate(PIN_CAM_Y4);
  rtc_gpio_isolate(PIN_CAM_Y5);
  rtc_gpio_isolate(PIN_CAM_Y6);
  rtc_gpio_isolate(PIN_CAM_Y7);
  rtc_gpio_isolate(PIN_CAM_Y8);
  rtc_gpio_isolate(PIN_CAM_Y9);

#if HW_VERSION >= 20
  gpio_reset_pin(PIN_PERIPH_PWR);
  rtc_gpio_isolate(PIN_BAT_LEVEL);
  rtc_gpio_isolate(PIN_PERIPH_PWR);
#else
  pinMode(PIN_CAM_PWDN, OUTPUT);  // Stong pullup
  digitalWrite(PIN_CAM_PWDN, HIGH);
  gpio_hold_en(PIN_CAM_PWDN);  // Will wake if not enabled*/
#endif

  ESP_LOGI(TAG, "See you.");
  delay(50);
  esp_deep_sleep_start();
}

// Borrowed from SD_Update exampple
// perform the actual update from a given stream
void performUpdate(Stream &updateSource, size_t updateSize) {
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      ESP_LOGI(TAG, "Written: %d successfully", written);
    } else {
      ESP_LOGE(TAG, "Written only : %d / %d. Retry?", written, updateSize);
    }
    if (Update.end()) {
      ESP_LOGI(TAG, "OTA done!");
      if (Update.isFinished()) {
        ESP_LOGI(TAG, "Update successfully completed. Rebooting.");
      } else {
        ESP_LOGE(TAG, "Update not finished? Something went wrong!");
      }
    } else {
      ESP_LOGE(TAG, "Error Occurred. Error #: %d", Update.getError());
    }

  } else {
    ESP_LOGE(TAG, "Not enough space to begin OTA");
  }
}

// check given FS for valid update.bin and perform update if available
void updateFromFS(fs::FS &fs) {
  initializeSDCard();
  File updateBin = fs.open("/update.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      ESP_LOGE(TAG, "Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      ESP_LOGI(TAG, "Try to start update");
      performUpdate(updateBin, updateSize);
    } else {
      ESP_LOGE(TAG, "Error, file is empty");
    }

    updateBin.close();

    // when finished remove the binary from sd card to indicate end of the process
    fs.remove("/update.bin");
  } else {
    ESP_LOGE(TAG, "Could not load update.bin from sd root");
  }
}

void loop() {
  delay(10);
}
