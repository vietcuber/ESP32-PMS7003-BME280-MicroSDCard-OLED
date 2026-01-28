/*
 * Module: Main
 * Description: PMS7003, BME280, OLED, and SD Card Data Logging (Optimized)
 * Standard: SPARC Firmware code convention
 * Optimizations: Memory, Performance, Code Structure
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

/* --- CONSTANTS & CONFIGURATION --- */

/* PMS serial pins (ESP32) */
const uint8_t PmsRxPin_u8 = 16u;
const uint8_t PmsTxPin_u8 = 17u;

/* Screen Constants */
const uint8_t ScreenWidth_u8 = 128u;
const uint8_t ScreenHeight_u8 = 64u;
const int8_t OledReset_s8 = -1;

/* SD Card Chip Select */
const uint8_t SdCardCsPin_u8 = 5u;

/* Timing Intervals */
const uint32_t SpinnerInterval_u32 = 500u;      /* 500 ms for spinner update */
const uint32_t DataInterval_u32 = 1000u;        /* 1s for data update */
const uint32_t SavedDurationMs_u32 = 2000u;      /* 2 seconds notification */
const uint32_t BufferWriteInterval_u32 = 300000u; /* 5 minutes */

/* Buffer Configuration */
const uint16_t MaxBufferSize_u16 = 4096u;       /* 4KB buffer limit */
const uint8_t MaxBufferLines_u8 = 50u;          /* Max CSV lines in buffer */

/* --- GLOBAL OBJECTS --- */

/* Initialize UART for PMS */
HardwareSerial& PmsSerial_obj = Serial2;
PMS PmsSensor_obj(PmsSerial_obj);
PMS::DATA PmsData_st;

/* Initialize Display */
Adafruit_SSD1306 DisplayOled_obj(ScreenWidth_u8, ScreenHeight_u8, &Wire, OledReset_s8);

/* Initialize BME280 */
Adafruit_BME280 BmeSensor_obj;

/* SD Card File Object */
File MyFile_obj;

/* --- GLOBAL VARIABLES (PascalCase) --- */

/* Timing Counters */
uint32_t PreviousSpinnerMillis_u32 = 0u;
uint32_t PreviousDataMillis_u32 = 0u;
uint32_t LastBufferWriteTime_u32 = 0u;
uint32_t SavedUntil_u32 = 0u;

/* Sensor Data Storage */
uint16_t LastPm1_u16 = 0u;
uint16_t LastPm25_u16 = 0u;
uint16_t LastPm10_u16 = 0u;

float LastTemperature_f = 0.0f;
uint16_t LastPressure_u16 = 0u;
float LastAltitude_f = 0.0f;
uint16_t LastHumidity_u16 = 0u;

/* UI State */
const char SpinnerFrames_a8[] PROGMEM = {'|', '/', '-', '\\'};  /* Store in Flash */
const uint8_t SpinnerFrameCount_u8 = 4u;  /* Hardcode instead of sizeof */
uint8_t SpinnerIndex_u8 = 0u;

bool ShowSaved_bool = false;

/* SD Buffer Management */
String DataBuffer_string = "";
uint8_t BufferLineCount_u8 = 0u;
bool IsSdInitialized_bool = false;
bool NeedsDisplayUpdate_bool = true;  /* Flag to reduce unnecessary redraws */

/* --- LOCAL FUNCTION PROTOTYPES --- */

static void writeBufferToSd(void);
static void displayDataToSerial(void);
static void drawFullInfoScreen(void);
static void appendToBuffer(const String& dataLine_string);

/* --- FUNCTION DEFINITIONS --- */

/*
 * Function: appendToBuffer
 * Scope: Local (static)
 * Description: Adds data to buffer with overflow protection.
 */
static void appendToBuffer(const String& dataLine_string)
{
    /* Check buffer size limits */
    if (DataBuffer_string.length() + dataLine_string.length() > MaxBufferSize_u16 ||
        BufferLineCount_u8 >= MaxBufferLines_u8)
    {
        /* Force write if buffer is full */
        Serial.println("Buffer full. Force writing to SD...");
        writeBufferToSd();
    }
    
    DataBuffer_string += dataLine_string;
    DataBuffer_string += '\n';
    BufferLineCount_u8++;
}

/*
 * Function: writeBufferToSd
 * Scope: Local (static)
 * Description: Writes the RAM buffer to the SD card.
 * Optimization: Added buffer size checking
 */
static void writeBufferToSd(void) 
{
    /* 1. Check if buffer is empty */
    if (BufferLineCount_u8 == 0u) 
    {
        return;
    }

    /* 2. Write only if SD is initialized and File object is valid */
    if (IsSdInitialized_bool && MyFile_obj) 
    {
        Serial.print(F("Saving ")); /* F() macro saves SRAM */
        Serial.print(BufferLineCount_u8);
        Serial.println(F(" records to SD..."));
        
        /* Write data from RAM to File object */
        size_t bytesWritten_u32 = MyFile_obj.print(DataBuffer_string);

        if (bytesWritten_u32 > 0u) 
        {
            /* Flush to ensure physical write */
            MyFile_obj.flush();
            Serial.println(F("Data saved."));
            
            /* Clear RAM buffer */
            DataBuffer_string = ""; 
            DataBuffer_string.reserve(MaxBufferSize_u16); /* Pre-allocate */
            BufferLineCount_u8 = 0u;

            /* Update UI flag */
            ShowSaved_bool = true;
            SavedUntil_u32 = millis() + SavedDurationMs_u32;
            NeedsDisplayUpdate_bool = true;
        } 
        else 
        {
            Serial.println(F("Write failed!"));
        }
    } 
    else 
    {
        Serial.println(F("SD not ready. Clearing buffer."));
        DataBuffer_string = "";
        BufferLineCount_u8 = 0u;
    }
}

/*
 * Function: displayDataToSerial
 * Scope: Local (static)
 * Description: Prints current sensor data to Serial Monitor.
 * Optimization: Use F() macro for string literals
 */
static void displayDataToSerial(void) 
{
    Serial.println(F("\n--- DATA ---"));
    Serial.print(F("Time: "));        
    Serial.println(millis() / 1000u);
    Serial.print(F("PM1.0: "));       
    Serial.println(LastPm1_u16);
    Serial.print(F("PM2.5: "));       
    Serial.println(LastPm25_u16);
    Serial.print(F("PM10: "));        
    Serial.println(LastPm10_u16);
    Serial.print(F("Temperature: ")); 
    Serial.println(LastTemperature_f);
    Serial.print(F("Pressure: "));    
    Serial.println(LastPressure_u16);
    Serial.print(F("Altitude: "));    
    Serial.println(LastAltitude_f);
    Serial.print(F("Humidity: "));    
    Serial.println(LastHumidity_u16);
    Serial.println(F("----------------------------"));
}

/*
 * Function: drawFullInfoScreen
 * Scope: Local (static)
 * Description: Updates the OLED display with sensor data or status.
 * Optimization: Only redraw when necessary
 */
static void drawFullInfoScreen(void) 
{
    if (!NeedsDisplayUpdate_bool)
    {
        return;  /* Skip if no update needed */
    }
    
    DisplayOled_obj.clearDisplay();
    DisplayOled_obj.setTextSize(1);
    DisplayOled_obj.setTextColor(SSD1306_WHITE);

    /* --- LEFT COLUMN (PM SENSORS) --- */
    DisplayOled_obj.setCursor(0, 0);
    DisplayOled_obj.print(F("PM1.0: ")); 
    DisplayOled_obj.print(LastPm1_u16);

    DisplayOled_obj.setCursor(0, 10);
    DisplayOled_obj.print(F("PM2.5: ")); 
    DisplayOled_obj.print(LastPm25_u16);

    DisplayOled_obj.setCursor(0, 20);
    DisplayOled_obj.print(F("PM10 : ")); 
    DisplayOled_obj.print(LastPm10_u16);

    /* --- RIGHT COLUMN (ENVIRONMENT) - Align at x = 64 --- */
    DisplayOled_obj.setCursor(64, 0);
    DisplayOled_obj.print(F("T:")); 
    DisplayOled_obj.print(LastTemperature_f, 1); 
    DisplayOled_obj.print('C'); 

    DisplayOled_obj.setCursor(64, 10);
    DisplayOled_obj.print(F("H:")); 
    DisplayOled_obj.print(LastHumidity_u16); 
    DisplayOled_obj.print('%');

    DisplayOled_obj.setCursor(64, 20);
    DisplayOled_obj.print(F("P:")); 
    DisplayOled_obj.print(LastPressure_u16 / 100u); 
    DisplayOled_obj.print('h'); 

    DisplayOled_obj.setCursor(64, 30);
    DisplayOled_obj.print(F("A:")); 
    DisplayOled_obj.print(LastAltitude_f, 0);
    DisplayOled_obj.print('m');

    /* Draw separator line */
    DisplayOled_obj.drawLine(0, 45, 128, 45, SSD1306_WHITE);

    /* Logic to display "Saved" or Spinner */
    if (ShowSaved_bool && (millis() < SavedUntil_u32)) 
    {
        DisplayOled_obj.setCursor(0, 50);
        DisplayOled_obj.print(F("SAVED!"));
    } 
    else 
    {
        /* Reset saved flag if duration expired */
        if (ShowSaved_bool) 
        {
            ShowSaved_bool = false;
        }

        /* Display Spinner - read from PROGMEM */
        DisplayOled_obj.setCursor(0, 50);
        DisplayOled_obj.print((char)pgm_read_byte(&SpinnerFrames_a8[SpinnerIndex_u8]));

        /* Display Uptime */
        DisplayOled_obj.setCursor(20, 50);
        DisplayOled_obj.print(millis() / 1000u); 
        DisplayOled_obj.print('s');
    }

    DisplayOled_obj.display();
    NeedsDisplayUpdate_bool = false;  /* Reset update flag */
}

/*
 * Function: setup
 * Description: Standard Arduino initialization.
 */
void setup() 
{
    Serial.begin(115200u);
    Serial.println(F("ESP32 Starting. Initializing..."));

    /* Pre-allocate String buffer */
    DataBuffer_string.reserve(MaxBufferSize_u16);

    /* Initialize UART for PMS (Baud 9600) */
    PmsSerial_obj.begin(9600u, SERIAL_8N1, PmsRxPin_u8, PmsTxPin_u8);

    Wire.begin();
    Wire.setClock(100000u); /* Safe: 100 kHz */

    /* Initialize OLED */
    if (!DisplayOled_obj.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        Serial.println(F("OLED init failed!"));
        for(;;);  /* Halt if critical component fails */
    }
    
    DisplayOled_obj.clearDisplay();
    DisplayOled_obj.setTextSize(1);
    DisplayOled_obj.setTextColor(SSD1306_WHITE);
    DisplayOled_obj.setCursor(0, 0);
    DisplayOled_obj.println(F("Initializing..."));
    DisplayOled_obj.display();

    /* Initialize BME280 */
    if (!BmeSensor_obj.begin(0x76))
    {
        Serial.println(F("BME280 init failed!"));
    }

    /* --- INITIALIZE SD CARD --- */
    Serial.print(F("Initializing SD card..."));
    if (!SD.begin(SdCardCsPin_u8)) 
    {
        Serial.println(F("Failed!"));
        IsSdInitialized_bool = false;
    } 
    else 
    {
        Serial.println(F("Done."));
        IsSdInitialized_bool = true;

        /* Open file immediately and keep handle */
        MyFile_obj = SD.open("/datalog.csv", FILE_APPEND);

        if (MyFile_obj) 
        {
            Serial.println(F("File opened."));
            
            /* If file is empty, write header */
            if (MyFile_obj.size() == 0u) 
            {
                MyFile_obj.println(F("Time(s),PM1.0,PM2.5,PM10,Temperature,Pressure,Altitude,Humidity"));
                MyFile_obj.flush(); 
            }
        } 
        else 
        {
            Serial.println(F("File open error!"));
            IsSdInitialized_bool = false; 
        }
    }
    
    NeedsDisplayUpdate_bool = true;
}

/*
 * Function: loop
 * Description: Standard Arduino main loop.
 * Optimization: Reduced String operations, added conditional display updates
 */
void loop() 
{
    uint32_t currentTime_u32 = millis();

    /* --- DATA READING TASK --- */
    if ((currentTime_u32 - PreviousDataMillis_u32) >= DataInterval_u32) 
    {
        PreviousDataMillis_u32 = currentTime_u32;

        if (PmsSensor_obj.readUntil(PmsData_st, 1000u)) 
        {
            LastPm1_u16 = PmsData_st.PM_AE_UG_1_0;
            LastPm25_u16 = PmsData_st.PM_AE_UG_2_5;
            LastPm10_u16 = PmsData_st.PM_AE_UG_10_0;

            LastTemperature_f = BmeSensor_obj.readTemperature();
            LastPressure_u16 = (uint16_t)(BmeSensor_obj.readPressure());
            LastAltitude_f = BmeSensor_obj.readAltitude(1013.25f);
            LastHumidity_u16 = (uint16_t)BmeSensor_obj.readHumidity();

            displayDataToSerial();

            /* Build CSV line using char buffer (more efficient than String concatenation) */
            char csvLine_a8[128];
            snprintf(csvLine_a8, sizeof(csvLine_a8), 
                     "%lu,%u,%u,%u,%.2f,%u,%.1f,%u",
                     millis() / 1000u,
                     LastPm1_u16,
                     LastPm25_u16,
                     LastPm10_u16,
                     LastTemperature_f,
                     LastPressure_u16,
                     LastAltitude_f,
                     LastHumidity_u16);
            
            appendToBuffer(String(csvLine_a8));
            NeedsDisplayUpdate_bool = true;  /* Data changed, need display update */
        }
    }

    /* --- SD WRITE TASK --- */
    if ((currentTime_u32 - LastBufferWriteTime_u32) >= BufferWriteInterval_u32) 
    {
        LastBufferWriteTime_u32 = currentTime_u32;
        writeBufferToSd();
    }

    /* --- UI UPDATE TASK --- */
    /* Update Spinner Index based on timer */
    if ((currentTime_u32 - PreviousSpinnerMillis_u32) >= SpinnerInterval_u32)
    {
        PreviousSpinnerMillis_u32 = currentTime_u32;
        SpinnerIndex_u8 = (SpinnerIndex_u8 + 1u) & 0x03u;  /* Faster modulo 4 */
        NeedsDisplayUpdate_bool = true;  /* Spinner changed */
    }

    drawFullInfoScreen();
}