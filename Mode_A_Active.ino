/*
 * ============================================================================
 * TÊN FILE CODE: Mode A - Active Monitoring
 * ============================================================================
 * MÔ TẢ: Thiết bị đo chất lượng không khí sử dụng:
 *        - Cảm biến bụi PMS7003
 *        - Cảm biến nhiệt độ/độ ẩm/áp suất BME280
 *        - Màn hình OLED SSD1306
 *        - Thẻ nhớ SD để lưu dữ liệu
 *        - Pin có thể sạc lại
 * CHỨC NĂNG:
 *        1. Đo dữ liệu cảm biến mỗi 1 giây
 *        2. Hiển thị dữ liệu lên OLED và Serial
 *        3. Lưu dữ liệu vào RAM buffer
 *        4. Ghi toàn bộ buffer vào SD card mỗi 300 giây (5 phút)
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

/* Khoảng thời gian đo dữ liệu cảm biến */
const uint32_t SENSOR_READ_INTERVAL_MS_U32 = 1000u;    /* Đo mỗi 1 giây */

/* Khoảng thời gian ghi dữ liệu vào SD card */
const uint32_t SD_WRITE_INTERVAL_MS_U32 = 300000u;     /* Ghi mỗi 300 giây (5 phút) */

/* Thời gian hiển thị thông báo "SAVED!" */
const uint32_t SAVED_DISPLAY_DURATION_MS_U32 = 3000u;  /* Hiển thị 3 giây */

/* Khoảng thời gian cập nhật màn hình OLED */
const uint32_t OLED_UPDATE_INTERVAL_MS_U32 = 500u;     /* Cập nhật mỗi 0.5 giây */

/* ============================================================================
 * ĐỊNH NGHĨA HẰNG SỐ - CẤU HÌNH BUFFER
 * ============================================================================ */

/* Giới hạn kích thước buffer để tránh tràn bộ nhớ RAM */
const uint16_t MAX_BUFFER_SIZE_U16 = 12288u;      /* Giới hạn 12KB cho buffer */

/* ============================================================================
 * ĐỊNH NGHĨA HẰNG SỐ - THÔNG SỐ BME280
 * ============================================================================ */
const uint8_t BME280_I2C_ADDRESS_U8 = 0x76u;     /* Địa chỉ I2C của BME280 */
const float SEA_LEVEL_PRESSURE_HPA_F = 1013.25f; /* Áp suất mực nước biển (hPa) */

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
 * BIẾN TOÀN CỤC - QUẢN LÝ THỜI GIAN (PascalCase theo convention)
 * ============================================================================ */

/* Thời điểm đọc dữ liệu cảm biến gần nhất */
uint32_t LastSensorReadTime_u32 = 0u;

/* Thời điểm ghi dữ liệu vào SD gần nhất */
uint32_t LastSdWriteTime_u32 = 0u;

/* Thời điểm cập nhật OLED gần nhất */
uint32_t LastOledUpdateTime_u32 = 0u;

/* Thời điểm bắt đầu hiển thị thông báo "SAVED!" */
uint32_t SavedNotificationStartTime_u32 = 0u;

/* ============================================================================
 * BIẾN TOÀN CỤC - LƯU TRỮ DỮ LIỆU CẢM BIẾN (PascalCase)
 * ============================================================================ */

/* Dữ liệu từ cảm biến bụi PMS7003 (đơn vị: µg/m³) */
uint16_t CurrentPm1_u16 = 0u;    /* Nồng độ bụi PM1.0 */
uint16_t CurrentPm25_u16 = 0u;   /* Nồng độ bụi PM2.5 */
uint16_t CurrentPm10_u16 = 0u;   /* Nồng độ bụi PM10 */

/* Dữ liệu từ cảm biến BME280 */
float CurrentTemperature_f = 0.0f;   /* Nhiệt độ (°C) */
uint32_t CurrentPressure_u32 = 0u;   /* Áp suất (Pa) */
float CurrentAltitude_f = 0.0f;      /* Độ cao (m) */
uint8_t CurrentHumidity_u8 = 0u;     /* Độ ẩm (%) */

/* ============================================================================
 * BIẾN TOÀN CỤC - QUẢN LÝ BUFFER VÀ TRẠNG THÁI (PascalCase)
 * ============================================================================ */

/* Buffer lưu trữ dữ liệu CSV trong RAM trước khi ghi SD */
String DataBuffer_string = "";

/* Số dòng dữ liệu hiện có trong buffer */
uint16_t BufferLineCount_u16 = 0u;

/* Cờ kiểm tra SD card đã khởi tạo thành công hay chưa */
bool IsSdCardReady_bool = false;

/* Cờ hiển thị thông báo "SAVED!" */
bool ShowSavedNotification_bool = false;

/* Số lần đo dữ liệu (dùng để đếm đến 300 giây) */
uint16_t MeasurementCount_u16 = 0u;

/* ============================================================================
 * KHAI BÁO HÀM CỤC BỘ (Local Functions)
 * ============================================================================ */

static void Sensor_Init(void);
static void Sensor_ReadData(void);
static void Buffer_AppendData(void);
static void Sd_WriteBuffer(void);
static void Oled_DisplayData(void);
static void Serial_PrintData(void);

/* ============================================================================
 * TÊN HÀM: Sensor_Init
 * ============================================================================
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
        Serial.println(F("  He thong dung lai."));
        
        /* Dừng chương trình nếu OLED không hoạt động */
        for (;;)
        {
            /* Vòng lặp vô tận */
        }
    }
    
    Serial.println(F("  [OK] OLED da khoi tao"));
    
    /* Hiển thị thông báo khởi động lên OLED */
    OledDisplay_obj.clearDisplay();
    OledDisplay_obj.setTextSize(1);
    OledDisplay_obj.setTextColor(SSD1306_WHITE);
    OledDisplay_obj.setCursor(0, 0);
    OledDisplay_obj.println(F("Air Quality"));
    OledDisplay_obj.println(F("Monitor System"));
    OledDisplay_obj.println(F(""));
    OledDisplay_obj.println(F("Dang khoi tao..."));
    OledDisplay_obj.display();

    /* ========================================================================
     * 4. KHỞI TẠO CẢM BIẾN BME280
     * ======================================================================== */
    Serial.println(F("Khoi tao cam bien BME280..."));
    
    if (BmeSensor_obj.begin(BME280_I2C_ADDRESS_U8) == false)
    {
        Serial.println(F("  [CANH BAO] BME280 khong phat hien!"));
        Serial.println(F("  He thong se tiep tuc nhung khong co du lieu BME280."));
    }
    else
    {
        Serial.println(F("  [OK] BME280 da khoi tao"));
    }

    /* ========================================================================
     * 5. KHỞI TẠO THẺ NHỚ SD CARD
     * ======================================================================== */
    Serial.println(F("Khoi tao the nho SD card..."));
    
    if (SD.begin(SD_CS_PIN_U8) == false)
    {
        Serial.println(F("  [LOI] SD card khoi tao that bai!"));
        IsSdCardReady_bool = false;
    }
    else
    {
        Serial.println(F("  [OK] SD card da khoi tao"));
        IsSdCardReady_bool = true;

        /* Kiểm tra nếu file chưa tồn tại hoặc rỗng thì ghi tiêu đề */
        if (!SD.exists("/datalog.csv") || SD.open("/datalog.csv").size() == 0)
        {
            File tempFile = SD.open("/datalog.csv", FILE_WRITE);
            if (tempFile)
            {
                Serial.println(F("  Ghi dong tieu de CSV..."));
                tempFile.println(F("Time(s),PM1.0,PM2.5,PM10,Temp(C),Press(Pa),Alt(m),Humi(%)"));
                tempFile.close();
            }
        }
    }

    /* ========================================================================
     * 6. CẤP PHÁT BỘ NHỚ CHO BUFFER
     * ======================================================================== */
    Serial.println(F("Cap phat bo nho cho buffer..."));
    DataBuffer_string.reserve(MAX_BUFFER_SIZE_U16);
    Serial.println(F("  [OK] Buffer da cap phat"));

    Serial.println(F("================================="));
    Serial.println(F("Khoi tao hoan tat!"));
    Serial.println(F(""));
    
    /* Cập nhật màn hình OLED */
    OledDisplay_obj.clearDisplay();
    OledDisplay_obj.setCursor(0, 0);
    OledDisplay_obj.println(F("Khoi tao xong!"));
    OledDisplay_obj.println(F(""));
    OledDisplay_obj.println(F("Bat dau do du lieu..."));
    OledDisplay_obj.display();
    
    delay(2000u);  /* Hiển thị thông báo 2 giây */
}

/* ============================================================================
 * TÊN HÀM: Sensor_ReadData
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Đọc dữ liệu từ tất cả các cảm biến:
 *        - PMS7003: Nồng độ bụi PM1.0, PM2.5, PM10
 *        - BME280: Nhiệt độ, áp suất, độ cao, độ ẩm
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Sensor_ReadData(void)
{
    /* ========================================================================
     * ĐỌC DỮ LIỆU TỪ PMS7003
     * ======================================================================== */
    
    /* Đọc dữ liệu từ PMS7003 với timeout 1000ms */
    if (PmsSensor_obj.readUntil(st_PmsData, 1000u) == true)
    {
        /* Lấy giá trị PM (atmospheric environment) */
        CurrentPm1_u16 = st_PmsData.PM_AE_UG_1_0;
        CurrentPm25_u16 = st_PmsData.PM_AE_UG_2_5;
        CurrentPm10_u16 = st_PmsData.PM_AE_UG_10_0;
    }
    else
    {
        /* Nếu không đọc được dữ liệu, giữ nguyên giá trị cũ */
        Serial.println(F("[CANH BAO] Khong doc duoc du lieu PMS7003"));
    }

    /* ========================================================================
     * ĐỌC DỮ LIỆU TỪ BME280
     * ======================================================================== */
    
    /* Đọc nhiệt độ (°C) */
    CurrentTemperature_f = BmeSensor_obj.readTemperature();
    
    /* Đọc áp suất (Pa) */
    CurrentPressure_u32 = (uint32_t)BmeSensor_obj.readPressure();
    
    /* Đọc độ cao ước tính dựa trên áp suất (m) */
    CurrentAltitude_f = BmeSensor_obj.readAltitude(SEA_LEVEL_PRESSURE_HPA_F);
    
    /* Đọc độ ẩm (%) */
    CurrentHumidity_u8 = (uint8_t)BmeSensor_obj.readHumidity();
}

/* ============================================================================
 * TÊN HÀM: Buffer_AppendData
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Thêm dữ liệu đo được vào buffer RAM dưới dạng CSV
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Buffer_AppendData(void)
{
    /* ========================================================================
     * TẠO CHUỖI CSV TỪ DỮ LIỆU CẢM BIẾN
     * ======================================================================== */
    
    /* Tạo buffer tạm để format chuỗi CSV */
    char csvLine_a8[150];
    
    /* Tính thời gian hiện tại (giây) */
    uint32_t currentTimeSeconds_u32 = millis() / 1000u;
    
    /* Format chuỗi CSV với snprintf (an toàn hơn String concatenation) */
    snprintf(csvLine_a8, sizeof(csvLine_a8),
             "%lu,%u,%u,%u,%.2f,%lu,%.1f,%u",
             currentTimeSeconds_u32,
             CurrentPm1_u16,
             CurrentPm25_u16,
             CurrentPm10_u16,
             CurrentTemperature_f,
             CurrentPressure_u32,
             CurrentAltitude_f,
             CurrentHumidity_u8);

    /* ========================================================================
     * THÊM DỮ LIỆU VÀO BUFFER
     * ======================================================================== */
    
    DataBuffer_string += String(csvLine_a8);
    DataBuffer_string += '\n';  /* Thêm ký tự xuống dòng */
    BufferLineCount_u16++;

    /* In thông tin ra Serial để debug */
    Serial.print(F("Buffer: "));
    Serial.print(BufferLineCount_u16);
    Serial.print(F(" dong, "));
    Serial.print(DataBuffer_string.length());
    Serial.println(F(" bytes"));
}

/* ============================================================================
 * TÊN HÀM: Sd_WriteBuffer
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Ghi toàn bộ dữ liệu từ buffer RAM vào thẻ SD
 *        Sau khi ghi xong, xóa buffer và reset bộ đếm
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Sd_WriteBuffer(void)
{
    /* 1. Kiểm tra điều kiện ghi */
    if (BufferLineCount_u16 == 0u || IsSdCardReady_bool == false)
    {
        return;
    }

    /* 2. Mở file ở chế độ ghi tiếp (FILE_APPEND) */
    File dataFile = SD.open("/datalog.csv", FILE_APPEND);

    if (dataFile)
    {
        Serial.println(F(">>> Dang ghi du lieu vao SD..."));
        
        /* 3. Ghi dữ liệu từ buffer */
        size_t bytesWritten_u32 = dataFile.print(DataBuffer_string);

        if (bytesWritten_u32 > 0u)
        {
            dataFile.close(); // ĐÓNG FILE NGAY LẬP TỨC
            
            Serial.print(F(">>> [THANH CONG] Da ghi & dong file."));
            
            /* Xóa buffer sau khi ghi thành công và đếm lại từ đầu */
            DataBuffer_string = "";
            DataBuffer_string.reserve(MAX_BUFFER_SIZE_U16);
            BufferLineCount_u16 = 0;
            /* Bật cờ thông báo */
            ShowSavedNotification_bool = true;
            SavedNotificationStartTime_u32 = millis();
        }
        else
        {
            Serial.println(F(">>> [LOI] Ghi du lieu that bai!"));
            dataFile.close(); // Luôn đóng file kể cả khi lỗi
        }
    }
    else
    {
        Serial.println(F(">>> [LOI] Khong the mo file de ghi!"));
    }
}

/* ============================================================================
 * TÊN HÀM: Oled_DisplayData
 * ============================================================================
 * PHẠM VI: Local (static)
 * 
 * MÔ TẢ: Hiển thị dữ liệu cảm biến lên màn hình OLED
 *        Bố cục: - Cột trái: PM1.0, PM2.5, PM10
 *                - Cột phải: Nhiệt độ, Độ ẩm, Áp suất, Độ cao
 *                - Dòng dưới: Thời gian chạy hoặc thông báo SAVED!
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
static void Oled_DisplayData(void)
{
    /* Xóa màn hình */
    OledDisplay_obj.clearDisplay();
    OledDisplay_obj.setTextSize(1);
    OledDisplay_obj.setTextColor(SSD1306_WHITE);

    /* ========================================================================
     * CỘT TRÁI - DỮ LIỆU BỤI PM
     * ======================================================================== */
    
    OledDisplay_obj.setCursor(0, 0);
    OledDisplay_obj.print(F("PM1 : "));
    OledDisplay_obj.print(CurrentPm1_u16);

    OledDisplay_obj.setCursor(0, 10);
    OledDisplay_obj.print(F("PM25: "));
    OledDisplay_obj.print(CurrentPm25_u16);

    OledDisplay_obj.setCursor(0, 20);
    OledDisplay_obj.print(F("PM10: "));
    OledDisplay_obj.print(CurrentPm10_u16);

    /* ========================================================================
     * CỘT PHẢI - DỮ LIỆU MÔI TRƯỜNG
     * ======================================================================== */
    
    /* Nhiệt độ */
    OledDisplay_obj.setCursor(68, 0);
    OledDisplay_obj.print(F("T:"));
    OledDisplay_obj.print(CurrentTemperature_f, 1);
    OledDisplay_obj.print('C');

    /* Độ ẩm */
    OledDisplay_obj.setCursor(68, 10);
    OledDisplay_obj.print(F("H:"));
    OledDisplay_obj.print(CurrentHumidity_u8);
    OledDisplay_obj.print('%');

    /* Áp suất (chuyển từ Pa sang hPa) */
    OledDisplay_obj.setCursor(68, 20);
    OledDisplay_obj.print(F("P:"));
    OledDisplay_obj.print(CurrentPressure_u32 / 100u);
    OledDisplay_obj.print('h');

    /* Độ cao */
    OledDisplay_obj.setCursor(68, 30);
    OledDisplay_obj.print(F("A:"));
    OledDisplay_obj.print(CurrentAltitude_f, 0);
    OledDisplay_obj.print('m');

    /* ========================================================================
     * ĐƯỜNG PHÂN CÁCH
     * ======================================================================== */
    
    OledDisplay_obj.drawLine(0, 42, 128, 42, SSD1306_WHITE);

    /* ========================================================================
     * DÒNG DƯỚI - THỜI GIAN HOẶC THÔNG BÁO
     * ======================================================================== */
    
    uint32_t currentTime_u32 = millis();

    /* Kiểm tra xem có đang hiển thị thông báo "SAVED!" không */
    if ((ShowSavedNotification_bool == true) && 
        ((currentTime_u32 - SavedNotificationStartTime_u32) < SAVED_DISPLAY_DURATION_MS_U32))
    {
        /* Hiển thị thông báo "SAVED!" */
        OledDisplay_obj.setCursor(0, 50);
        OledDisplay_obj.setTextSize(2);
        OledDisplay_obj.print(F("SAVED!"));
    }
    else
    {
        /* Tắt cờ thông báo nếu đã hết thời gian */
        if (ShowSavedNotification_bool == true)
        {
            ShowSavedNotification_bool = false;
        }

        /* Hiển thị thời gian chạy */
        OledDisplay_obj.setCursor(0, 50);
        OledDisplay_obj.setTextSize(1);
        OledDisplay_obj.print(F("Time: "));
        OledDisplay_obj.print(currentTime_u32 / 1000u);
        OledDisplay_obj.print('s');

        /* Hiển thị số lần đo */
        OledDisplay_obj.setCursor(0, 58);
        OledDisplay_obj.print(F("Count: "));
        OledDisplay_obj.print(MeasurementCount_u16);
        OledDisplay_obj.print(F("/300"));
    }

    /* Cập nhật màn hình */
    OledDisplay_obj.display();
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
    Serial.println(F("--- DU LIEU CAM BIEN ---"));
    
    Serial.print(F("Thoi gian: "));
    Serial.print(millis() / 1000u);
    Serial.println(F(" giay"));
    
    Serial.print(F("PM1.0 : "));
    Serial.print(CurrentPm1_u16);
    Serial.println(F(" ug/m3"));
    
    Serial.print(F("PM2.5 : "));
    Serial.print(CurrentPm25_u16);
    Serial.println(F(" ug/m3"));
    
    Serial.print(F("PM10  : "));
    Serial.print(CurrentPm10_u16);
    Serial.println(F(" ug/m3"));
    
    Serial.print(F("Nhiet do: "));
    Serial.print(CurrentTemperature_f);
    Serial.println(F(" *C"));
    
    Serial.print(F("Ap suat : "));
    Serial.print(CurrentPressure_u32);
    Serial.println(F(" Pa"));
    
    Serial.print(F("Do cao  : "));
    Serial.print(CurrentAltitude_f);
    Serial.println(F(" m"));
    
    Serial.print(F("Do am   : "));
    Serial.print(CurrentHumidity_u8);
    Serial.println(F(" %"));
    
    Serial.println(F("------------------------"));
}

/* ============================================================================
 * TÊN HÀM: setup
 * ============================================================================
 * MÔ TẢ: Hàm khởi tạo Arduino (chạy 1 lần khi bật nguồn)
 *        Thiết lập Serial, khởi tạo tất cả cảm biến và thiết bị
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
void setup(void)
{
    /* Khởi tạo Serial Monitor */
    Serial.begin(115200u);
    delay(1000u);  /* Đợi Serial ổn định */
    
    Serial.println(F(""));
    Serial.println(F("====================================="));
    Serial.println(F("   AIR QUALITY MONITOR   "));
    Serial.println(F("====================================="));
    
    /* Gọi hàm khởi tạo cảm biến */
    Sensor_Init();
    
    Serial.println(F("He thong san sang hoat dong!"));
    Serial.println(F("====================================="));
    Serial.println(F(""));
}

/* ============================================================================
 * TÊN HÀM: loop
 * ============================================================================
 * MÔ TẢ: Vòng lặp chính của Arduino (chạy liên tục)
 *        Quản lý 3 nhiệm vụ chính:
 *        1. Đọc dữ liệu cảm biến mỗi 1 giây
 *        2. Ghi dữ liệu vào SD mỗi 300 giây (300 lần đo)
 *        3. Cập nhật màn hình OLED mỗi 0.5 giây
 * 
 * THAM SỐ: void
 * 
 * TRẢ VỀ: void
 * ============================================================================ */
void loop(void)
{
    /* Lấy thời gian hiện tại */
    uint32_t currentTime_u32 = millis();

    /* ========================================================================
     * NHIỆM VỤ 1: ĐỌC DỮ LIỆU CẢM BIẾN (MỖI 1 GIÂY)
     * ======================================================================== */
    
    if ((currentTime_u32 - LastSensorReadTime_u32) >= SENSOR_READ_INTERVAL_MS_U32)
    {
        /* Cập nhật thời điểm đọc */
        LastSensorReadTime_u32 = currentTime_u32;

        /* Đọc dữ liệu từ cảm biến */
        Sensor_ReadData();

        /* In dữ liệu ra Serial */
        Serial_PrintData();

        /* Thêm dữ liệu vào buffer */
        Buffer_AppendData();

        /* Tăng bộ đếm số lần đo */
        MeasurementCount_u16++;

        /* ====================================================================
         * KIỂM TRA ĐỦ 300 LẦN ĐO (300 GIÂY) THÌ GHI SD
         * ==================================================================== */
        
        if (MeasurementCount_u16 >= 300u)
        {
            Serial.println(F(""));
            Serial.println(F("*** DA DU 300 GIAY - GHI DU LIEU VAO SD ***"));
            
            /* Ghi buffer vào SD */
            Sd_WriteBuffer();
            
            /* Reset bộ đếm */
            MeasurementCount_u16 = 0u;
            
            /* Cập nhật thời điểm ghi SD */
            LastSdWriteTime_u32 = currentTime_u32;
        }
    }

    /* ========================================================================
     * NHIỆM VỤ 2: CẬP NHẬT OLED (MỖI 0.5 GIÂY)
     * ======================================================================== */
    
    if ((currentTime_u32 - LastOledUpdateTime_u32) >= OLED_UPDATE_INTERVAL_MS_U32)
    {
        /* Cập nhật thời điểm vẽ OLED */
        LastOledUpdateTime_u32 = currentTime_u32;

        /* Hiển thị dữ liệu lên OLED */
        Oled_DisplayData();
    }
}

/* ============================================================================
 * KẾT THÚC FILE
 * ============================================================================ */
