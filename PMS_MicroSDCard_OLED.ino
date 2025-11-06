#include "PMS.h"
#include "HardwareSerial.h"

#include <freertos/FreeRTOS.h> 
#include <freertos/task.h> 

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include "time.h"

const char* ssid       = "Phong Bach Khoa mat chat";       // <-- THAY TÊN WIFI CỦA BẠN
const char* password   = "107107107";   // <-- THAY MẬT KHẨU WIFI

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600 * 7; // Múi giờ Việt Nam (GMT+7)
const int   daylightOffset_sec = 0;

// --- Cấu hình chân CS cho thẻ SD ---
#define SDCARD_CS 5

// PMS serial pins (ESP32)
const int PMS_RX_PIN = 16;  
const int PMS_TX_PIN = 17;  

//Khởi tạo giao tiếp UART cho PMS
HardwareSerial &PMS_SERIAL = Serial2;
PMS pms(PMS_SERIAL); 
PMS::DATA data;

//Thông số của màn hình
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

//Khởi tạo màn hình
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Thời gian
unsigned long previousSpinnerMillis = 0;
const long spinnerInterval = 500; // 500 ms cho spinner xoay mỗi lần

unsigned long previousDataMillis = 0;
const long dataInterval = 2000; // 2s cho data cấp nhật mỗi lần

// Biến lưu giá trị PM cuối cùng (để hiển thị khi chưa có gói mới)
uint16_t last_PM1 = 0;
uint16_t last_PM2_5 = 0;
uint16_t last_PM10 = 0;
// Sử dụng uint16_t vì giá trị bụi không âm, và hiện thị 16bit = 2bytes, đúng như gói dũ liệu từ datasheet của cảm biến

// Spinner frames
const char spinnerFrames[] = {'|', '/', '-', '\\'};
const int spinnerFrameCount = sizeof(spinnerFrames) / sizeof(spinnerFrames[0]);
int spinnerIndex = 0;

// hiển thị thông báo saved
bool showSaved = false;
unsigned long savedUntil = 0;
const unsigned long SAVED_DURATION_MS = 500; // 0.5 giây

// --- Ghi dữ liệu vào thẻ SD ---
void logToSDCard(String data) {
  // Mở file trên thẻ SD ở chế độ ghi tiếp (append)
  File dataFile = SD.open("/datalog.csv", FILE_APPEND);

  if (dataFile) {
    dataFile.println(data);
    dataFile.close();
    Serial.println("Ghi vào thẻ SD: " + data);
  } else {
    Serial.println("Lỗi khi mở file datalog.csv");
  }
}

void displayDataToSerial(){
  Serial.println("\n--- PMS DATA (new packet) ---");
  Serial.print("PM1.0: ");   Serial.println(last_PM1);
  Serial.print("PM2.5: ");   Serial.println(last_PM2_5);
  Serial.print("PM10: ");    Serial.println(last_PM10);
  Serial.println("----------------------------");
}

void setup()
{
  Serial.begin(115200);
  Serial.println("ESP32 khởi động. PMS7003 Khởi động. OLED khởi động");

  //Khởi tạo giao tiếp UART cho PMS tốc độ baud datasheet = 9600
  PMS_SERIAL.begin(9600, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);

  Wire.begin();
  Wire.setClock(100000); // an toàn: 100 kHz

  //Kiểm tra xem khởi tạo màn hình có được không
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found at 0x3C or init failed");
    for (;;)
      vTaskDelay(pdMS_TO_TICKS(1000));
  }

    // Kết nối WiFi
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println(" CONNECTED");

  // Lấy thời gian từ NTP server
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Khởi tạo thẻ SD
  if(!SD.begin(SDCARD_CS)) {
    Serial.println("Không thể khởi tạo thẻ SD!");
    return;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("Không tìm thấy thẻ SD.");
    return;
  }
  Serial.println("Khởi tạo thẻ SD thành công.");
  // Tạo tiêu đề cho file CSV nếu file chưa tồn tại
  File dataFile = SD.open("/datalog.csv");
  if (!dataFile) {
      logToSDCard("Timestamp,PM1.0, PM2.5, PM10");
  }
  dataFile.close();

  // Xóa toàn bộ một lần khởi tạo
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Wait - PMS (10s).");
  display.display();
  //vTaskDelay(pdMS_TO_TICKS(30000));

  Serial.println("Wait - PMS (10s).");

  delay(10000);
}

void loop()
{
  // 1) Đọc dữ liệu PMS nếu có gói mới
  if(millis() - previousDataMillis >= dataInterval){
    previousDataMillis = millis();
    if (pms.readUntil(data)) {
    last_PM1   = data.PM_AE_UG_1_0;
    last_PM2_5 = data.PM_AE_UG_2_5;
    last_PM10  = data.PM_AE_UG_10_0;
    // In ra Serial
    displayDataToSerial();
    }
        // --- Lấy thời gian và ghi vào thẻ SD ---
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
    Serial.println("Không thể lấy thông tin thời gian");
    } else {
    char timeString[20];
    // Định dạng thời gian: YYYY-MM-DD HH:MM:SS
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // Tạo chuỗi dữ liệu theo định dạng CSV
    String dataString = String(timeString) + "," + String(last_PM1) + "," + String(last_PM2_5) + "," + String(last_PM10);
    
    // Gọi hàm để ghi vào thẻ SD
    logToSDCard(dataString);
    }
    
    showSaved = true;
    savedUntil = millis() + SAVED_DURATION_MS;

  }

      if (showSaved) {
  if (millis() < savedUntil) {
    // vẽ chữ "saved" ở góc dưới trái
    display.setTextSize(1); // chỉnh kích thước chữ nếu cần
    display.fillRect(0, 48, 64, 16, SSD1306_BLACK); // xóa vùng (chỉ phần cần thiết)
    display.setCursor(0, 50); // một chút xuống để canh giữa theo chiều dọc
    display.setTextColor(SSD1306_WHITE);
    display.print("saved");
    display.display();
  } else {
    // thời gian hiển thị kết thúc -> xóa vùng một lần và tắt cờ
    display.fillRect(0, 48, 64, 16, SSD1306_BLACK);
    display.display();
    showSaved = false;
  }
}
  unsigned long currentMillis = millis();

  // 2) Cập nhật spinner nhanh (chỉ vẽ lại vùng nhỏ: spinner + từng dòng PM)
  if (currentMillis - previousSpinnerMillis >= spinnerInterval) {
    previousSpinnerMillis = currentMillis;
    spinnerIndex = (spinnerIndex + 1) % spinnerFrameCount;

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

// 1) Spinner (góc trái)
    display.fillRect(0, 0, 16, 12, SSD1306_BLACK);
    display.setCursor(2, 0);
    display.print(spinnerFrames[spinnerIndex]);

// 2) Dòng 1: PM1.0 (x = 20, y = 0)
    display.fillRect(16, 0, SCREEN_WIDTH - 16, 16, SSD1306_BLACK);
    display.setCursor(20, 0);
    display.print("PM1.0: ");
    display.print(last_PM1);
    display.print(" ug/m3");

// 3) Dòng 2: PM2.5 (x = 20, y = 16)
    display.fillRect(20, 16, SCREEN_WIDTH - 20, 16, SSD1306_BLACK);
    display.setCursor(20, 16);
    display.print("PM2.5: ");
    display.print(last_PM2_5);
    display.print(" ug/m3");

// 4) Dòng 3: PM10 (x = 20, y = 32)
    display.fillRect(20, 32, SCREEN_WIDTH - 20, 16, SSD1306_BLACK);
    display.setCursor(20, 32);
    display.print("PM10: ");
    display.print(last_PM10);
    display.print(" ug/m3");

// 5) Đồng hồ (đưa xuống dòng cuối bên phải, y = 48)
    display.fillRect(80, 48, SCREEN_WIDTH - 80, 16, SSD1306_BLACK);
    display.setCursor(80, 48);
    display.print((unsigned long)(currentMillis / 1000)); // giây từ khi bật

    display.display();
  }
}