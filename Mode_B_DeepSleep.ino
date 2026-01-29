/*
 * ============================================================================
 * TÊN FILE CODE: Mode B - Deep Sleep Monitoring
 * ============================================================================
 * MÔ TẢ: Thiết bị đo chất lượng không khí tiết kiệm pin sử dụng:
 *        - Cảm biến bụi PMS7003
 *        - Cảm biến nhiệt độ/độ ẩm/áp suất BME280
 *        - Màn hình OLED SSD1306
 *        - Thẻ nhớ SD để lưu dữ liệu
 *        - Pin có thể sạc lại
 * 
 * CHỨC NĂNG:
 *        1. Đo dữ liệu cảm biến mỗi 15 phút (900 giây)
 *        2. Thời gian warm-up PMS7003: 30 giây (đếm ngược)
 *        3. Thời gian đo mẫu: 1 giây
 *        4. Hiển thị dữ liệu lên OLED: 3 giây
 *        5. Hiển thị thông báo ngủ: 3 giây
 *        6. Đi vào deep sleep: 15 phút
 *        7. Chỉ theo dõi Total Elapsed Time
 * 
 * QUY TRÌNH MỖI CHU KỲ:
 *        - Khởi động và khởi tạo phần cứng
 *        - Warm-up PMS7003 (30 giây đếm ngược)
 *        - Đọc dữ liệu cảm biến (1 giây)
 *        - Ghi dữ liệu vào SD card
 *        - Hiển thị kết quả (3 giây)
 *        - Hiển thị thông báo ngủ (3 giây)
 *        - Đi vào deep sleep (15 phút)
 * 
 * CHUẨN CODE: SPARC Firmware Code Convention (version 1.02)
 * ============================================================================
 */

/* ============================================================================
 * INCLUDE THƯ VIỆN
 * ============================================================================ */
#include "PMS.h"                /* Thư viện cảm biến bụi PMS7003 */
#include "HardwareSerial.h"     /* UART phần cứng ESP32 */
#include <Wire.h>               /* Giao tiếp I2C */
#include <Adafruit_GFX.h>       /* Thư viện đồ họa cơ bản */
#include <Adafruit_SSD1306.h>   /* Thư viện màn hình OLED */
#include <Adafruit_Sensor.h>    /* Thư viện cảm biến Adafruit */
#include <Adafruit_BME280.h>    /* Thư viện cảm biến BME280 */
#include <SPI.h>                /* Giao tiếp SPI cho SD card */
#include <SD.h>                 /* Thư viện thẻ nhớ SD */
#include "esp_sleep.h"          /* Thư viện Deep Sleep ESP32 */

/* ============================================================================
 * ĐỊNH NGHĨA HẰNG SỐ - GPIO PINS
 * ============================================================================ */

/* Chân kết nối PMS7003 với ESP32 */
const uint8_t PMS_RX_PIN_U8 = 16u;  /* Chân RX của ESP32 (nhận dữ liệu từ PMS TX) */
const uint8_t PMS_TX_PIN_U8 = 17u;  /* Chân TX của ESP32 (gửi dữ liệu đến PMS RX) */

/* Chân kết nối SD Card */
const uint8_t SD_CS_PIN_U8 = 5u;    /* Chân Chip Select của SD card */

/* ============================================================================
 * ĐỊNH NGHĨA HẰNG SỐ - CẤU HÌNH OLED
 * ============================================================================ */
const uint8_t OLED_WIDTH_U8 = 128u;   /* Độ rộng màn hình OLED (pixel) */
const uint8_t OLED_HEIGHT_U8 = 64u;   /* Độ cao màn hình OLED (pixel) */
const int8_t OLED_RESET_PIN_S8 = -1;  /* Không sử dụng chân reset (-1) */
const uint8_t OLED_I2C_ADDRESS_U8 = 0x3Cu; /* Địa chỉ I2C của OLED */

/* ============================================================================
 * ĐỊNH NGHĨA HẰNG SỐ - THỜI GIAN (milliseconds)
 * ============================================================================ */

/* Thời gian warm-up cho PMS7003 */
const uint32_t WARMUP_DURATION_MS_U32 = 30000u;        /* Warm-up 30 giây */
const uint32_t WARMUP_DURATION_S_U32 = 30u;            /* 30 giây */

/* Khoảng thời gian đo dữ liệu cảm biến */
const uint32_t SENSOR_READ_INTERVAL_MS_U32 = 1000u;    /* Đo 1 giây */

/* Thời gian hiển thị kết quả lên OLED */
const uint32_t OLED_DISPLAY_DURATION_MS_U32 = 3000u;   /* Hiển thị 3 giây */

/* Thời gian hiển thị thông báo "SLEEPING..." */
const uint32_t SLEEP_DISPLAY_DURATION_MS_U32 = 3000u;  /* Hiển thị 3 giây */

/* Thời gian deep sleep (microseconds) */
const uint64_t DEEP_SLEEP_DURATION_US_U64 = 900000000ULL; /* 15 phút = 900 giây */

/* Thời gian deep sleep (seconds) - để tính toán */
const uint32_t DEEP_SLEEP_DURATION_S_U32 = 900u;       /* 15 phút = 900 giây */

/* ============================================================================
 * ĐỊNH NGHĨA HẰNG SỐ - THÔNG SỐ BME280
 * ============================================================================ */
const uint8_t BME280_I2C_ADDRESS_U8 = 0x76u;     /* Địa chỉ I2C của BME280 */
const float SEA_LEVEL_PRESSURE_HPA_F = 1013.25f; /* Áp suất mực nước biển (hPa) */

/* ============================================================================
 * ĐỊNH NGHĨA CẤU TRÚC DỮ LIỆU - SENSOR DATA
 * ============================================================================ */

/* Cấu trúc lưu trữ dữ liệu cảm biến */
typedef struct
{
    uint16_t pm1_u16;           /* Nồng độ bụi PM1.0 (µg/m³) */
    uint16_t pm25_u16;          /* Nồng độ bụi PM2.5 (µg/m³) */
    uint16_t pm10_u16;          /* Nồng độ bụi PM10 (µg/m³) */
    float temperature_f;        /* Nhiệt độ (°C) */
    float humidity_f;           /* Độ ẩm (%) */
    float pressure_f;           /* Áp suất (hPa) */
    float altitude_f;           /* Độ cao (m) */
    uint32_t timestamp_u32;     /* Thời điểm đo (giây) */
} SensorData_st;

/* ============================================================================
 * KHAI BÁO ĐỐI TƯỢNG TOÀN CỤC
 * ============================================================================ */

/* Đối tượng UART cho cảm biến PMS7003 */
HardwareSerial& PmsSerial_obj = Serial2;

/* Đối tượng cảm biến PMS7003 */
PMS PmsSensor_obj(PmsSerial_obj);

/* Cấu trúc dữ liệu PMS (do thư viện PMS.h cung cấp) */
PMS::DATA st_PmsData;

/* Đối tượng màn hình OLED */
Adafruit_SSD1306 OledDisplay_obj(OLED_WIDTH_U8, OLED_HEIGHT_U8, &Wire, OLED_RESET_PIN_S8);

/* Đối tượng cảm biến BME280 */
Adafruit_BME280 BmeSensor_obj;

/* ============================================================================
 * BIẾN TOÀN CỤC - LƯU TRỮ DỮ LIỆU CẢM BIẾN (PascalCase)
 * ============================================================================ */

/* Dữ liệu cảm biến hiện tại */
SensorData_st st_CurrentSensorData = {0u, 0u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0u};

/* ============================================================================
 * BIẾN TOÀN CỤC - TRẠNG THÁI PHẦN CỨNG (PascalCase)
 * ============================================================================ */

/* Cờ kiểm tra các thiết bị đã khởi tạo thành công */
bool IsOledReady_bool = false;      /* Trạng thái OLED */
bool IsBmeReady_bool = false;       /* Trạng thái BME280 */
bool IsSdCardReady_bool = false;    /* Trạng thái SD Card */
bool IsPmsReady_bool = false;       /* Trạng thái PMS7003 */

/* ============================================================================
 * BIẾN RTC MEMORY - LƯU TRỮ QUA CHẾ ĐỘ DEEP SLEEP
 * ============================================================================ */

/* Biến lưu trong RTC Memory (giữ giá trị qua deep sleep) */
RTC_DATA_ATTR uint32_t TotalElapsedTime_u32 = 0u;   /* Tổng thời gian từ lúc bắt đầu (giây) */
RTC_DATA_ATTR uint32_t BootCount_u32 = 0u;          /* Số lần khởi động */

/* ============================================================================
 * KHAI BÁO HÀM CỤC BỘ (Local Functions)
 * ============================================================================ */

static void Sensor_Init(void);
static void Sensor_Warmup(void);
static void Sensor_ReadData(void);
static void Sd_WriteData(void);
static void Oled_DisplayData(void);
static void Oled_DisplaySleepMessage(void);
static void Serial_PrintData(void);
static void DeepSleep_Enter(void);

/* ============================================================================
 * TÊN HÀM: Sensor_Init
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Khởi tạo tất cả các cảm biến và thiết bị ngoại vi
 *        - PMS7003 (qua UART)
 *        - BME280 (qua I2C)
 *        - OLED Display (qua I2C)
 *        - SD Card (qua SPI)
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Sensor_Init(void)
{
    /* ========================================================================
     * 1. KHỞI TẠO UART CHO PMS7003
     * ======================================================================== */
    Serial.println(F(""));
    Serial.println(F("================================="));
    Serial.println(F("Khoi tao cam bien PMS7003..."));
    
    /* Cấu hình UART2: baudrate 9600, 8 data bits, No parity, 1 stop bit */
    PmsSerial_obj.begin(9600u, SERIAL_8N1, PMS_RX_PIN_U8, PMS_TX_PIN_U8);
    
    /* Đánh thức PMS7003 (thoát chế độ sleep nếu có) */
    PmsSensor_obj.wakeUp();
    delay(100u);
    
    IsPmsReady_bool = true;
    Serial.println(F("  [OK] PMS7003 da khoi tao"));

    /* ========================================================================
     * 2. KHỞI TẠO I2C CHO BME280 VÀ OLED
     * ======================================================================== */
    Serial.println(F("Khoi tao I2C bus..."));
    
    Wire.begin();                /* Khởi tạo I2C với chân mặc định */
    Wire.setClock(100000u);      /* Tốc độ I2C: 100kHz (an toàn) */
    
    Serial.println(F("  [OK] I2C bus da khoi tao"));

    /* ========================================================================
     * 3. KHỞI TẠO MÀN HÌNH OLED
     * ======================================================================== */
    Serial.println(F("Khoi tao man hinh OLED..."));
    
    if (OledDisplay_obj.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS_U8) == false)
    {
        Serial.println(F("  [LOI] OLED khoi tao that bai!"));
        IsOledReady_bool = false;
    }
    else
    {
        Serial.println(F("  [OK] OLED da khoi tao"));
        IsOledReady_bool = true;
        
        /* Xóa màn hình và hiển thị thông báo khởi tạo */
        OledDisplay_obj.clearDisplay();
        OledDisplay_obj.setTextSize(1);
        OledDisplay_obj.setTextColor(SSD1306_WHITE);
        OledDisplay_obj.setCursor(0, 0);
        OledDisplay_obj.println(F("INITIALIZING..."));
        OledDisplay_obj.display();
    }

    /* ========================================================================
     * 4. KHỞI TẠO CẢM BIẾN BME280
     * ======================================================================== */
    Serial.println(F("Khoi tao cam bien BME280..."));
    
    if (BmeSensor_obj.begin(BME280_I2C_ADDRESS_U8) == false)
    {
        Serial.println(F("  [LOI] BME280 khoi tao that bai!"));
        IsBmeReady_bool = false;
    }
    else
    {
        Serial.println(F("  [OK] BME280 da khoi tao"));
        IsBmeReady_bool = true;
    }

    /* ========================================================================
     * 5. KHỞI TẠO THẺ NHỚ SD
     * ======================================================================== */
    Serial.println(F("Khoi tao the nho SD..."));
    
    if (SD.begin(SD_CS_PIN_U8) == false)
    {
        Serial.println(F("  [LOI] SD card khoi tao that bai!"));
        IsSdCardReady_bool = false;
    }
    else
    {
        Serial.println(F("  [OK] SD card da khoi tao"));
        IsSdCardReady_bool = true;
    }

    Serial.println(F("================================="));
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: Sensor_Warmup
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Đếm ngược 30 giây để PMS7003 khởi động và ổn định
 *        - Hiển thị đếm ngược trên Serial
 *        - Hiển thị đếm ngược trên OLED
 *        - Cập nhật mỗi giây
 *        - Không dùng delay() để có thể làm việc khác trong lúc chờ
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Sensor_Warmup(void)
{
    uint32_t warmupStartTime_u32 = 0u;
    uint32_t currentTime_u32 = 0u;
    uint32_t elapsedSeconds_u32 = 0u;
    uint32_t lastDisplayedSecond_u32 = WARMUP_DURATION_S_U32 + 1u;  /* Giá trị khởi tạo lớn hơn max */
    int32_t remainingTime_s32 = 0;
    
    Serial.println(F("================================="));
    Serial.println(F("WARM-UP PMS7003 - 30 GIAY"));
    Serial.println(F("================================="));
    Serial.println(F(""));
    
    warmupStartTime_u32 = millis();
    
    /* Vòng lặp đếm ngược 30 giây */
    while ((millis() - warmupStartTime_u32) < WARMUP_DURATION_MS_U32)
    {
        currentTime_u32 = millis();
        
        /* Tính số giây đã trôi qua */
        elapsedSeconds_u32 = (currentTime_u32 - warmupStartTime_u32) / 1000u;
        
        /* Tính thời gian còn lại */
        remainingTime_s32 = (int32_t)WARMUP_DURATION_S_U32 - (int32_t)elapsedSeconds_u32;
        
        /* Chỉ cập nhật khi giây thay đổi */
        if (remainingTime_s32 != (int32_t)lastDisplayedSecond_u32)
        {
            lastDisplayedSecond_u32 = (uint32_t)remainingTime_s32;
            
            /* In ra Serial */
            Serial.print(F("Warm-up: "));
            Serial.print(remainingTime_s32);
            Serial.println(F(" giay con lai..."));
            
            /* Hiển thị lên OLED nếu có */
            if (IsOledReady_bool == true)
            {
                OledDisplay_obj.clearDisplay();
                OledDisplay_obj.setTextSize(2);
                OledDisplay_obj.setTextColor(SSD1306_WHITE);
                
                /* Tiêu đề */
                OledDisplay_obj.setCursor(0, 0);
                OledDisplay_obj.println(F("WARM-UP"));
                
                /* Thông báo */
                OledDisplay_obj.setTextSize(1);
                OledDisplay_obj.setCursor(0, 20);
                OledDisplay_obj.println(F("PMS7003"));
                OledDisplay_obj.println(F("Starting..."));
                
                /* Đếm ngược */
                OledDisplay_obj.setTextSize(2);
                OledDisplay_obj.setCursor(0, 45);
                OledDisplay_obj.print(remainingTime_s32);
                OledDisplay_obj.println(F(" sec"));
                
                OledDisplay_obj.display();
            }
        }
        
        /* Có thể thêm các tác vụ khác ở đây nếu cần */
    }
    
    Serial.println(F(""));
    Serial.println(F("[OK] Warm-up hoan thanh!"));
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: Sensor_ReadData
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Đọc dữ liệu từ tất cả các cảm biến
 *        - PMS7003: Nồng độ bụi PM1.0, PM2.5, PM10
 *        - BME280: Nhiệt độ, độ ẩm, áp suất, độ cao
 *        - Lưu timestamp hiện tại
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Sensor_ReadData(void)
{
    uint8_t retryCount_u8 = 0u;
    bool pmsSuccess_bool = false;
    
    Serial.println(F("Dang doc du lieu cam bien..."));

    /* ========================================================================
     * 1. ĐỌC DỮ LIỆU TỪ PMS7003
     * ======================================================================== */
    
    if (IsPmsReady_bool == true)
    {
        /* Thử đọc dữ liệu PMS với 5 lần thử */
        for (retryCount_u8 = 0u; retryCount_u8 < 5u; retryCount_u8++)
        {
            if (PmsSensor_obj.readUntil(st_PmsData, 2000u) == true)
            {
                /* Đọc thành công */
                st_CurrentSensorData.pm1_u16 = st_PmsData.PM_AE_UG_1_0;
                st_CurrentSensorData.pm25_u16 = st_PmsData.PM_AE_UG_2_5;
                st_CurrentSensorData.pm10_u16 = st_PmsData.PM_AE_UG_10_0;
                pmsSuccess_bool = true;
                Serial.println(F("  [OK] Doc PMS7003 thanh cong"));
                break;
            }
            
            delay(200u);
        }
        
        if (pmsSuccess_bool == false)
        {
            Serial.println(F("  [LOI] Doc PMS7003 that bai sau 5 lan thu"));
            IsPmsReady_bool = false;
        }
    }
    else
    {
        Serial.println(F("  [SKIP] PMS7003 khong san sang"));
    }

    /* ========================================================================
     * 2. ĐỌC DỮ LIỆU TỪ BME280
     * ======================================================================== */
    
    if (IsBmeReady_bool == true)
    {
        st_CurrentSensorData.temperature_f = BmeSensor_obj.readTemperature();
        st_CurrentSensorData.humidity_f = BmeSensor_obj.readHumidity();
        st_CurrentSensorData.pressure_f = BmeSensor_obj.readPressure() / 100.0f;
        st_CurrentSensorData.altitude_f = BmeSensor_obj.readAltitude(SEA_LEVEL_PRESSURE_HPA_F);
        
        Serial.println(F("  [OK] Doc BME280 thanh cong"));
    }
    else
    {
        Serial.println(F("  [SKIP] BME280 khong san sang"));
    }

    /* ========================================================================
     * 3. LƯU TIMESTAMP HIỆN TẠI
     * ======================================================================== */
    
    st_CurrentSensorData.timestamp_u32 = TotalElapsedTime_u32;
    
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: Sd_WriteData
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Ghi dữ liệu cảm biến vào file CSV trên SD card
 *        - Tạo header nếu file chưa tồn tại
 *        - Ghi dữ liệu dạng CSV với timestamp
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Sd_WriteData(void)
{
    File dataFile;
    char csvLine_au8[128];
    bool fileExists_bool = false;
    
    if (IsSdCardReady_bool == false)
    {
        Serial.println(F("SD card khong san sang, bo qua viec ghi du lieu"));
        Serial.println(F(""));
        return;
    }
    
    Serial.println(F("Dang ghi du lieu vao SD card..."));

    /* ========================================================================
     * 1. MỞ FILE DATA.CSV
     * ======================================================================== */
    
    /* Kiểm tra file đã tồn tại chưa */
    fileExists_bool = SD.exists("/data.csv");
    
    /* Mở file ở chế độ append */
    dataFile = SD.open("/data.csv", FILE_APPEND);
    
    if (dataFile == false)
    {
        Serial.println(F("  [LOI] Khong mo duoc file data.csv"));
        IsSdCardReady_bool = false;
        Serial.println(F(""));
        return;
    }

    /* ========================================================================
     * 2. GHI HEADER NẾU FILE MỚI
     * ======================================================================== */
    
    if ((fileExists_bool == false) || (dataFile.size() == 0u))
    {
        Serial.println(F("  Tao header CSV moi..."));
        dataFile.println(F("Time(s),PM1.0,PM2.5,PM10,Temp(C),Hum(%),Pres(hPa),Alt(m)"));
    }

    /* ========================================================================
     * 3. GHI DỮ LIỆU
     * ======================================================================== */
    
    /* Tạo chuỗi CSV */
    snprintf(csvLine_au8, sizeof(csvLine_au8), "%lu,%d,%d,%d,%.2f,%.2f,%.2f,%.2f",
             st_CurrentSensorData.timestamp_u32,
             st_CurrentSensorData.pm1_u16,
             st_CurrentSensorData.pm25_u16,
             st_CurrentSensorData.pm10_u16,
             st_CurrentSensorData.temperature_f,
             st_CurrentSensorData.humidity_f,
             st_CurrentSensorData.pressure_f,
             st_CurrentSensorData.altitude_f);
    
    /* Ghi vào file */
    dataFile.println(csvLine_au8);
    dataFile.flush();
    dataFile.close();
    
    Serial.println(F("  [OK] Du lieu da duoc ghi thanh cong"));
    Serial.print(F("  Timestamp: "));
    Serial.print(st_CurrentSensorData.timestamp_u32);
    Serial.println(F(" giay"));
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: Oled_DisplayData
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Hiển thị dữ liệu cảm biến lên màn hình OLED
 *        Bao gồm: PM1.0, PM2.5, PM10, nhiệt độ, độ ẩm, áp suất, độ cao
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Oled_DisplayData(void)
{
    if (IsOledReady_bool == false)
    {
        Serial.println(F("OLED khong san sang, bo qua hien thi"));
        return;
    }
    
    Serial.println(F("Dang hien thi du lieu len OLED..."));

    /* Xóa màn hình */
    OledDisplay_obj.clearDisplay();
    OledDisplay_obj.setTextSize(1);
    OledDisplay_obj.setTextColor(SSD1306_WHITE);

    /* ========================================================================
     * DÒNG 1 - TIÊU ĐỀ
     * ======================================================================== */
    
    OledDisplay_obj.setCursor(0, 0);
    OledDisplay_obj.println(F("=== DATA ==="));

    /* ========================================================================
     * DÒNG 2-4 - DỮ LIỆU BỤI PM
     * ======================================================================== */
    
    OledDisplay_obj.setCursor(0, 12);
    OledDisplay_obj.print(F("PM1 :"));
    OledDisplay_obj.print(st_CurrentSensorData.pm1_u16);
    OledDisplay_obj.println(F(" ug/m3"));
    
    OledDisplay_obj.setCursor(0, 22);
    OledDisplay_obj.print(F("PM2.5:"));
    OledDisplay_obj.print(st_CurrentSensorData.pm25_u16);
    OledDisplay_obj.println(F(" ug/m3"));
    
    OledDisplay_obj.setCursor(0, 32);
    OledDisplay_obj.print(F("PM10 :"));
    OledDisplay_obj.print(st_CurrentSensorData.pm10_u16);
    OledDisplay_obj.println(F(" ug/m3"));

    /* ========================================================================
     * DÒNG 5 - NHIỆT ĐỘ VÀ ĐỘ ẨM
     * ======================================================================== */
    
    OledDisplay_obj.setCursor(0, 42);
    OledDisplay_obj.print(F("T:"));
    OledDisplay_obj.print(st_CurrentSensorData.temperature_f, 1);
    OledDisplay_obj.print(F("C H:"));
    OledDisplay_obj.print(st_CurrentSensorData.humidity_f, 0);
    OledDisplay_obj.print(F("%"));

    /* ========================================================================
     * DÒNG 6 - ÁP SUẤT VÀ ĐỘ CAO
     * ======================================================================== */
    
    OledDisplay_obj.setCursor(0, 52);
    OledDisplay_obj.print(F("P:"));
    OledDisplay_obj.print(st_CurrentSensorData.pressure_f, 0);
    OledDisplay_obj.print(F(" A:"));
    OledDisplay_obj.print(st_CurrentSensorData.altitude_f, 0);

    /* Cập nhật màn hình */
    OledDisplay_obj.display();
    
    Serial.println(F("  [OK] Hien thi OLED thanh cong"));
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: Oled_DisplaySleepMessage
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Hiển thị thông báo "SLEEPING" lên OLED trước khi vào deep sleep
 *        Bao gồm số lần boot và tổng thời gian
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Oled_DisplaySleepMessage(void)
{
    if (IsOledReady_bool == false)
    {
        return;
    }
    
    Serial.println(F("Dang hien thi thong bao ngu..."));

    /* Xóa màn hình */
    OledDisplay_obj.clearDisplay();

    /* ========================================================================
     * DÒNG 1 - THÔNG BÁO "SLEEPING"
     * ======================================================================== */
    
    OledDisplay_obj.setTextSize(2);
    OledDisplay_obj.setCursor(0, 0);
    OledDisplay_obj.println(F("SLEEPING"));

    /* ========================================================================
     * DÒNG 2 - THỜI GIAN NGỦ
     * ======================================================================== */
    
    OledDisplay_obj.setTextSize(1);
    OledDisplay_obj.setCursor(0, 20);
    OledDisplay_obj.println(F("15 minutes"));

    /* ========================================================================
     * DÒNG 3 - COUNTDOWN
     * ======================================================================== */
    
    OledDisplay_obj.setTextSize(2);
    OledDisplay_obj.setCursor(0, 35);
    OledDisplay_obj.print(DEEP_SLEEP_DURATION_S_U32);
    OledDisplay_obj.println(F(" sec"));

    /* ========================================================================
     * DÒNG 4 - THÔNG TIN BOOT VÀ THỜI GIAN
     * ======================================================================== */
    
    OledDisplay_obj.setTextSize(1);
    OledDisplay_obj.setCursor(0, 56);
    OledDisplay_obj.print(F("B:"));
    OledDisplay_obj.print(BootCount_u32);
    OledDisplay_obj.print(F(" T:"));
    OledDisplay_obj.print(TotalElapsedTime_u32);
    OledDisplay_obj.print(F("s"));

    /* Cập nhật màn hình */
    OledDisplay_obj.display();
    
    Serial.println(F("  [OK] Hien thi thong bao thanh cong"));
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: Serial_PrintData
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: In dữ liệu cảm biến ra Serial Monitor để debug
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Serial_PrintData(void)
{
    Serial.println(F("========================================"));
    Serial.println(F("--- DU LIEU CAM BIEN ---"));
    Serial.println(F("----------------------------------------"));
    
    Serial.print(F("Timestamp: "));
    Serial.print(st_CurrentSensorData.timestamp_u32);
    Serial.println(F(" giay"));
    
    Serial.println(F("----------------------------------------"));
    
    Serial.print(F("PM1.0 : "));
    Serial.print(st_CurrentSensorData.pm1_u16);
    Serial.println(F(" ug/m3"));
    
    Serial.print(F("PM2.5 : "));
    Serial.print(st_CurrentSensorData.pm25_u16);
    Serial.println(F(" ug/m3"));
    
    Serial.print(F("PM10  : "));
    Serial.print(st_CurrentSensorData.pm10_u16);
    Serial.println(F(" ug/m3"));
    
    Serial.println(F("----------------------------------------"));
    
    Serial.print(F("Nhiet do: "));
    Serial.print(st_CurrentSensorData.temperature_f);
    Serial.println(F(" C"));
    
    Serial.print(F("Do am   : "));
    Serial.print(st_CurrentSensorData.humidity_f);
    Serial.println(F(" %"));
    
    Serial.print(F("Ap suat : "));
    Serial.print(st_CurrentSensorData.pressure_f);
    Serial.println(F(" hPa"));
    
    Serial.print(F("Do cao  : "));
    Serial.print(st_CurrentSensorData.altitude_f);
    Serial.println(F(" m"));
    
    Serial.println(F("========================================"));
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: DeepSleep_Enter
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Chuẩn bị và đi vào chế độ deep sleep
 *        - Hiển thị thông báo sleep trên OLED
 *        - Tắt OLED
 *        - Cho PMS7003 vào chế độ sleep
 *        - Cấu hình timer wakeup
 *        - Vào deep sleep
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void (không bao giờ trả về vì ESP32 sẽ reset sau khi tỉnh dậy)
 * ============================================================================ */
static void DeepSleep_Enter(void)
{
    Serial.println(F("================================="));
    Serial.println(F("CHUAN BI VAO DEEP SLEEP"));
    Serial.println(F("================================="));

    /* ========================================================================
     * 1. HIỂN THỊ THÔNG BÁO SLEEP
     * ======================================================================== */
    
    Oled_DisplaySleepMessage();
    delay(SLEEP_DISPLAY_DURATION_MS_U32);

    /* ========================================================================
     * 2. TẮT OLED
     * ======================================================================== */
    
    if (IsOledReady_bool == true)
    {
        OledDisplay_obj.ssd1306_command(SSD1306_DISPLAYOFF);
        Serial.println(F("OLED da tat"));
    }

    /* ========================================================================
     * 3. CHO PMS7003 VÀO CHẾ ĐỘ SLEEP
     * ======================================================================== */
    
    if (IsPmsReady_bool == true)
    {
        PmsSensor_obj.sleep();
        delay(100u);
        Serial.println(F("PMS7003 da vao che do sleep"));
    }

    /* ========================================================================
     * 4. CẤU HÌNH TIMER WAKEUP
     * ======================================================================== */
    
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_US_U64);
    
    Serial.print(F("Se thuc day sau "));
    Serial.print(DEEP_SLEEP_DURATION_S_U32);
    Serial.println(F(" giay (15 phut)"));

    /* ========================================================================
     * 5. IN THÔNG TIN TỔNG KẾT
     * ======================================================================== */
    
    Serial.println(F(""));
    Serial.println(F("--- THONG TIN TONG KET ---"));
    Serial.print(F("Lan khoi dong thu: "));
    Serial.println(BootCount_u32);
    Serial.print(F("Tong thoi gian da troi: "));
    Serial.print(TotalElapsedTime_u32);
    Serial.println(F(" giay"));
    Serial.print(F("Thoi diem lay mau tiep theo: "));
    Serial.print(TotalElapsedTime_u32 + DEEP_SLEEP_DURATION_S_U32 + WARMUP_DURATION_S_U32);
    Serial.println(F(" giay"));
    Serial.println(F(""));

    /* ========================================================================
     * 6. VÀO DEEP SLEEP
     * ======================================================================== */
    
    Serial.println(F("DANG VAO DEEP SLEEP..."));
    Serial.println(F("================================="));
    Serial.println(F(""));
    Serial.flush();
    
    /* Đóng các cổng Serial */
    PmsSerial_obj.end();
    Serial.end();
    
    delay(100u);

    /* Vào deep sleep - ESP32 sẽ reset khi tỉnh dậy */
    esp_deep_sleep_start();
    
    /* Không bao giờ đến đây */
}

/* ============================================================================
 * TÊN HÀM: setup
 * ============================================================================
 * MÔ TẢ: Hàm khởi tạo Arduino (chạy 1 lần khi bật nguồn hoặc tỉnh dậy)
 *        Thực hiện toàn bộ quy trình:
 *        1. Cập nhật bộ đếm và thời gian
 *        2. Khởi tạo phần cứng
 *        3. Warm-up PMS7003 (30 giây)
 *        4. Đọc dữ liệu cảm biến (1 giây)
 *        5. Ghi dữ liệu vào SD
 *        6. Hiển thị dữ liệu (3 giây)
 *        7. Vào deep sleep (15 phút)
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
void setup(void)
{
    uint32_t cycleStartTime_u32 = 0u;
    uint32_t cycleTime_u32 = 0u;

    /* ========================================================================
     * 1. TĂNG BỘ ĐẾM BOOT VÀ CẬP NHẬT THỜI GIAN
     * ======================================================================== */
    
    BootCount_u32++;
    
    /* Chỉ cộng thêm thời gian sleep nếu không phải lần boot đầu tiên */
    if (BootCount_u32 > 1u)
    {
        TotalElapsedTime_u32 += DEEP_SLEEP_DURATION_S_U32;
    }

    /* ========================================================================
     * 2. KHỞI TẠO SERIAL MONITOR
     * ======================================================================== */
    
    Serial.begin(115200u);
    delay(500u);  /* Đợi Serial ổn định */
    
    Serial.println(F(""));
    Serial.println(F(""));
    Serial.println(F("========================================"));
    Serial.println(F("   AIR QUALITY MONITOR - DEEP SLEEP   "));
    Serial.println(F("========================================"));
    Serial.print(F("Boot #"));
    Serial.print(BootCount_u32);
    Serial.print(F(" | Total Elapsed: "));
    Serial.print(TotalElapsedTime_u32);
    Serial.println(F("s"));
    Serial.println(F(""));

    /* Lưu thời điểm bắt đầu chu kỳ */
    cycleStartTime_u32 = millis();

    /* ========================================================================
     * 3. KHỞI TẠO PHẦN CỨNG
     * ======================================================================== */
    
    Sensor_Init();

    /* ========================================================================
     * 4. WARM-UP PMS7003 (30 GIÂY ĐẾM NGƯỢC)
     * ======================================================================== */
    
    if (IsPmsReady_bool == true)
    {
        Sensor_Warmup();
    }
    else
    {
        Serial.println(F("Bo qua warm-up (PMS7003 khong san sang)"));
        Serial.println(F(""));
    }

    /* ========================================================================
     * 5. ĐỌC DỮ LIỆU CẢM BIẾN (1 GIÂY)
     * ======================================================================== */
    
    Sensor_ReadData();

    /* ========================================================================
     * 6. IN DỮ LIỆU RA SERIAL
     * ======================================================================== */
    
    Serial_PrintData();

    /* ========================================================================
     * 7. GHI DỮ LIỆU VÀO SD CARD
     * ======================================================================== */
    
    Sd_WriteData();

    /* ========================================================================
     * 8. HIỂN THỊ DỮ LIỆU LÊN OLED (3 GIÂY)
     * ======================================================================== */
    
    Oled_DisplayData();
    delay(OLED_DISPLAY_DURATION_MS_U32);

    /* ========================================================================
     * 9. TÍNH TOÁN THỜI GIAN CHU KỲ VÀ CẬP NHẬT
     * ======================================================================== */
    
    /* Tính thời gian chu kỳ (bao gồm cả thời gian hiển thị sleep message) */
    cycleTime_u32 = ((millis() - cycleStartTime_u32) + SLEEP_DISPLAY_DURATION_MS_U32) / 1000u;
    TotalElapsedTime_u32 += cycleTime_u32;
    
    Serial.println(F("--- TONG KET CHU KY ---"));
    Serial.print(F("Thoi gian chu ky: "));
    Serial.print(cycleTime_u32);
    Serial.println(F(" giay"));
    Serial.print(F("Tong thoi gian: "));
    Serial.print(TotalElapsedTime_u32);
    Serial.println(F(" giay"));
    Serial.println(F(""));

    /* ========================================================================
     * 10. VÀO DEEP SLEEP
     * ======================================================================== */
    
    DeepSleep_Enter();
    
    /* Không bao giờ đến đây */
}

/* ============================================================================
 * TÊN HÀM: loop
 * ============================================================================
 * MÔ TẢ: Vòng lặp chính của Arduino
 *        Không được sử dụng trong chế độ Deep Sleep
 *        ESP32 sẽ reset và chạy lại setup() khi tỉnh dậy
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
void loop(void)
{
    /* Không sử dụng trong chế độ Deep Sleep */
    /* ESP32 sẽ chạy setup() sau mỗi lần tỉnh dậy */
}

/* ============================================================================
 * KẾT THÚC FILE
 * ============================================================================ */
