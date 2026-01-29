# ESP32 Environmental Monitoring System

Hệ thống giám sát môi trường dựa trên vi điều khiển ESP32, tập trung vào việc thu thập dữ liệu bụi mịn từ cảm biến **PMS7003** và dữ liệu khí tượng từ cảm biến **BME280**. Hệ thống hỗ trợ hiển thị trực quan và lưu trữ dữ liệu dài hạn trên thẻ nhớ MicroSD.

## 🌟 Mục tiêu dự án
* [cite_start]**Đo lường chất lượng không khí**: Theo dõi nồng độ bụi mịn PM1.0, PM2.5, PM10[cite: 1, 2].
* [cite_start]**Theo dõi khí hậu**: Đo nhiệt độ, độ ẩm, áp suất khí quyển và tính toán độ cao ước tính[cite: 1, 2].
* [cite_start]**Lưu trữ dữ liệu**: Ghi nhật ký dưới dạng file CSV để phục vụ phân tích môi trường[cite: 2].

---

## 🚀 Chế độ hoạt động

Dự án hỗ trợ hai chế độ vận hành riêng biệt để tối ưu hóa giữa hiệu suất và điện năng:

### 1. Mode A: Active Monitoring (Giám sát chủ động)
[cite_start]Đây là chế độ hoạt động liên tục, phù hợp khi có nguồn điện ổn định[cite: 1].
* [cite_start]**Tần suất đo**: Đọc dữ liệu từ cảm biến mỗi 1 giây[cite: 2, 12].
* [cite_start]**Hiển thị**: Cập nhật thông số lên màn hình OLED mỗi 0.5 giây[cite: 15].
* [cite_start]**Lưu trữ**: Dữ liệu được lưu tạm vào RAM buffer và ghi vào thẻ SD mỗi 5 phút (300 giây) để bảo vệ tuổi thọ thẻ nhớ[cite: 2, 13].

### 2. Mode B: Power Saving (Tiết kiệm năng lượng)
Chế độ tối ưu cho các thiết bị chạy pin hoặc trạm quan trắc từ xa.
* **Chu trình**: Thức dậy -> Đo & Đọc dữ liệu -> Ghi trực tiếp vào thẻ SD -> Deep Sleep.
* **Tiết kiệm**: ESP32 sẽ đi vào chế độ ngủ sâu (Deep Sleep) trong **15 phút** giữa mỗi lần đo.
* **Mục tiêu**: Giảm tiêu thụ điện năng tối đa, phù hợp cho việc thu thập dữ liệu dài hạn mà không cần bảo trì nguồn điện thường xuyên.

---

## 🛠 Danh sách linh kiện & Kết nối
| Linh kiện | Chức năng |
| :--- | :--- |
| **ESP32** | Vi điều khiển trung tâm |
| **PMS7003** | Cảm biến bụi mịn |
| **BME280** | Nhiệt độ, Độ ẩm, Áp suất |
| **OLED SSD1306** | Hiển thị dữ liệu |
| **MicroSD Module** | Lưu trữ dữ liệu CSV |

---

## 📊 Cấu trúc dữ liệu (CSV)
[cite_start]Dữ liệu được lưu trữ trong file `datalog.csv` với các trường thông tin sau[cite: 55]:
`Time(s), PM1.0, PM2.5, PM10, Temp(C), Press(Pa), Alt(m), Humi(%)`

---

## 📝 Tiêu chuẩn mã nguồn
[cite_start]Mã nguồn được phát triển tuân thủ theo **SPARC Firmware Code Convention (v1.02)**[cite: 2]:
* [cite_start]Sử dụng biến theo quy tắc PascalCase (ví dụ: `CurrentPm25_u16`)[cite: 26].
* [cite_start]Logic điều khiển không chặn (Non-blocking) sử dụng `millis()` để quản lý tác vụ[cite: 108].
* [cite_start]Buffer dữ liệu được cấp phát sẵn (12KB) để tối ưu hóa quản lý bộ nhớ RAM[cite: 16, 56].

---
