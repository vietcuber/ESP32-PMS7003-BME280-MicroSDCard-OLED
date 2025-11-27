#include "PMS.h"
#include "HardwareSerial.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <SPI.h>
#include <SD.h>

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

// --- Cấu hình chân CS cho thẻ SD ---
#define SDCARD_CS 5

// Thời gian
unsigned long previousSpinnerMillis = 0;
const long spinnerInterval = 500; // 500 ms cho spinner xoay mỗi lần
unsigned long previousDataMillis = 0;
const long dataInterval = 1000; // 1s cho data cấp nhật mỗi lần

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

// --- CÁC BIẾN CHO VIỆC ĐỆM VÀ QUẢN LÝ THẺ SD ---
File myFile; 
String dataBuffer = ""; 
const long BUFFER_WRITE_INTERVAL = 300000; // 5 phút
unsigned long lastBufferWriteTime = 0;

bool isSDInitialized = false;

// --- HÀM GHI BỘ ĐỆM XUỐNG THẺ SD ---
void writeBufferToSD() {
  // 1. Kiểm tra nếu bộ đệm rỗng thì thoát ngay
  if (dataBuffer.length() == 0) {
    return; 
  }

  // 2. Chỉ ghi khi hệ thống đã khởi tạo thành công và File đang mở
  if (isSDInitialized && myFile) {
    
    Serial.println("Saving buffer to SD card...");
    
    // Ghi dữ liệu từ RAM xuống đối tượng File
    size_t bytesWritten = myFile.print(dataBuffer);

    if (bytesWritten > 0) {
      // Quan trọng: flush() để đảm bảo dữ liệu vật lý được ghi xuống thẻ
      myFile.flush(); 
      
      Serial.println("Data saved.");
      dataBuffer = ""; // Xóa bộ đệm RAM sau khi ghi thành công
      
      // Hiển thị chữ "saved" lên màn hình
      showSaved = true;
      savedUntil = millis() + SAVED_DURATION_MS;
      
    } else {
      Serial.println("Write failed (Possible hardware error).");
      // Ở đây không làm gì cả, dữ liệu vẫn giữ trong buffer chờ lần sau
    }
    
  } else {
    Serial.println("SD Card not ready or File not open. Skipping write.");
    // Tùy chọn: Nếu muốn xóa buffer để tránh tràn RAM khi không có thẻ:
    dataBuffer = ""; 
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
  Serial.println("ESP32 khởi động. PMS7003 Khởi động. OLED khởi động. SDCard khởi động");

  //Khởi tạo giao tiếp UART cho PMS tốc độ baud datasheet = 9600
  PMS_SERIAL.begin(9600, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);

  Wire.begin();
  Wire.setClock(100000); // an toàn: 100 kHz

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.display();
  
  // --- KHỞI TẠO SD CARD ---
  Serial.print("Initializing SD card...");
  
  if (!SD.begin(SDCARD_CS)) {
    Serial.println("Initialization failed! (Không tìm thấy thẻ)");
    isSDInitialized = false;
  } else {
    Serial.println("Initialization done.");
    isSDInitialized = true;

    // Mở file ngay lập tức và giữ handle file
    // Sử dụng FILE_APPEND để ghi nối tiếp
    myFile = SD.open("/datalog.csv", FILE_APPEND);

    // Kiểm tra file có mở được không
    if (myFile) {
      Serial.println("File opened/created successfully.");
      // Nếu file mới tinh (size = 0), ghi thêm dòng tiêu đề
      if (myFile.size() == 0) {
        myFile.println("Time(s), PM1.0, PM2.5, PM10");
        myFile.flush(); // Đẩy dữ liệu xuống thẻ ngay
      }
    } else {
      Serial.println("Error opening datalog.csv");
      isSDInitialized = false; // Đánh dấu lỗi nếu không mở được file
    }
  }
}

void loop(){
  unsigned long currentTime = millis(); // <-- Lấy thời gian 1 lần đầu vòng lặp

  if(currentTime - previousDataMillis >= dataInterval){
    previousDataMillis = currentTime;
    if (pms.readUntil(data, 1000)) {
    last_PM1   = data.PM_AE_UG_1_0;
    last_PM2_5 = data.PM_AE_UG_2_5;
    last_PM10  = data.PM_AE_UG_10_0;

    displayDataToSerial();

    String dataString = String(millis()/1000) + "," + String(last_PM1) + "," + String(last_PM2_5) + "," + String(last_PM10);
    dataBuffer += dataString + "\n"; // <-- THÊM dòng này. Thêm dữ liệu vào bộ đệm RAM
    }
  }

  if (currentTime - lastBufferWriteTime >= BUFFER_WRITE_INTERVAL) {
    lastBufferWriteTime = currentTime; // <-- Đặt lại mốc thời gian ghi
    writeBufferToSD(); // <-- Gọi hàm ghi bộ đệm
  }
  
  if (showSaved) {
    if (millis() < savedUntil) {
      // vẽ chữ "saved" ở góc dưới trái trong khoảng thời gian saved duration
      display.setTextSize(1);
      display.fillRect(0, 48, 64, 16, SSD1306_BLACK);
      display.setCursor(0, 50);
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

  if (millis() - previousSpinnerMillis >= spinnerInterval) {
    previousSpinnerMillis = millis();
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
    display.print(millis() / 1000); // giây từ khi bật

    display.display();
  }
}