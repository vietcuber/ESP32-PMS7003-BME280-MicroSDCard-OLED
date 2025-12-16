/*
 * Module: Main
 * Description: PMS7003, BME280, OLED, and SD Card Data Logging
 * Standard: SPARC Firmware code convention
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
const uint32_t SavedDurationMs_u32 = 500u;      /* 0.5 seconds notification */
const uint32_t BufferWriteInterval_u32 = 300000u; /* 5 minutes */

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
/* Using uint16_t as dust values are non-negative [cite: 7] */
uint16_t LastPm1_u16 = 0u;
uint16_t LastPm25_u16 = 0u;
uint16_t LastPm10_u16 = 0u;

float LastTemperature_f = 0.0f;
uint16_t LastPressure_u16 = 0u;
float LastAltitude_f = 0.0f;
uint16_t LastHumidity_u16 = 0u;

/* UI State */
/* Spinner frames */
const char SpinnerFrames_a8[] = {'|', '/', '-', '\\'};
const uint8_t SpinnerFrameCount_u8 = (uint8_t)(sizeof(SpinnerFrames_a8) / sizeof(SpinnerFrames_a8[0]));
uint8_t SpinnerIndex_u8 = 0u;

bool ShowSaved_bool = false;

/* SD Buffer Management */
String DataBuffer_string = "";
bool IsSdInitialized_bool = false;

/* --- LOCAL FUNCTION PROTOTYPES --- */

static void writeBufferToSd(void);
static void displayDataToSerial(void);
static void drawFullInfoScreen(void);

/* --- FUNCTION DEFINITIONS --- */

/*
 * Function: writeBufferToSd
 * Scope: Local (static)
 * Description: Writes the RAM buffer to the SD card.
 */
static void writeBufferToSd(void) 
{
    /* 1. Check if buffer is empty */
    if (DataBuffer_string.length() == 0u) 
    {
        return;
    }

    /* 2. Write only if SD is initialized and File object is valid */
    if (IsSdInitialized_bool && MyFile_obj) 
    {
        Serial.println("Saving buffer to SD card...");
        
        /* Write data from RAM to File object */
        size_t bytesWritten_u32 = MyFile_obj.print(DataBuffer_string);

        if (bytesWritten_u32 > 0u) 
        {
            /* Flush to ensure physical write */
            MyFile_obj.flush();
            Serial.println("Data saved.");
            
            /* Clear RAM buffer */
            DataBuffer_string = ""; 

            /* Update UI flag */
            ShowSaved_bool = true;
            SavedUntil_u32 = millis() + SavedDurationMs_u32;
        } 
        else 
        {
            Serial.println("Write failed (Possible hardware error).");
            /* Data remains in buffer */
        }
    } 
    else 
    {
        Serial.println("SD Card not ready or File not open. Skipping write.");
        /* Optional: Clear buffer to avoid RAM overflow if SD is removed */
        DataBuffer_string = "";
    }
}

/*
 * Function: displayDataToSerial
 * Scope: Local (static)
 * Description: Prints current sensor data to Serial Monitor.
 */
static void displayDataToSerial(void) 
{
    Serial.println("\n--- DATA ---");
    Serial.print("Time: ");        
    Serial.println(millis() / 1000u);
    Serial.print("PM1.0: ");       
    Serial.println(LastPm1_u16);
    Serial.print("PM2.5: ");       
    Serial.println(LastPm25_u16);
    Serial.print("PM10: ");        
    Serial.println(LastPm10_u16);
    Serial.print("Temperature: "); 
    Serial.println(LastTemperature_f);
    Serial.print("Pressure: ");    
    Serial.println(LastPressure_u16);
    Serial.print("Altitude: ");    
    Serial.println(LastAltitude_f);
    Serial.print("Humidity: ");    
    Serial.println(LastHumidity_u16);
    Serial.println("----------------------------");
}

/*
 * Function: drawFullInfoScreen
 * Scope: Local (static)
 * Description: Updates the OLED display with sensor data or status.
 */
static void drawFullInfoScreen(void) 
{
    DisplayOled_obj.clearDisplay();
    DisplayOled_obj.setTextSize(1);
    DisplayOled_obj.setTextColor(SSD1306_WHITE);

    /* --- LEFT COLUMN (PM SENSORS) --- */
    DisplayOled_obj.setCursor(0, 0);
    DisplayOled_obj.print("PM1.0: "); 
    DisplayOled_obj.print(LastPm1_u16);

    DisplayOled_obj.setCursor(0, 10);
    DisplayOled_obj.print("PM2.5: "); 
    DisplayOled_obj.print(LastPm25_u16);

    DisplayOled_obj.setCursor(0, 20);
    DisplayOled_obj.print("PM10 : "); 
    DisplayOled_obj.print(LastPm10_u16);

    /* --- RIGHT COLUMN (ENVIRONMENT) - Align at x = 64 --- */
    DisplayOled_obj.setCursor(64, 0);
    DisplayOled_obj.print("T:"); 
    DisplayOled_obj.print(LastTemperature_f, 1); 
    DisplayOled_obj.print("C"); 

    DisplayOled_obj.setCursor(64, 10);
    DisplayOled_obj.print("H:"); 
    DisplayOled_obj.print(LastHumidity_u16); 
    DisplayOled_obj.print("%");

    DisplayOled_obj.setCursor(64, 20);
    DisplayOled_obj.print("P:"); 
    DisplayOled_obj.print(LastPressure_u16 / 100u); 
    DisplayOled_obj.print("h"); 

    DisplayOled_obj.setCursor(64, 30);
    DisplayOled_obj.print("A:"); 
    DisplayOled_obj.print(LastAltitude_f, 0);
    DisplayOled_obj.print("m");

    /* Draw separator line */
    DisplayOled_obj.drawLine(0, 45, 128, 45, SSD1306_WHITE);

    /* Logic to display "Saved" or Spinner */
    if (ShowSaved_bool && (millis() < SavedUntil_u32)) 
    {
        DisplayOled_obj.setCursor(0, 50);
        DisplayOled_obj.print("SAVED!");
    } 
    else 
    {
        /* Reset saved flag if duration expired */
        if (ShowSaved_bool) 
        {
            ShowSaved_bool = false;
        }

        /* Display Spinner */
        DisplayOled_obj.setCursor(0, 50);
        DisplayOled_obj.print(SpinnerFrames_a8[SpinnerIndex_u8]);

        /* Display Uptime */
        DisplayOled_obj.setCursor(20, 50);
        DisplayOled_obj.print(millis() / 1000u); 
        DisplayOled_obj.print("s");
    }

    DisplayOled_obj.display();
}

/*
 * Function: setup
 * Description: Standard Arduino initialization.
 */
void setup() 
{
    Serial.begin(115200u);
    Serial.println("ESP32 Starting. PMS7003, BME280, OLED, SDCard initializing...");

    /* Initialize UART for PMS (Baud 9600) */
    PmsSerial_obj.begin(9600u, SERIAL_8N1, PmsRxPin_u8, PmsTxPin_u8);

    Wire.begin();
    Wire.setClock(100000u); /* Safe: 100 kHz */

    DisplayOled_obj.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    DisplayOled_obj.clearDisplay();
    DisplayOled_obj.setTextSize(1);
    DisplayOled_obj.setTextColor(SSD1306_WHITE);
    DisplayOled_obj.setCursor(0, 0);
    DisplayOled_obj.display();

    BmeSensor_obj.begin(0x76);

    /* --- INITIALIZE SD CARD --- */
    Serial.print("Initializing SD card...");
    if (!SD.begin(SdCardCsPin_u8)) 
    {
        Serial.println("Initialization failed! (Card not found)");
        IsSdInitialized_bool = false;
    } 
    else 
    {
        Serial.println("Initialization done.");
        IsSdInitialized_bool = true;

        /* Open file immediately and keep handle */
        MyFile_obj = SD.open("/datalog.csv", FILE_APPEND);

        if (MyFile_obj) 
        {
            Serial.println("File opened/created successfully.");
            
            /* If file is empty, write header */
            if (MyFile_obj.size() == 0u) 
            {
                MyFile_obj.println("Time(s), PM1.0, PM2.5, PM10, Temperature, Pressure, Altitude, Humidity");
                MyFile_obj.flush(); 
            }
        } 
        else 
        {
            Serial.println("Error opening datalog.csv");
            IsSdInitialized_bool = false; 
        }
    }
}

/*
 * Function: loop
 * Description: Standard Arduino main loop.
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
            LastPressure_u16 = (uint16_t)(BmeSensor_obj.readPressure() / 100.0f);
            LastAltitude_f = BmeSensor_obj.readAltitude(1013.25f);
            LastHumidity_u16 = (uint16_t)BmeSensor_obj.readHumidity();

            displayDataToSerial();

            /* Prepare CSV String */
            String dataString_string = String(millis() / 1000u) + "," + 
                                       String(LastPm1_u16) + "," + 
                                       String(LastPm25_u16) + "," + 
                                       String(LastPm10_u16) + "," + 
                                       String(LastTemperature_f) + "," + 
                                       String(LastPressure_u16) + "," + 
                                       String(LastAltitude_f) + "," + 
                                       String(LastHumidity_u16);
            
            DataBuffer_string += dataString_string + "\n";
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
        SpinnerIndex_u8++;
        if (SpinnerIndex_u8 >= SpinnerFrameCount_u8)
        {
            SpinnerIndex_u8 = 0u;
        }
    }

    drawFullInfoScreen();
}