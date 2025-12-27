/**
 * ESP32 Low Power Air Quality Monitor - Code Convention Compliant
 * Hardware: ESP32, PMS7003, BME280, SD Card, OLED 0.96"
 * Mode: Deep Sleep with 15 minutes cycle
 * Coding Standard: SPARC Firmware Code Convention
 * MODIFIED: Timestamp now includes sleep time for accurate total elapsed time
 */

#include "PMS.h"
#include "HardwareSerial.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SPI.h>
#include <SD.h>
#include "esp_sleep.h"

/* ============================================================================
 * HARDWARE CONFIGURATION
 * ========================================================================== */

/* PMS7003 Configuration */
#define PMS_RX_PIN 16
#define PMS_TX_PIN 17
HardwareSerial PMS_SERIAL(2);
PMS pms(PMS_SERIAL);
PMS::DATA data;

/* OLED Configuration */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* BME280 Sensor */
Adafruit_BME280 bme;

/* SD Card CS Pin */
#define SDCARD_CS 5

/* ============================================================================
 * TIMING CONFIGURATION (in milliseconds)
 * ========================================================================== */

#define WARMUP_TIME_MS        30000u  /* 30 seconds warm-up for PMS7003 */
#define SAMPLING_TIME_MS      2000u   /* 2 seconds sampling time */
#define DISPLAY_RESULT_TIME_MS 5000u  /* 5 seconds display time */
#define SLEEP_MESSAGE_TIME_MS 2000u   /* 2 seconds sleep message */
#define DEEP_SLEEP_TIME_US    900000000ULL /* 15 minutes = 900 seconds */
#define DEEP_SLEEP_TIME_S     900u    /* 15 minutes in seconds */


/* ============================================================================
 * RTC MEMORY - Persistent across Deep Sleep
 * ========================================================================== */

RTC_DATA_ATTR uint32_t totalRunTimeSeconds_u32 = 0u;
RTC_DATA_ATTR uint32_t totalElapsedTimeSeconds_u32 = 0u;  /* NEW: Total time including sleep */
RTC_DATA_ATTR uint32_t bootCount_u32 = 0u;

/* ============================================================================
 * SENSOR DATA STRUCTURE
 * ========================================================================== */

typedef struct
{
    uint16_t pm1_u16;
    uint16_t pm2_5_u16;
    uint16_t pm10_u16;
    float temperature_f32;
    float humidity_f32;
    float pressure_f32;
    float altitude_f32;
    uint32_t timestamp_u32;
} sensorData_st;

sensorData_st st_currentData = {0u, 0u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0u};

/* Hardware initialization status flags */
uint8_t oledOK_u8 = 0u;
uint8_t bmeOK_u8 = 0u;
uint8_t sdOK_u8 = 0u;
uint8_t pmsOK_u8 = 0u;

/* ============================================================================
 * FUNCTION PROTOTYPES
 * ========================================================================== */

static void initializeHardware (void);
static void warmupPhase (void);
static void samplingPhase (void);
static void sdLoggingPhase (void);
static void displayResults (void);
static void enterDeepSleep (void);
static void updateOLED (const char* p_state, const char* p_message, int32_t countdown_s32);
static uint8_t readPMSData (void);
static void readBMEData (void);
static void printDataToSerial (void);

/* ============================================================================
 * SETUP - Main execution flow then sleep
 * ========================================================================== */

void setup (void)
{
    uint32_t cycleStartTime_u32 = 0u;
    uint32_t cycleTime_u32 = 0u;
    
    /* Increment boot count */
    bootCount_u32++;
    
    /* Initialize Serial with safe delay */
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n\n========================================");
    Serial.println("ESP32 AIR QUALITY MONITOR - DEEP SLEEP");
    Serial.println("========================================");
    Serial.print("Boot #");
    Serial.print(bootCount_u32);
    Serial.print(" | Active Runtime: ");
    Serial.print(totalRunTimeSeconds_u32);
    Serial.print("s | Total Elapsed: ");
    Serial.print(totalElapsedTimeSeconds_u32);
    Serial.println("s\n");

    /* Save cycle start time */
    cycleStartTime_u32 = millis();

    /* 1. INITIALIZATION */
    initializeHardware();
    
    /* Check if any hardware initialized successfully */
    if ((0u == oledOK_u8) && (0u == bmeOK_u8) && (0u == sdOK_u8) && (0u == pmsOK_u8))
    {
        Serial.println("WARNING: No hardware initialized successfully!");
        Serial.println("Continuing anyway for testing...");
    }

    /* 2. WARM-UP */
    if (0u != pmsOK_u8)
    {
        warmupPhase();
    }
    else
    {
        Serial.println("Skipping warm-up (PMS not available)");
    }

    /* 3. SAMPLING */
    samplingPhase();

    /* 4. SD LOGGING */
    if (0u != sdOK_u8)
    {
        sdLoggingPhase();
    }
    else
    {
        Serial.println("Skipping SD logging (SD card not available)");
    }

    /* 5. DISPLAY RESULTS */
    displayResults();

    /* Calculate cycle time and update both counters */
    cycleTime_u32 = (millis() - cycleStartTime_u32) / 1000u;
    totalRunTimeSeconds_u32 += cycleTime_u32;
    totalElapsedTimeSeconds_u32 += cycleTime_u32;
    
    /* Add sleep time to total elapsed time (will be applied before next boot) */
    if (bootCount_u32 > 1u)  /* Don't add sleep time for first boot */
    {
        totalElapsedTimeSeconds_u32 += DEEP_SLEEP_TIME_S;
    }
    
    Serial.print("\nCycle completed in ");
    Serial.print(cycleTime_u32);
    Serial.println(" seconds");
    Serial.print("Active runtime: ");
    Serial.print(totalRunTimeSeconds_u32);
    Serial.println(" seconds");
    Serial.print("Total elapsed time: ");
    Serial.print(totalElapsedTimeSeconds_u32);
    Serial.println(" seconds");

    /* 6. ENTER DEEP SLEEP */
    enterDeepSleep();
}

void loop (void)
{
    /* Not used - Deep sleep mode */
}

/* ============================================================================
 * HARDWARE INITIALIZATION
 * ========================================================================== */

static void initializeHardware (void)
{
    Serial.println("-> Initializing hardware...");
    
    /* Initialize I2C bus first */
    Wire.begin();
    Wire.setClock(100000);
    delay(100);
    Serial.println("I2C bus initialized");

    /* Initialize OLED */
    Serial.print("Initializing OLED... ");
    oledOK_u8 = display.begin(SSD1306_SWITCHCAPVCC, 0x3C) ? 1u : 0u;
    if (0u != oledOK_u8)
    {
        Serial.println("OK");
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("INITIALIZING...");
        display.display();
    }
    else
    {
        Serial.println("FAILED");
    }

    /* Initialize BME280 */
    Serial.print("Initializing BME280... ");
    bmeOK_u8 = bme.begin(0x76) ? 1u : 0u;
    if (0u != bmeOK_u8)
    {
        Serial.println("OK");
    }
    else
    {
        Serial.println("FAILED");
        /* Try alternate I2C address 0x77 */
        Serial.print("Trying address 0x77... ");
        bmeOK_u8 = bme.begin(0x77) ? 1u : 0u;
        if (0u != bmeOK_u8)
        {
            Serial.println("OK");
        }
        else
        {
            Serial.println("FAILED");
        }
    }

    /* Initialize PMS7003 */
    Serial.print("Initializing PMS7003... ");
    PMS_SERIAL.begin(9600, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);
    delay(100);
    pms.wakeUp();
    delay(200);
    pmsOK_u8 = 1u; /* Assume OK, will verify during read */
    Serial.println("OK");

    /* Initialize SD Card */
    Serial.print("Initializing SD Card... ");
    sdOK_u8 = SD.begin(SDCARD_CS) ? 1u : 0u;
    if (0u != sdOK_u8)
    {
        Serial.println("OK");
    }
    else
    {
        Serial.println("FAILED");
    }

    Serial.println();
}

/* ============================================================================
 * WARM-UP PHASE
 * ========================================================================== */

static void warmupPhase (void)
{
    uint32_t warmupStart_u32 = 0u;
    int32_t remainingSeconds_s32 = 0;
    
    Serial.println("-> WARM-UP phase (30 seconds)...");
    
    warmupStart_u32 = millis();
    
    while ((millis() - warmupStart_u32) < WARMUP_TIME_MS)
    {
        remainingSeconds_s32 = (int32_t)((WARMUP_TIME_MS - (millis() - warmupStart_u32)) / 1000u);
        
        if (0u != oledOK_u8)
        {
            updateOLED("WARM UP", "PMS7003 ready", remainingSeconds_s32);
        }
        
        Serial.print("  Warming up... ");
        Serial.print(remainingSeconds_s32);
        Serial.print(" sec\r");
        delay(1000);
    }
    
    Serial.println("\n  Warm-up completed!          ");
    Serial.println();
}

/* ============================================================================
 * SAMPLING PHASE
 * ========================================================================== */

static void samplingPhase (void)
{
    uint8_t success_u8 = 0u;
    uint8_t i_u8 = 0u;
    
    Serial.println("-> SAMPLING phase...");
    
    if (0u != oledOK_u8)
    {
        updateOLED("SAMPLING", "Reading sensors...", -1);
    }

    /* Read PMS7003 */
    if (0u != pmsOK_u8)
    {
        Serial.print("  Reading PMS7003... ");
        success_u8 = 0u;
        for (i_u8 = 0u; i_u8 < 5u; i_u8++)
        {
            if (0u != readPMSData())
            {
                success_u8 = 1u;
                Serial.println("OK");
                break;
            }
            delay(200);
        }
        if (0u == success_u8)
        {
            Serial.println("FAILED");
            pmsOK_u8 = 0u;
        }
    }

    /* Read BME280 */
    if (0u != bmeOK_u8)
    {
        Serial.print("  Reading BME280... ");
        readBMEData();
        Serial.println("OK");
    }

    /* Save timestamp - USE TOTAL ELAPSED TIME (includes sleep) */
    st_currentData.timestamp_u32 = totalElapsedTimeSeconds_u32;

    Serial.println("  Sampling completed!");
    printDataToSerial();
    Serial.println();
}

/* ============================================================================
 * SD CARD LOGGING PHASE
 * ========================================================================== */

static void sdLoggingPhase (void)
{
    uint8_t fileExists_u8 = 0u;
    char buffer_au8[128];
    File dataFile;
    
    Serial.println("-> SD LOGGING phase...");
    
    if (0u != oledOK_u8)
    {
        updateOLED("SD LOGGING", "Writing data...", -1);
    }

    fileExists_u8 = SD.exists("/data.csv") ? 1u : 0u;
    dataFile = SD.open("/data.csv", FILE_APPEND);

    if (!dataFile)
    {
        Serial.println("  Failed to open data.csv");
        sdOK_u8 = 0u;
        return;
    }

    /* Write header if needed */
    if ((0u == fileExists_u8) || (0u == dataFile.size()))
    {
        Serial.println("  Writing CSV header...");
        dataFile.println("Time(s),PM1.0,PM2.5,PM10,Temp(C),Hum(%),Pres(hPa),Alt(m)");
    }

    /* Write data */
    snprintf(buffer_au8, sizeof(buffer_au8), "%lu,%d,%d,%d,%.2f,%.2f,%.2f,%.2f",
             st_currentData.timestamp_u32,
             st_currentData.pm1_u16,
             st_currentData.pm2_5_u16,
             st_currentData.pm10_u16,
             st_currentData.temperature_f32,
             st_currentData.humidity_f32,
             st_currentData.pressure_f32,
             st_currentData.altitude_f32);
    
    dataFile.println(buffer_au8);
    dataFile.flush();
    dataFile.close();

    Serial.println("  Data written successfully!");
    Serial.println();
}

/* ============================================================================
 * DISPLAY RESULTS
 * ========================================================================== */

static void displayResults (void)
{
    Serial.println("-> DISPLAY phase (5 seconds)...");
    
    if (0u != oledOK_u8)
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        /* Title */
        display.setCursor(0, 0);
        display.println("=== DATA ===");

        /* PM Data */
        display.setCursor(0, 12);
        display.print("PM1 :");
        display.print(st_currentData.pm1_u16);
        
        display.setCursor(0, 22);
        display.print("PM2.5:");
        display.print(st_currentData.pm2_5_u16);
        
        display.setCursor(0, 32);
        display.print("PM10 :");
        display.print(st_currentData.pm10_u16);

        /* Environment Data */
        display.setCursor(0, 42);
        display.print("T:");
        display.print(st_currentData.temperature_f32, 1);
        display.print("C H:");
        display.print(st_currentData.humidity_f32, 0);
        display.print("%");
        
        display.setCursor(0, 52);
        display.print("P:");
        display.print(st_currentData.pressure_f32, 0);
        display.print(" A:");
        display.print(st_currentData.altitude_f32, 0);

        display.display();
    }

    delay(DISPLAY_RESULT_TIME_MS);
    Serial.println("  Display completed!");
    Serial.println();
}

/* ============================================================================
 * ENTER DEEP SLEEP
 * ========================================================================== */

static void enterDeepSleep (void)
{
    Serial.println("-> Preparing DEEP SLEEP...");

    /* Display sleep message */
    if (0u != oledOK_u8)
    {
        updateOLED("SLEEPING", "15 minutes", 15);
        delay(SLEEP_MESSAGE_TIME_MS);
        display.ssd1306_command(SSD1306_DISPLAYOFF);
    }

    /* Put PMS7003 to sleep */
    if (0u != pmsOK_u8)
    {
        pms.sleep();
        delay(100);
    }

    /* Configure timer wakeup */
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIME_US);
    
    Serial.println("  Entering deep sleep NOW!");
    Serial.println("========================================\n");
    Serial.flush();
    delay(100);

    /* Enter deep sleep */
    esp_deep_sleep_start();
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ========================================================================== */

static void updateOLED (const char* p_state, const char* p_message, int32_t countdown_s32)
{
    if (0u == oledOK_u8)
    {
        return;
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    /* State */
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(p_state);

    /* Message */
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println(p_message);

    /* Countdown */
    if (0 <= countdown_s32)
    {
        display.setTextSize(2);
        display.setCursor(0, 35);
        display.print(countdown_s32);
        display.print(" sec");
    }

    /* Info */
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("B:");
    display.print(bootCount_u32);
    display.print(" T:");
    display.print(totalElapsedTimeSeconds_u32);
    display.print("s");

    display.display();
}

static uint8_t readPMSData (void)
{
    uint8_t result_u8 = 0u;
    
    if (pms.readUntil(data, 2000))
    {
        st_currentData.pm1_u16 = data.PM_AE_UG_1_0;
        st_currentData.pm2_5_u16 = data.PM_AE_UG_2_5;
        st_currentData.pm10_u16 = data.PM_AE_UG_10_0;
        result_u8 = 1u;
    }
    
    return result_u8;
}

static void readBMEData (void)
{
    st_currentData.temperature_f32 = bme.readTemperature();
    st_currentData.humidity_f32 = bme.readHumidity();
    st_currentData.pressure_f32 = bme.readPressure() / 100.0f;
    st_currentData.altitude_f32 = bme.readAltitude(1013.25f);
}

static void printDataToSerial (void)
{
    Serial.println("  ========================================");
    Serial.print("  Timestamp:    ");
    Serial.print(st_currentData.timestamp_u32);
    Serial.println(" seconds");
    Serial.println("  ----------------------------------------");
    Serial.print("  PM1.0:        ");
    Serial.print(st_currentData.pm1_u16);
    Serial.println(" ug/m3");
    Serial.print("  PM2.5:        ");
    Serial.print(st_currentData.pm2_5_u16);
    Serial.println(" ug/m3");
    Serial.print("  PM10:         ");
    Serial.print(st_currentData.pm10_u16);
    Serial.println(" ug/m3");
    Serial.println("  ----------------------------------------");
    Serial.print("  Temperature:  ");
    Serial.print(st_currentData.temperature_f32);
    Serial.println(" C");
    Serial.print("  Humidity:     ");
    Serial.print(st_currentData.humidity_f32);
    Serial.println(" %");
    Serial.print("  Pressure:     ");
    Serial.print(st_currentData.pressure_f32);
    Serial.println(" hPa");
    Serial.print("  Altitude:     ");
    Serial.print(st_currentData.altitude_f32);
    Serial.println(" m");
    Serial.println("  ========================================");
}