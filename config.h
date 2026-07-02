#pragma once

// ─── Donanım (değişmez — kod ile belirlenir) ──────────────────────────────────
// D1 (GPIO5) seçildi: boot sırasında HIGH kalır → röle tetiklenmez
#define RELAY_PIN             D1
#define RELAY_ACTIVE_LOW      true    // Mavi/yeşil PCB röle → aktif LOW

// ─── Süreler (ms) ────────────────────────────────────────────────────────────
#define POWER_PULSE_MS        500
#define RESET_PULSE_MS        200
#define HEARTBEAT_INTERVAL_MS 30000

// ─── Periyodik kontrol aralıkları (ms) ───────────────────────────────────────
#define WIFI_TIMEOUT_MS       15000
#define WIFI_CHECK_MS         10000
#define MQTT_RECONNECT_MS     5000
// PubSubClient.h kendi MQTT_KEEPALIVE'ını (15) tanımlar; çakışmayı önlemek için sıfırla
#ifdef MQTT_KEEPALIVE
  #undef MQTT_KEEPALIVE
#endif
#define MQTT_KEEPALIVE        60

// ─── Kurulum AP'i (ilk açılış / WiFi bağlanamadıysa) ─────────────────────────
#define SETUP_AP_SSID         "pcswitch-setup"   // şifresiz AP
#define SETUP_AP_IP           "192.168.4.1"
#define SETUP_PAGE_URL        "http://192.168.4.1/config"  // kurulum QR hedefi

// ─── LittleFS config dosyası ──────────────────────────────────────────────────
#define CONFIG_FILE           "/config.json"

// ─── Derleme zamanı varsayılanları (config.json yoksa kullanılır) ─────────────
#define DEFAULT_MDNS_HOSTNAME   "pcswitch"
#define DEFAULT_MQTT_PORT       8883
#define DEFAULT_MQTT_CLIENT_ID  "pcswitch-01"
#define DEFAULT_OTA_HOSTNAME    "pcswitch-ota"

// ─── Fiziksel reset butonu ────────────────────────────────────────────────────
// NodeMCU üzerindeki mevcut FLASH butonu — ekstra donanım gerekmez
// 5 saniye basılı tut → LED hızlanır → config.json silinir → AP moduna döner
#define RESET_BTN_PIN     D3      // GPIO0 — NodeMCU FLASH butonu (INPUT_PULLUP)
#define RESET_BTN_HOLD_MS 5000   // kaç ms basılı kalırsa sıfırlar

// ─── MQTT topic sabitleri ─────────────────────────────────────────────────────
#define MQTT_TOPIC_CMD        "pcswitch/command"
#define MQTT_TOPIC_STATE      "pcswitch/state"
#define MQTT_TOPIC_HEARTBEAT  "pcswitch/heartbeat"
#define MQTT_TOPIC_LWT        "pcswitch/lwt"

// ─── Nokia 5110 LCD (PCD8544) — OPSİYONEL ─────────────────────────────────────
// LCD takılı değil ise aşağıdaki "#define LCD_ENABLED" satırının başına "//" işareti EKLEYİN.
// LCD takılı değilken aktif bırakmayın (garip davranışlara yol açar).
#define LCD_ENABLED

#define LCD_CLK       D5     // GPIO14 — CLK (5)
#define LCD_DIN       D7     // GPIO13 — DIN (4)
#define LCD_DC        D6     // GPIO12 — DC  (3)
#define LCD_CE        D2     // GPIO4  — CE  (2)
#define LCD_RST       D0     // GPIO16 — RST (1)
#define LCD_CONTRAST  50     // 0-127; genellikle 40-60 arası iyi çalışır
#define LCD_UPDATE_MS 1000   // normal çalışmada ekran tazeleme aralığı (ms)

// ─── Firmware sürümü (tek doğru kaynak) ──────────────────────────────────────
// GitHub Actions derlerken sürümü tag'den config.h'e yazar.
// Elle/lokal derlemede aşağıdaki değer kullanılır.
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION "1.0.0"
#endif

// ─── Otomatik OTA güncelleme (GitHub Releases) ───────────────────────────────
// Cihaz her açılışta GITHUB reposunun en son release'ini kontrol eder.
// Daha yeni bir sürüm varsa otomatik indirir, flashlar ve yeniden başlar.
// GEREKSİNİM: repo PUBLIC olmalı (kalıcı /releases/latest/download/ linki için).
#define OTA_UPDATE_ENABLED   true
#define GITHUB_OWNER         "ibrahim-kaya"          // ← GitHub kullanıcı/org adınız
#define GITHUB_REPO          "nodemcu-pc-switch"     // ← repo adı

// Türetilen kalıcı linkler (release asset'leri) — değiştirmeyin
#define OTA_VERSION_URL \
  "https://github.com/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest/download/version.json"
#define OTA_FIRMWARE_URL \
  "https://github.com/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest/download/firmware.bin"
