/*
 * pc-switch — NodeMCU V3 (ESP8266) WiFi PC Güç Kontrolü
 *
 * Kütüphaneler (Arduino Library Manager):
 *   - PubSubClient  (Nick O'Leary)
 *   - ArduinoJson   (Benoit Blanchon) v6.x
 *
 * Board package:
 *   URL: https://arduino.esp8266.com/stable/package_esp8266com_index.json
 *   Board:      "NodeMCU 1.0 (ESP-12E Module)"
 *   Flash Size: "4MB (FS:2MB, OTA:~1019kB)"  ← LittleFS için önemli!
 *
 * İlk kurulum:
 *   1. Flashla, Serial Monitor'ü aç (115200 baud)
 *   2. Telefon/bilgisayardan "pcswitch-setup" WiFi'sine bağlan
 *   3. Tarayıcıda http://192.168.4.1/config aç
 *   4. Bilgileri gir, "Kaydet" → cihaz yeniden başlar
 *
 * Sonraki değişiklikler:
 *   http://pcswitch.local/config  (kullanıcı: admin, şifre: API anahtarın)
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"

#ifdef LCD_ENABLED
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include "qrcode.h"
// Software SPI: CLK, DIN, DC, CE, RST
Adafruit_PCD8544 lcd(LCD_CLK, LCD_DIN, LCD_DC, LCD_CE, LCD_RST);
static unsigned long lastDisplayMs = 0;
static bool          lcdBootPhase  = true;   // true → boot mesajları gösterilir
static bool          setupLcdDrawn = false;  // kurulum ekranı (QR) bir kez çizildi mi
// Forward declaration — tanım LCD bölümünde, ama WiFi/MQTT fonksiyonları önce derleniyor
static void lcdBootMsg(const char* msg, const char* detail = nullptr);
#endif

// ─── Çalışma zamanı config yapısı ────────────────────────────────────────────
struct Config {
    char     wifi_ssid[64];
    char     wifi_password[64];
    char     mqtt_broker[128];
    uint16_t mqtt_port;
    char     mqtt_user[64];
    char     mqtt_password[64];
    char     mqtt_client_id[32];
    char     api_key[64];
    char     mdns_hostname[32];
    char     ota_password[64];
};

Config cfg;
bool   configLoaded = false;  // true → config.json başarıyla okundu
bool   setupMode    = false;  // true → AP modunda, normal servisler durduruldu

// ─── Web arayüzü HTML (PROGMEM — RAM kullanmaz) ───────────────────────────────
const char CONFIG_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="tr"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>pc-switch</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font:15px system-ui,sans-serif;background:#0f172a;color:#cbd5e1;
  display:flex;justify-content:center;padding:2rem 1rem;min-height:100vh}
.wrap{width:100%;max-width:460px}
h1{font-size:1.3rem;color:#f1f5f9;margin-bottom:.2rem}
.sub{font-size:.8rem;color:#475569;margin-bottom:1.6rem}
section{margin-bottom:1.3rem}
h2{font-size:.68rem;text-transform:uppercase;letter-spacing:.08em;color:#3b82f6;
  margin-bottom:.8rem;padding-bottom:.4rem;border-bottom:1px solid #1e293b}
label{display:block;font-size:.8rem;color:#94a3b8;margin:.65rem 0 .25rem}
input{width:100%;padding:.55rem .75rem;background:#1e293b;
  border:1px solid #334155;border-radius:6px;color:#f1f5f9;
  font-size:.88rem;outline:none;transition:border-color .15s}
input:focus{border-color:#3b82f6;box-shadow:0 0 0 2px #3b82f620}
.btn{display:block;width:100%;padding:.7rem;border:none;border-radius:7px;
  font-size:.92rem;font-weight:600;cursor:pointer;margin-top:1rem;transition:opacity .15s}
.save{background:#3b82f6;color:#fff}.save:hover{opacity:.85}
.rst{background:#1e293b;color:#ef4444;border:1px solid #ef444445;margin-top:.65rem}
.rst:hover{background:#ef44440d}
.notice{border-radius:7px;padding:.75rem 1rem;font-size:.82rem;
  margin-bottom:1.4rem;border:1px solid}
.info{background:#0c1e3b;border-color:#1d4ed8;color:#93c5fd}
</style></head><body><div class="wrap">
<h1>&#9889; pc-switch</h1>
<p class="sub">NodeMCU ESP8266 &mdash; Ayar Paneli</p>
%NOTICE%
<form method="POST" action="/config">
<section><h2>WiFi</h2>
<label>A&#287; Ad&#305; (SSID)</label>
<input name="wifi_ssid" value="%WIFI_SSID%" autocomplete="off" required>
<label>&#350;ifre</label>
<input name="wifi_password" type="password"
  placeholder="(de&#287;i&#351;tirmek i&#231;in yaz&#305;n)" autocomplete="new-password">
</section>
<section><h2>MQTT Broker</h2>
<label>Adres</label>
<input name="mqtt_broker" value="%MQTT_BROKER%"
  placeholder="xxxx.s2.eu.hivemq.cloud" required>
<label>Port</label>
<input name="mqtt_port" type="number" value="%MQTT_PORT%">
<label>Kullan&#305;c&#305; Ad&#305;</label>
<input name="mqtt_user" value="%MQTT_USER%" autocomplete="off">
<label>&#350;ifre</label>
<input name="mqtt_password" type="password"
  placeholder="(de&#287;i&#351;tirmek i&#231;in yaz&#305;n)" autocomplete="new-password">
<label>Client ID</label>
<input name="mqtt_client_id" value="%MQTT_CLIENT_ID%">
</section>
<section><h2>G&#252;venlik</h2>
<label>HTTP API Anahtar&#305; &mdash; config sayfas&#305; &#351;ifresi de budur</label>
<input name="api_key" value="%API_KEY%"
  placeholder="Uzun rastgele bir &#351;ifre" required autocomplete="new-password">
<label>mDNS Hostname (.local)</label>
<input name="mdns_hostname" value="%MDNS_HOSTNAME%">
<label>OTA &#350;ifresi</label>
<input name="ota_password" type="password"
  placeholder="(de&#287;i&#351;tirmek i&#231;in yaz&#305;n)" autocomplete="new-password">
</section>
<button class="btn save" type="submit">&#10003; Kaydet &amp; Yeniden Ba&#351;lat</button>
</form>
<form method="POST" action="/config/reset"
  onsubmit="return confirm('T&#252;m ayarlar silinsin mi?')">
<button class="btn rst" type="submit">&#8635; Fabrika S&#305;f&#305;rla</button>
</form>
</div></body></html>)rawhtml";

// ─── Global nesneler ─────────────────────────────────────────────────────────
WiFiClientSecure wifiClient;
PubSubClient     mqttClient(wifiClient);
ESP8266WebServer server(80);

// ─── Röle durum makinesi (delay() yok) ───────────────────────────────────────
static bool          relayActive     = false;
static unsigned long relayStartMs    = 0;
static unsigned long relayDurationMs = 0;

// ─── Zamanlayıcı referansları ─────────────────────────────────────────────────
static unsigned long lastWifiCheckMs  = 0;
static unsigned long lastWifiBeginMs  = 0;   // son WiFi.begin() zamanı
static unsigned long lastMqttCheckMs  = 0;
static unsigned long lastHeartbeatMs  = 0;

// ═════════════════════════════════════════════════════════════════════════════
// CONFIG — Yükle / Kaydet
// ═════════════════════════════════════════════════════════════════════════════

static bool loadConfig() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("[Config] config.json yok");
        return false;
    }
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) { Serial.println("[Config] Açma hatası"); return false; }

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        f.close();
        Serial.println("[Config] JSON parse hatası");
        return false;
    }
    f.close();

    strlcpy(cfg.wifi_ssid,      doc["wifi_ssid"]      | "",                    sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_password,  doc["wifi_password"]  | "",                    sizeof(cfg.wifi_password));
    strlcpy(cfg.mqtt_broker,    doc["mqtt_broker"]    | "",                    sizeof(cfg.mqtt_broker));
    cfg.mqtt_port = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
    strlcpy(cfg.mqtt_user,      doc["mqtt_user"]      | "",                    sizeof(cfg.mqtt_user));
    strlcpy(cfg.mqtt_password,  doc["mqtt_password"]  | "",                    sizeof(cfg.mqtt_password));
    strlcpy(cfg.mqtt_client_id, doc["mqtt_client_id"] | DEFAULT_MQTT_CLIENT_ID, sizeof(cfg.mqtt_client_id));
    strlcpy(cfg.api_key,        doc["api_key"]        | "",                    sizeof(cfg.api_key));
    strlcpy(cfg.mdns_hostname,  doc["mdns_hostname"]  | DEFAULT_MDNS_HOSTNAME, sizeof(cfg.mdns_hostname));
    strlcpy(cfg.ota_password,   doc["ota_password"]   | "",                    sizeof(cfg.ota_password));

    Serial.printf("[Config] Yüklendi: WiFi=%s MQTT=%s\n", cfg.wifi_ssid, cfg.mqtt_broker);
    return strlen(cfg.wifi_ssid) > 0;
}

static bool saveConfig() {
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) { Serial.println("[Config] Yazma hatası"); return false; }

    DynamicJsonDocument doc(1024);
    doc["wifi_ssid"]      = cfg.wifi_ssid;
    doc["wifi_password"]  = cfg.wifi_password;
    doc["mqtt_broker"]    = cfg.mqtt_broker;
    doc["mqtt_port"]      = cfg.mqtt_port;
    doc["mqtt_user"]      = cfg.mqtt_user;
    doc["mqtt_password"]  = cfg.mqtt_password;
    doc["mqtt_client_id"] = cfg.mqtt_client_id;
    doc["api_key"]        = cfg.api_key;
    doc["mdns_hostname"]  = cfg.mdns_hostname;
    doc["ota_password"]   = cfg.ota_password;

    serializeJson(doc, f);
    f.close();
    Serial.println("[Config] Kaydedildi");
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// RÖLE
// ═════════════════════════════════════════════════════════════════════════════

static inline void relayWrite(bool active) {
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? !active : active);
}

static bool activateRelay(unsigned long durationMs) {
    if (relayActive) return false;
    relayWrite(true);
    relayStartMs    = millis();
    relayDurationMs = durationMs;
    relayActive     = true;
    Serial.printf("[RELAY] Activated for %lu ms\n", durationMs);
    return true;
}

static void handleRelayTimer() {
    if (relayActive && (millis() - relayStartMs >= relayDurationMs)) {
        relayWrite(false);
        relayActive = false;
        Serial.println("[RELAY] Deactivated");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// MQTT
// ═════════════════════════════════════════════════════════════════════════════

static String buildStateJson() {
    StaticJsonDocument<256> doc;
    doc["online"]       = true;
    doc["relay_active"] = relayActive;
    doc["fw_version"]   = FIRMWARE_VERSION;
    doc["uptime_ms"]    = millis();
    doc["rssi"]         = WiFi.RSSI();
    doc["ip"]           = WiFi.localIP().toString();
    doc["heap"]         = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    return out;
}

static void publishState() {
    String p = buildStateJson();
    mqttClient.publish(MQTT_TOPIC_STATE, p.c_str(), false);
    Serial.printf("[MQTT] State: %s\n", p.c_str());
}

static void sendHeartbeat() {
    if (millis() - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
    lastHeartbeatMs = millis();
    StaticJsonDocument<100> doc;
    doc["online"]    = true;
    doc["uptime_ms"] = millis();
    doc["rssi"]      = WiFi.RSSI();
    String p;
    serializeJson(doc, p);
    mqttClient.publish(MQTT_TOPIC_HEARTBEAT, p.c_str(), false);
    Serial.printf("[MQTT] Heartbeat rssi=%d\n", (int)WiFi.RSSI());
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    msg.reserve(length);
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    Serial.printf("[MQTT] [%s] %s\n", topic, msg.c_str());

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

    const char* action = doc["action"] | "";
    if      (strcmp(action, "power")  == 0) activateRelay(POWER_PULSE_MS);
    else if (strcmp(action, "reset")  == 0) activateRelay(RESET_PULSE_MS);
    else if (strcmp(action, "status") == 0) publishState();
}

static bool mqttConnect() {
    Serial.printf("[MQTT] Bağlanıyor: %s:%d\n", cfg.mqtt_broker, cfg.mqtt_port);
#ifdef LCD_ENABLED
    lcdBootMsg("MQTT baglan...", cfg.mqtt_broker);
#endif
    bool ok = mqttClient.connect(
        cfg.mqtt_client_id, cfg.mqtt_user, cfg.mqtt_password,
        MQTT_TOPIC_LWT, 1, true, "{\"online\":false}"
    );
    if (!ok) {
        Serial.printf("[MQTT] Başarısız rc=%d\n", mqttClient.state());
#ifdef LCD_ENABLED
        char errmsg[16];
        snprintf(errmsg, sizeof(errmsg), "Hata rc=%d", mqttClient.state());
        lcdBootMsg("MQTT basarisiz", errmsg);
        delay(1500);
#endif
        return false;
    }
    mqttClient.subscribe(MQTT_TOPIC_CMD);
    mqttClient.publish(MQTT_TOPIC_LWT, "{\"online\":true}", true);
    Serial.println("[MQTT] Bağlandı");
#ifdef LCD_ENABLED
    lcdBootMsg("MQTT baglandi");
#endif
    return true;
}

static void maintainMQTT() {
    if (setupMode) return;
    if (millis() - lastMqttCheckMs < MQTT_RECONNECT_MS) return;
    lastMqttCheckMs = millis();
    if (!mqttClient.connected()) mqttConnect();
}

// ═════════════════════════════════════════════════════════════════════════════
// WiFi
// ═════════════════════════════════════════════════════════════════════════════

static bool hasWifiCredentials() {
    return configLoaded && strlen(cfg.wifi_ssid) > 0;
}

static void startNormalServices();
static void recoverFromSetupMode();

static void beginStaConnection() {
    WiFi.disconnect();
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
    lastWifiBeginMs = millis();
}

static void startSetupAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(SETUP_AP_SSID);
    setupMode = true;
#ifdef LCD_ENABLED
    setupLcdDrawn = false;
#endif
    Serial.printf("[WiFi] AP modu: SSID=%s  IP=192.168.4.1\n", SETUP_AP_SSID);
    Serial.println("[WiFi] Tarayıcıda http://192.168.4.1/config adresini aç");
#ifdef LCD_ENABLED
    lcdBootMsg("Kurulum AP modu", "192.168.4.1");
#endif
}

static void startSetupAPWithRetry() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    beginStaConnection();
    WiFi.softAP(SETUP_AP_SSID);
    setupMode = true;
    lastWifiCheckMs = millis();
    Serial.printf("[WiFi] Kurtarma modu: AP=%s + STA yeniden deneme\n", SETUP_AP_SSID);
    Serial.printf("[WiFi] STA denemesi: %ds bekleme, sonra en az %ds aralıkla tekrar\n",
        WIFI_TIMEOUT_MS / 1000, WIFI_CHECK_MS / 1000);
    Serial.println("[WiFi] Tarayıcıda http://192.168.4.1/config adresini aç");
#ifdef LCD_ENABLED
    setupLcdDrawn = false;
    lcdBootMsg("AP + yeniden baglan", "192.168.4.1");
#endif
}

static void connectWiFi() {
    if (!hasWifiCredentials()) { startSetupAP(); return; }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    beginStaConnection();
    Serial.printf("[WiFi] Bağlanıyor: %s", cfg.wifi_ssid);
#ifdef LCD_ENABLED
    lcdBootMsg("WiFi baglan...", cfg.wifi_ssid);
#endif
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
    }

    if (WiFi.status() == WL_CONNECTED) {
        setupMode = false;
        Serial.printf("\n[WiFi] Bağlandı: %s\n", WiFi.localIP().toString().c_str());
#ifdef LCD_ENABLED
        lcdBootMsg("WiFi baglandi", WiFi.localIP().toString().c_str());
#endif
    } else {
        Serial.println("\n[WiFi] Bağlanamadı — kurtarma moduna geçiliyor");
        if (hasWifiCredentials()) startSetupAPWithRetry();
        else startSetupAP();
    }
}

static void recoverFromSetupMode() {
    setupMode = false;
#ifdef LCD_ENABLED
    setupLcdDrawn = false;
#endif
    WiFi.mode(WIFI_STA);
    WiFi.softAPdisconnect(true);
    Serial.printf("[WiFi] Kurtarıldı: %s\n", WiFi.localIP().toString().c_str());
#ifdef LCD_ENABLED
    lcdBootMsg("WiFi baglandi", WiFi.localIP().toString().c_str());
#endif
    startNormalServices();
}

static void maintainWiFi() {
    if (setupMode) {
        if (!hasWifiCredentials()) return;

        // Bağlantı kuruldu mu? — her loop turunda kontrol et
        if (WiFi.status() == WL_CONNECTED) {
            recoverFromSetupMode();
            return;
        }

        // Devam eden denemeyi kesme: önce WIFI_TIMEOUT_MS kadar bekle
        if (millis() - lastWifiBeginMs < WIFI_TIMEOUT_MS) return;

        if (millis() - lastWifiCheckMs < WIFI_CHECK_MS) return;
        lastWifiCheckMs = millis();

        Serial.println("[WiFi] STA yeniden deneniyor...");
        beginStaConnection();
        return;
    }

    if (millis() - lastWifiCheckMs < WIFI_CHECK_MS) return;
    lastWifiCheckMs = millis();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Koptu — yeniden bağlanılıyor");
        connectWiFi();
        if (!setupMode) MDNS.begin(cfg.mdns_hostname);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// HTTP — Kimlik doğrulama
// ═════════════════════════════════════════════════════════════════════════════

// REST API → X-API-Key header
static bool authenticate() {
    if (strlen(cfg.api_key) == 0) return true;
    if (server.hasHeader("X-API-Key") && server.header("X-API-Key") == cfg.api_key)
        return true;
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
}

// Config sayfası → HTTP Basic Auth (tarayıcı dialog gösterir)
// Setup modunda veya api_key boşsa kimlik istenmez
static bool authenticateConfig() {
    if (setupMode || strlen(cfg.api_key) == 0) return true;
    if (server.authenticate("admin", cfg.api_key)) return true;
    server.requestAuthentication(BASIC_AUTH, "pc-switch");
    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// HTTP — API rotaları (/status, /power, /reset)
// ═════════════════════════════════════════════════════════════════════════════

static void handleGetStatus() {
    if (setupMode) { server.send(503, "application/json", "{\"error\":\"not configured\"}"); return; }
    if (!authenticate()) return;
    server.send(200, "application/json", buildStateJson());
}

static void handlePostPower() {
    if (setupMode) { server.send(503, "application/json", "{\"error\":\"not configured\"}"); return; }
    if (!authenticate()) return;
    if (relayActive) { server.send(409, "application/json", "{\"error\":\"relay busy\"}"); return; }
    activateRelay(POWER_PULSE_MS);
    server.send(200, "application/json",
        "{\"status\":\"ok\",\"action\":\"power\",\"pulse_ms\":" + String(POWER_PULSE_MS) + "}");
}

static void handlePostReset() {
    if (setupMode) { server.send(503, "application/json", "{\"error\":\"not configured\"}"); return; }
    if (!authenticate()) return;
    if (relayActive) { server.send(409, "application/json", "{\"error\":\"relay busy\"}"); return; }
    activateRelay(RESET_PULSE_MS);
    server.send(200, "application/json",
        "{\"status\":\"ok\",\"action\":\"reset\",\"pulse_ms\":" + String(RESET_PULSE_MS) + "}");
}

static void handleNotFound() {
    if (setupMode) {
        // Setup modunda bilinmeyen rotaları config sayfasına yönlendir
        server.sendHeader("Location", "/config");
        server.send(302);
    } else {
        server.send(404, "application/json", "{\"error\":\"not found\"}");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// HTTP — Config sayfası (/config GET/POST, /config/reset POST)
// ═════════════════════════════════════════════════════════════════════════════

static String buildConfigHtml() {
    String html = FPSTR(CONFIG_HTML);

    String notice = "";
    if (setupMode) {
        notice = F("<div class='notice info'>"
                   "&#8505; &#304;lk kurulum &mdash; bilgileri girip Kaydet'e bas&#305;n."
                   "</div>");
    }
    html.replace(F("%NOTICE%"),       notice);
    html.replace(F("%WIFI_SSID%"),    String(cfg.wifi_ssid));
    html.replace(F("%MQTT_BROKER%"),  String(cfg.mqtt_broker));
    html.replace(F("%MQTT_PORT%"),    String(cfg.mqtt_port > 0 ? cfg.mqtt_port : DEFAULT_MQTT_PORT));
    html.replace(F("%MQTT_USER%"),    String(cfg.mqtt_user));
    html.replace(F("%MQTT_CLIENT_ID%"),
        String(strlen(cfg.mqtt_client_id) > 0 ? cfg.mqtt_client_id : DEFAULT_MQTT_CLIENT_ID));
    html.replace(F("%API_KEY%"),      String(cfg.api_key));
    html.replace(F("%MDNS_HOSTNAME%"),
        String(strlen(cfg.mdns_hostname) > 0 ? cfg.mdns_hostname : DEFAULT_MDNS_HOSTNAME));
    return html;
}

static void handleGetConfig() {
    if (!authenticateConfig()) return;
    server.send(200, "text/html; charset=utf-8", buildConfigHtml());
}

static void handlePostConfig() {
    if (!authenticateConfig()) return;

    // Zorunlu alan kontrolü (API'den geliyorsa browser required bypass edilebilir)
    if (server.arg("wifi_ssid").length() == 0) {
        server.send(400, "text/plain", "wifi_ssid gerekli");
        return;
    }
    if (server.arg("api_key").length() == 0 && !configLoaded) {
        server.send(400, "text/plain", "api_key gerekli");
        return;
    }

    // Boş olmayan alanları güncelle (boş bırakılan = mevcut değer korunur)
    auto setIfNotEmpty = [](char* dst, size_t size, const String& val) {
        if (val.length() > 0) strlcpy(dst, val.c_str(), size);
    };

    setIfNotEmpty(cfg.wifi_ssid,      sizeof(cfg.wifi_ssid),      server.arg("wifi_ssid"));
    setIfNotEmpty(cfg.wifi_password,  sizeof(cfg.wifi_password),  server.arg("wifi_password"));
    setIfNotEmpty(cfg.mqtt_broker,    sizeof(cfg.mqtt_broker),    server.arg("mqtt_broker"));
    setIfNotEmpty(cfg.mqtt_user,      sizeof(cfg.mqtt_user),      server.arg("mqtt_user"));
    setIfNotEmpty(cfg.mqtt_password,  sizeof(cfg.mqtt_password),  server.arg("mqtt_password"));
    setIfNotEmpty(cfg.mqtt_client_id, sizeof(cfg.mqtt_client_id), server.arg("mqtt_client_id"));
    setIfNotEmpty(cfg.api_key,        sizeof(cfg.api_key),        server.arg("api_key"));
    setIfNotEmpty(cfg.mdns_hostname,  sizeof(cfg.mdns_hostname),  server.arg("mdns_hostname"));
    setIfNotEmpty(cfg.ota_password,   sizeof(cfg.ota_password),   server.arg("ota_password"));
    if (server.arg("mqtt_port").length() > 0)
        cfg.mqtt_port = (uint16_t)server.arg("mqtt_port").toInt();

    if (!saveConfig()) {
        server.send(500, "text/plain", "Kaydetme hatası — LittleFS sorunu");
        return;
    }

    server.send(200, "text/html; charset=utf-8",
        F("<!DOCTYPE html><html><head><meta charset='UTF-8'><style>"
          "body{font:16px system-ui;background:#0f172a;color:#86efac;"
          "display:flex;justify-content:center;align-items:center;min-height:100vh}"
          "</style></head><body>"
          "<p>&#10003; Kaydedildi &mdash; Yeniden ba&#351;lat&#305;l&#305;yor...</p>"
          "</body></html>"));
    server.client().flush();
    delay(800);
    ESP.restart();
}

static void handlePostConfigReset() {
    if (!authenticateConfig()) return;
    LittleFS.remove(CONFIG_FILE);
    server.send(200, "text/html; charset=utf-8",
        F("<!DOCTYPE html><html><head><meta charset='UTF-8'><style>"
          "body{font:16px system-ui;background:#0f172a;color:#fca5a5;"
          "display:flex;justify-content:center;align-items:center;min-height:100vh}"
          "</style></head><body>"
          "<p>&#8635; Fabrika s&#305;f&#305;rland&#305; &mdash; Yeniden ba&#351;lat&#305;l&#305;yor...</p>"
          "</body></html>"));
    server.client().flush();
    delay(800);
    ESP.restart();
}

// ═════════════════════════════════════════════════════════════════════════════
// FİZİKSEL RESET BUTONU (D3 — NodeMCU FLASH butonu)
// ═════════════════════════════════════════════════════════════════════════════

static void handleResetButton() {
    static bool          prevState  = HIGH;
    static unsigned long pressStart = 0;

    bool state = digitalRead(RESET_BTN_PIN);

    if (state == LOW && prevState == HIGH) {
        pressStart = millis();
        Serial.println("[Button] Basıldı — 5s bas = fabrika sıfırla");
    }

    if (state == LOW && pressStart > 0) {
        unsigned long held = millis() - pressStart;

        // LED: tutulma süresine göre giderek hızlanan yanıp sönme
        unsigned long interval = map(constrain(held, 0UL, (unsigned long)RESET_BTN_HOLD_MS),
                                     0, RESET_BTN_HOLD_MS, 500, 60);
        digitalWrite(LED_BUILTIN, (millis() / interval) % 2 == 0 ? LOW : HIGH);

        if (held >= RESET_BTN_HOLD_MS) {
            Serial.println("[Button] FABRİKA SIFIRLAMA!");
            digitalWrite(LED_BUILTIN, LOW);   // sürekli yanar
            LittleFS.remove(CONFIG_FILE);
            delay(300);
            ESP.restart();
        }
    }

    if (state == HIGH && prevState == LOW) {
        pressStart = 0;
        digitalWrite(LED_BUILTIN, HIGH);  // LED kapat (active low)
        Serial.println("[Button] Bırakıldı — iptal");
    }

    prevState = state;
}

// ═════════════════════════════════════════════════════════════════════════════
// OTA
// ═════════════════════════════════════════════════════════════════════════════

static void setupOTA() {
    ArduinoOTA.setHostname(DEFAULT_OTA_HOSTNAME);
    if (strlen(cfg.ota_password) > 0) ArduinoOTA.setPassword(cfg.ota_password);

    ArduinoOTA.onStart([]() {
        relayWrite(false);
        relayActive = false;
        Serial.println("[OTA] Başladı");
    });
    ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Tamamlandı"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Hata %u\n", e); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        static unsigned int last = 0;
        unsigned int pct = (done * 100) / total;
        if (pct / 10 != last / 10) { Serial.printf("[OTA] %%%u\n", pct); last = pct; }
    });
    ArduinoOTA.begin();
    Serial.println("[OTA] Hazır");
}

static void startNormalServices() {
#ifdef LCD_ENABLED
    lcdBootMsg("mDNS & OTA...");
#endif
    if (MDNS.begin(cfg.mdns_hostname))
        Serial.printf("[mDNS] %s.local\n", cfg.mdns_hostname);

    setupOTA();

    wifiClient.setInsecure();
    mqttClient.setServer(cfg.mqtt_broker, cfg.mqtt_port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE);
    mqttClient.setBufferSize(512);
    mqttConnect();
}

// ═════════════════════════════════════════════════════════════════════════════
// OTOMATİK GÜNCELLEME (GitHub Releases — açılışta kontrol)
// ═════════════════════════════════════════════════════════════════════════════

// "1.2.3" → major*1e6 + minor*1e3 + patch  (basit semver karşılaştırması)
static unsigned long versionToNum(const char* v) {
    int a = 0, b = 0, c = 0;
    sscanf(v, "%d.%d.%d", &a, &b, &c);
    return (unsigned long)a * 1000000UL + (unsigned long)b * 1000UL + (unsigned long)c;
}

static bool isNewerVersion(const char* remote, const char* local) {
    return versionToNum(remote) > versionToNum(local);
}

// onProgress: indirme ilerledikçe LED'i yanıp söndür + LCD'de yüzde göster
static void otaUpdateProgress(int done, int total) {
    if (total <= 0) return;
    int pct = (done * 100) / total;
    static int last = -1;
    if (pct == last) return;
    digitalWrite(LED_BUILTIN, (pct / 5) % 2 == 0 ? LOW : HIGH);
    if (pct / 10 != last / 10) {
        Serial.printf("[Update] İndiriliyor %%%d\n", pct);
#ifdef LCD_ENABLED
        char buf[8];
        snprintf(buf, sizeof(buf), "%%%d", pct);
        lcdBootMsg("Guncelleniyor", buf);
#endif
    }
    last = pct;
}

static void checkForUpdates() {
    if (!OTA_UPDATE_ENABLED) return;

    Serial.println("[Update] Güncelleme kontrol ediliyor...");
    Serial.printf("[Update] Mevcut sürüm: %s\n", FIRMWARE_VERSION);
#ifdef LCD_ENABLED
    lcdBootMsg("Guncelleme", "kontrol ediliyor");
#endif

    // version.json indir
    WiFiClientSecure client;
    client.setInsecure();   // sertifika doğrulaması yok (ilk sürüm)

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub 302 → S3
    http.setTimeout(10000);
    if (!http.begin(client, OTA_VERSION_URL)) {
        Serial.println("[Update] version.json bağlantısı kurulamadı — atlanıyor");
        return;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Update] version.json alınamadı (HTTP %d) — atlanıyor\n", code);
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[Update] version.json parse hatası — atlanıyor");
        return;
    }

    const char* remote = doc["version"] | "";
    if (strlen(remote) == 0) {
        Serial.println("[Update] version alanı boş — atlanıyor");
        return;
    }

    Serial.printf("[Update] Sunucu sürümü: %s\n", remote);
    if (!isNewerVersion(remote, FIRMWARE_VERSION)) {
        Serial.println("[Update] Firmware güncel — güncelleme yok");
#ifdef LCD_ENABLED
        lcdBootMsg("Firmware guncel", FIRMWARE_VERSION);
        delay(800);
#endif
        return;
    }

    Serial.printf("[Update] Yeni sürüm bulundu (%s) → indiriliyor...\n", remote);
#ifdef LCD_ENABLED
    lcdBootMsg("Yeni surum!", remote);
    delay(800);
#endif

    // Röleyi güvenli duruma al (flash sırasında tetiklenmesin)
    relayWrite(false);
    relayActive = false;

    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.onProgress(otaUpdateProgress);

    t_httpUpdate_return r = ESPhttpUpdate.update(client, OTA_FIRMWARE_URL);
    // Başarılı olursa çekirdek otomatik ESP.restart() yapar; buraya dönülmez.
    switch (r) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[Update] BAŞARISIZ (%d): %s\n",
                ESPhttpUpdate.getLastError(),
                ESPhttpUpdate.getLastErrorString().c_str());
            digitalWrite(LED_BUILTIN, HIGH);  // LED kapat
#ifdef LCD_ENABLED
            lcdBootMsg("Guncelleme", "BASARISIZ");
            delay(1500);
#endif
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[Update] Güncelleme yok (sunucu reddetti)");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("[Update] Tamamlandı — yeniden başlatılıyor");
            break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// LCD (Nokia 5110) — sadece LCD_ENABLED tanımlıysa derlenir
// 84x48 px, textSize=1: her karakter 6x8 px → 14 sütun / 5 satır (10px aralık)
// ═════════════════════════════════════════════════════════════════════════════

#ifdef LCD_ENABLED

// Sayfayı ortala (x ekseni, 84 px genişlik)
static int16_t lcdCenterX(const char* text) {
    return max(0, (84 - (int16_t)(strlen(text) * 6)) / 2);
}

// Boot aşaması mesajı — başlık + ince çizgi + durum satırı + opsiyonel detay
// Sadece lcdBootPhase == true iken çizer (normal çalışmada çağrılsa bile etki etmez)
static void lcdBootMsg(const char* msg, const char* detail) {
    if (!lcdBootPhase) return;
    lcd.clearDisplay();
    lcd.setTextColor(BLACK);
    lcd.setTextSize(1);
    // Başlık
    lcd.setCursor(lcdCenterX("pc-switch"), 0);
    lcd.print("pc-switch");
    // Ayırıcı çizgi
    lcd.drawFastHLine(0, 10, 84, BLACK);
    // Durum mesajı
    lcd.setCursor(0, 14);
    lcd.print(msg);
    // Opsiyonel detay (2. satır)
    if (detail) {
        lcd.setCursor(0, 26);
        lcd.print(detail);
    }
    lcd.display();
}

static void setupDisplay() {
    lcd.begin();
    lcd.setContrast(LCD_CONTRAST);
    lcd.clearDisplay();
    lcdBootMsg("Baslatiliyor...");
    Serial.println("[LCD] Baslatildi");
}

// Kurulum modu: QR → http://192.168.4.1/config + IP metni
#define SETUP_QR_VERSION 2   // 25 byte URL için yeterli (ECC_LOW)

static void lcdDrawSetupScreen() {
    if (setupLcdDrawn) return;
    setupLcdDrawn = true;

    lcd.clearDisplay();
    lcd.setTextSize(1);
    lcd.setTextColor(BLACK);

    const char* status = hasWifiCredentials() ? "Yeniden dene" : "Kurulum";
    lcd.setCursor(lcdCenterX(status), 0);
    lcd.print(status);

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(SETUP_QR_VERSION)];
    if (qrcode_initText(&qrcode, qrcodeData, SETUP_QR_VERSION, ECC_LOW, SETUP_PAGE_URL) == 0) {
        const uint8_t size = qrcode.size;
        const int16_t qrX = (84 - size) / 2;
        const int16_t qrY = 10;
        for (uint8_t row = 0; row < size; row++) {
            for (uint8_t col = 0; col < size; col++) {
                if (qrcode_getModule(&qrcode, col, row))
                    lcd.drawPixel(qrX + col, qrY + row, BLACK);
            }
        }
    }

    lcd.setCursor(lcdCenterX(SETUP_AP_IP), 40);
    lcd.print(SETUP_AP_IP);

    lcd.display();
}

static void updateDisplay() {
    if (millis() - lastDisplayMs < LCD_UPDATE_MS) return;
    lastDisplayMs = millis();

    if (setupMode) {
        lcdDrawSetupScreen();
        return;
    }
    setupLcdDrawn = false;

    lcd.clearDisplay();
    lcd.setTextSize(1);
    lcd.setTextColor(BLACK);

    // ── Satır 0 (y=0): Başlık ───────────────────────────────────────────────
    lcd.setCursor(lcdCenterX("pc-switch"), 0);
    lcd.print("pc-switch");

    // ── Satır 1 (y=10): WiFi ────────────────────────────────────────────────
    lcd.setCursor(0, 10);
    if (WiFi.status() == WL_CONNECTED) {
        String ssid = String(cfg.wifi_ssid);
        if (ssid.length() > 11) ssid = ssid.substring(0, 10) + "~";
        lcd.print("W:");
        lcd.print(ssid);
    } else {
        lcd.print("WiFi:Baglaniyor");
    }

    // ── Satır 2 (y=20): IP ──────────────────────────────────────────────────
    lcd.setCursor(0, 20);
    lcd.print(WiFi.localIP().toString());

    // ── Satır 3 (y=30): MQTT ────────────────────────────────────────────────
    lcd.setCursor(0, 30);
    lcd.print("MQTT:");
    lcd.print(mqttClient.connected() ? "BAGLI" : "KOPUK");

    // ── Satır 4 (y=40): Röle (sol) + RSSI (sağ) ─────────────────────────────
    // "Role:AKTIF" = 10 karakter = 60px → RSSI değeri için 24px kaldı (4 karakter)
    lcd.setCursor(0, 40);
    lcd.print("Role:");
    lcd.print(relayActive ? "AKTIF" : "Pasif");
    if (WiFi.status() == WL_CONNECTED) {
        String rssi = String(WiFi.RSSI());        // ör. "-58"
        int16_t rx = 84 - (int16_t)(rssi.length() * 6);
        lcd.setCursor(rx, 40);
        lcd.print(rssi);
    }

    lcd.display();
}

#endif  // LCD_ENABLED

// ═════════════════════════════════════════════════════════════════════════════
// setup() / loop()
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
    // Boot güvenliği: röleyi hemen inaktif yap
    pinMode(RELAY_PIN, OUTPUT);
    relayWrite(false);

    // Reset butonu ve LED
    pinMode(RESET_BTN_PIN, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);  // başlangıçta kapalı (active low)

    Serial.begin(115200);
    Serial.println("\n\n[Boot] pc-switch başlıyor...");

#ifdef LCD_ENABLED
    setupDisplay();
#endif

    // LittleFS başlat
#ifdef LCD_ENABLED
    lcdBootMsg("Dosya sistemi...");
#endif
    if (!LittleFS.begin()) {
        Serial.println("[LittleFS] Mount başarısız — formatlanıyor...");
        LittleFS.format();
        LittleFS.begin();
    }

    // Config yükle ve WiFi'ye bağlan (başarısız → AP modu)
#ifdef LCD_ENABLED
    lcdBootMsg("Config yukleniyor");
#endif
    configLoaded = loadConfig();
    connectWiFi();   // → içinde LCD mesajları var

    // Normal mod servisleri
    if (!setupMode) {
        // Açılışta otomatik güncelleme kontrolü.
        // Yeni sürüm varsa buradan indirilir, flashlanır ve cihaz yeniden başlar
        // (aşağıdaki MDNS/OTA/MQTT kurulumlarına hiç ulaşılmaz).
        checkForUpdates();
        startNormalServices();
    }

    // HTTP sunucu — her modda çalışır
#ifdef LCD_ENABLED
    lcdBootMsg("HTTP baslatiliyor");
#endif
    server.collectHeaders("X-API-Key");
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Location", "/config");
        server.send(302);
    });
    server.on("/config",       HTTP_GET,  handleGetConfig);
    server.on("/config",       HTTP_POST, handlePostConfig);
    server.on("/config/reset", HTTP_POST, handlePostConfigReset);
    server.on("/status",       HTTP_GET,  handleGetStatus);
    server.on("/power",        HTTP_POST, handlePostPower);
    server.on("/reset",        HTTP_POST, handlePostReset);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("[HTTP] Sunucu başladı (port 80)");

    if (setupMode) {
        Serial.println("[Boot] KURULUM MODU — http://192.168.4.1/config");
    } else {
        Serial.printf("[Boot] Hazır — http://%s/  veya  http://%s.local/\n",
            WiFi.localIP().toString().c_str(), cfg.mdns_hostname);
    }

    // Boot tamamlandı — artık normal ekran güncelleme devreye girer
#ifdef LCD_ENABLED
    lcdBootMsg("Hazir!");
    delay(600);
    lcdBootPhase = false;
    lastDisplayMs = 0;   // ilk updateDisplay hemen çalışsın
#endif
}

void loop() {
    handleResetButton();
    handleRelayTimer();
    server.handleClient();

#ifdef LCD_ENABLED
    updateDisplay();
#endif

    maintainWiFi();

    if (!setupMode) {
        MDNS.update();
        ArduinoOTA.handle();
        if (mqttClient.connected()) {
            mqttClient.loop();
            sendHeartbeat();
        }
        maintainMQTT();
    }
}
