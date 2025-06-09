#pragma once

#define HW_VERSION 21 //10, 20, 21

# if HW_VERSION < 20

//#define PIN_BAT_LEVEL (gpio_num_t)1
#define PIN_SHUTTER GPIO_NUM_19
#define PIN_LED GPIO_NUM_14
#define PIN_FEEDBACK GPIO_NUM_20
#define PIN_UTIL_IO (gpio_num_t) 0 // Disabled
//#define PIN_PERIPH_PWR (gpio_num_t) 14
//#define PIN_POWERED (gpio_num_t) 21

#define PIN_MIC_CLK (gpio_num_t) 47
#define PIN_MIC_DATA (gpio_num_t) 48

#define PIN_USB_DP (gpio_num_t) 20
#define PIN_USB_DM (gpio_num_t) 19

#define PIN_SD_CS (gpio_num_t) 9
#define PIN_SPI_PICO (gpio_num_t) 38
#define PIN_SPI_POCI (gpio_num_t) 40
#define PIN_SPI_CLK (gpio_num_t) 39

#define PIN_CAM_PWDN GPIO_NUM_3
#define PIN_CAM_RESET (gpio_num_t) 21
#define PIN_CAM_SIO_D GPIO_NUM_17
#define PIN_CAM_SIO_C (gpio_num_t) 41
#define PIN_CAM_HREF GPIO_NUM_18
#define PIN_CAM_VSYNC (gpio_num_t) 42
#define PIN_CAM_XVCLK GPIO_NUM_11
#define PIN_CAM_PCLK GPIO_NUM_12
#define PIN_CAM_Y2 GPIO_NUM_6
#define PIN_CAM_Y3 GPIO_NUM_15
#define PIN_CAM_Y4 GPIO_NUM_16
#define PIN_CAM_Y5 GPIO_NUM_7
#define PIN_CAM_Y6 GPIO_NUM_5
#define PIN_CAM_Y7 GPIO_NUM_10
#define PIN_CAM_Y8 GPIO_NUM_4
#define PIN_CAM_Y9 GPIO_NUM_13

#define HW_VERSION_TXT "1.0"

# elif HW_VERSION == 20

#define PIN_BAT_LEVEL (gpio_num_t)1
#define PIN_SHUTTER GPIO_NUM_2
#define PIN_LED GPIO_NUM_3
#define PIN_FEEDBACK (gpio_num_t) 44
#define PIN_UTIL_IO GPIO_NUM_8
#define PIN_PERIPH_PWR (gpio_num_t) 14
#define PIN_POWERED (gpio_num_t) 21

#define PIN_MIC_CLK (gpio_num_t) 47
#define PIN_MIC_DATA (gpio_num_t) 48

#define PIN_USB_DP (gpio_num_t) 20
#define PIN_USB_DM (gpio_num_t) 19

#define PIN_SD_CS (gpio_num_t) 9
#define PIN_SPI_PICO (gpio_num_t) 38
#define PIN_SPI_POCI (gpio_num_t) 40
#define PIN_SPI_CLK (gpio_num_t) 39

#define PIN_CAM_RESET (gpio_num_t) 43
#define PIN_CAM_SIO_D GPIO_NUM_17
#define PIN_CAM_SIO_C (gpio_num_t) 41
#define PIN_CAM_HREF GPIO_NUM_18
#define PIN_CAM_VSYNC (gpio_num_t) 42
#define PIN_CAM_XVCLK GPIO_NUM_11
#define PIN_CAM_PCLK GPIO_NUM_12
#define PIN_CAM_Y2 GPIO_NUM_6
#define PIN_CAM_Y3 GPIO_NUM_15
#define PIN_CAM_Y4 GPIO_NUM_16
#define PIN_CAM_Y5 GPIO_NUM_7
#define PIN_CAM_Y6 GPIO_NUM_5
#define PIN_CAM_Y7 GPIO_NUM_10
#define PIN_CAM_Y8 GPIO_NUM_4
#define PIN_CAM_Y9 GPIO_NUM_13

#define HW_VERSION_TXT "2.0"

# else // HW Version 2.1
#define PIN_BAT_LEVEL (gpio_num_t)1
#define PIN_SHUTTER GPIO_NUM_2
#define PIN_LED GPIO_NUM_3
#define PIN_FEEDBACK (gpio_num_t) 9 // Flipped from 2.0 (due to limited pre-boot control.)
#define PIN_UTIL_IO GPIO_NUM_8
#define PIN_PERIPH_PWR (gpio_num_t) 14
#define PIN_POWERED (gpio_num_t) 21

#define PIN_MIC_CLK (gpio_num_t) 47
#define PIN_MIC_DATA (gpio_num_t) 48

#define PIN_USB_DP (gpio_num_t) 20
#define PIN_USB_DM (gpio_num_t) 19

#define PIN_SD_CS (gpio_num_t) 44 // Flipped from 2.0
#define PIN_SPI_PICO (gpio_num_t) 38
#define PIN_SPI_POCI (gpio_num_t) 40
#define PIN_SPI_CLK (gpio_num_t) 39

#define PIN_CAM_RESET (gpio_num_t) 43
#define PIN_CAM_SIO_D GPIO_NUM_17
#define PIN_CAM_SIO_C (gpio_num_t) 41
#define PIN_CAM_HREF GPIO_NUM_18
#define PIN_CAM_VSYNC (gpio_num_t) 42
#define PIN_CAM_XVCLK GPIO_NUM_11
#define PIN_CAM_PCLK GPIO_NUM_12
#define PIN_CAM_Y2 GPIO_NUM_6
#define PIN_CAM_Y3 GPIO_NUM_15
#define PIN_CAM_Y4 GPIO_NUM_16
#define PIN_CAM_Y5 GPIO_NUM_7
#define PIN_CAM_Y6 GPIO_NUM_5
#define PIN_CAM_Y7 GPIO_NUM_10
#define PIN_CAM_Y8 GPIO_NUM_4
#define PIN_CAM_Y9 GPIO_NUM_13

#define PIN_CAM_PWDN null

#define HW_VERSION_TXT "2.1"

#endif

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)

#define U0_TXD GPIO_NUM_43
#define U0_RXD GPIO_NUM_44
