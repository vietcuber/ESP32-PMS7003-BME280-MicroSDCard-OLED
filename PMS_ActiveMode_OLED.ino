#include "PMS.h"
#include "HardwareSerial.h"

#include <freertos/FreeRTOS.h> 
#include <freertos/task.h> 

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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

// Biến lưu giá trị PM cuối cùng (để hiển thị khi chưa có gói mới)
uint16_t last_PM1 = 0;
uint16_t last_PM2_5 = 0;
uint16_t last_PM10 = 0;
// Sử dụng uint16_t vì giá trị bụi không âm, và hiện thị 16bit = 2bytes, đúng như gói dũ liệu từ datasheet của cảm biến

// Spinner frames
const char spinnerFrames[] = {'|', '/', '-', '\\'};
const int spinnerFrameCount = sizeof(spinnerFrames) / sizeof(spinnerFrames[0]);
int spinnerIndex = 0;

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

  // Xóa toàn bộ một lần khởi tạo
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SSD1306 Ready");
  display.display();
  vTaskDelay(pdMS_TO_TICKS(3000));
}

void loop()
{
  // 1) Đọc dữ liệu PMS nếu có gói mới
  if (pms.readUntil(data)) {
    last_PM1   = data.PM_AE_UG_1_0;
    last_PM2_5 = data.PM_AE_UG_2_5;
    last_PM10  = data.PM_AE_UG_10_0;
    // In ra Serial
    displayDataToSerial();
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

  // 3) Cập nhật dữ liệu chu kỳ (chỉ log ra Serial ở đây nếu cần)
  Serial.println((unsigned long)(currentMillis / 1000)); // giây từ khi bật

}