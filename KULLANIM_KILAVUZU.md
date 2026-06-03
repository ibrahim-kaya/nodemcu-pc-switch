# pc-switch Kullanım Kılavuzu

NodeMCU V3 (ESP8266) ve 5V röle kullanarak bilgisayarın güç düğmesini WiFi üzerinden uzaktan kontrol etmeyi sağlayan sistem.

---

## İçindekiler

1. [Gereksinimler](#1-gereksinimler)
2. [Donanım Bağlantısı](#2-donanım-bağlantısı)
3. [Arduino IDE Kurulumu](#3-arduino-ide-kurulumu)
4. [Kodu Yükleme](#4-kodu-yükleme)
5. [İlk Kurulum (Web Arayüzü)](#5-i̇lk-kurulum-web-arayüzü)
6. [HiveMQ Cloud Broker Kurulumu](#6-hivemq-cloud-broker-kurulumu)
7. [Kullanım](#7-kullanım)
8. [Webhook Sunucusu (Website & Uygulama Entegrasyonu)](#8-webhook-sunucusu-website--uygulama-entegrasyonu)
9. [Home Assistant Entegrasyonu](#9-home-assistant-entegrasyonu)
10. [Ayarları Değiştirme](#10-ayarları-değiştirme)
11. [Fabrika Sıfırlama](#11-fabrika-sıfırlama)
12. [Nokia 5110 LCD Ekran (Opsiyonel)](#12-nokia-5110-lcd-ekran-opsiyonel)
13. [Otomatik Güncelleme (OTA)](#13-otomatik-güncelleme-ota)
14. [Sorun Giderme](#14-sorun-giderme)

---

## 1. Gereksinimler

### Donanım (Zorunlu)
| Parça | Adet |
|---|---|
| NodeMCU V3 CH-340 (ESP8266) | 1 |
| 1 Kanal 5V Röle Modülü (mavi/yeşil PCB) | 1 |
| Jumper kablo (dişi-dişi) | 3 |
| Micro USB kablo | 1 |

### Donanım (Opsiyonel — LCD Ekran)
| Parça | Adet | Not |
|---|---|---|
| Nokia 5110 LCD modülü (PCD8544, 84x48) | 1 | 3.3V uyumlu modül tercih edin |
| Jumper kablo (dişi-dişi) | 5 | |

### Yazılım
| Program | Notlar |
|---|---|
| [Arduino IDE 2.x](https://www.arduino.cc/en/software) | |
| [Python 3.8+](https://www.python.org/downloads/) | `client.py` için |
| `pip install paho-mqtt requests` | Python kütüphaneleri |

---

## 2. Donanım Bağlantısı

> ⚠️ **Güvenlik:** Bağlantıları yapmadan önce bilgisayarın güç kablosunu prizden çekin.

### 2.1 Besleme Seçenekleri

NodeMCU'ya iki farklı şekilde güç verebilirsiniz:

| Yöntem | Bağlantı | PC kapalıyken çalışır mı? |
|---|---|---|
| **USB şarj adaptörü** (önerilen) | USB girişi | ✅ Evet — her zaman açık |
| **PSU 5V çıkışı (kasa içi, düzenli)** | VIN + GND | ❌ Hayır — PC açıkken |

> **PSU 5V ile beslemede kritik kısıtlama:** ATX güç kaynağının 5V çıkışları yalnızca bilgisayar **açıkken** aktif olur. PC kapalıyken NodeMCU güç alamaz; bu durumda uzaktan açma komutu çalışmaz. PSU bağlantısı yalnızca **reset ve durum izleme** kullanım senaryolarına uygundur.
>
> Kapalı PC'yi uzaktan **açabilmek** istiyorsanız NodeMCU'yu USB şarj adaptörü veya her zaman açık bir USB hub üzerinden besleyin.

### 2.2 PSU 5V ile Besleme (Kasa İçi Kurulum)

PSU'dan herhangi bir **5V (+)** ve **GND (−)** noktası kullanılabilir (Molex, SATA güç, konnektör ucu vb.).

```
PSU 5V Çıkışı       NodeMCU V3
─────────────       ──────────
5V  (+)        ──── VIN
GND (−)        ──── GND
```

> - NodeMCU'nun VIN pini 4.5V–9V aralığını kabul eder; PSU 5V güvenlidir.
> - **12V hattına kesinlikle bağlamayın** — NodeMCU'yu bozar.

### 2.3 NodeMCU → Röle Modülü

```
NodeMCU V3          Röle Modülü
──────────          ────────────────
D1 (GPIO5) ──────── IN
GND        ──────── GND
3V3        ──────── VCC
```

> Röle tetiklenmiyorsa (klik sesi gelmiyorsa) VCC kablosunu 3V3'ten **VIN** pinine taşıyın.

### 2.4 Röle → Anakart Güç Header'ı

```
Röle Terminalleri        Anakart
─────────────────        ────────────────────────────
COM ──────────────────── PWR_SW Pin 1 (PWR_BTN+)
NO  ──────────────────── PWR_SW Pin 2 (PWR_BTN-)
```

**PWR_SW header'ını bulmak için:** Anakartın kullanım kılavuzuna bakın. Genellikle sağ alt köşede, "PWR_SW", "PW" veya "POWER SW" yazan 2 pinli bir header'dır.

> Mevcut fiziksel güç düğmesini **sökmeyiniz.** Röle, güç düğmesiyle paralel çalışır — ikisi de aynı anda kullanılabilir.

### 2.5 Bağlantı Şeması (Özet)

```
[USB adaptör]  VEYA  [PSU 5V + GND]  ← PC kapalıyken açmak istiyorsanız USB adaptör
      │                     │
      └──────────┬───────────┘
                 │
           [NodeMCU V3]
                 │  D1
           [Röle IN]──[Röle COM + NO]
                              │
                      [Anakart PWR_SW]
                              │
                      [Fiziksel güç düğmesi] (paralelde kalır)
```

---

## 3. Arduino IDE Kurulumu

### 3.1 ESP8266 Board Paketi

1. Arduino IDE'yi açın.
2. **File → Preferences** (veya `Ctrl+,`)
3. "Additional boards manager URLs" alanına şunu yapıştırın:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
4. **Tools → Board → Boards Manager** açın.
5. Arama kutusuna `esp8266` yazın.
6. **"esp8266 by ESP8266 Community"** → **Install**

### 3.2 Board Ayarları

**Tools** menüsünden sırayla seçin:

| Ayar | Değer |
|---|---|
| Board | NodeMCU 1.0 (ESP-12E Module) |
| Flash Size | **4MB (FS: 2MB, OTA: ~1019kB)** ← Kritik! |
| CPU Frequency | 80 MHz |
| Upload Speed | 115200 |

> ⚠️ Flash Size yanlış seçilirse web arayüzü çalışmaz. Mutlaka **4MB (FS: 2MB, OTA: ~1019kB)** seçin.

### 3.3 Gerekli Kütüphaneler

**Tools → Manage Libraries** açın, sırayla aratıp yükleyin:

| Kütüphane | Yayıncı | Versiyon | Zorunlu mu? |
|---|---|---|---|
| PubSubClient | Nick O'Leary | En güncel | Evet |
| ArduinoJson | Benoit Blanchon | **6.x** (7.x değil!) | Evet |
| Adafruit PCD8544 Nokia 5110 LCD library | Adafruit | En güncel | Yalnızca LCD kullanılacaksa |
| Adafruit GFX Library | Adafruit | En güncel | Yalnızca LCD kullanılacaksa |

> ArduinoJson kurulumunda versiyon seçme ekranı çıkarsa "6.21.x" seçin.
>
> Adafruit PCD8544 kurulumunda bağımlılık sorarsa "Install All" tıklayın (Adafruit GFX otomatik kurulur).

---

## 4. Kodu Yükleme

1. `pc-switch` klasörünü açın, `pc-switch.ino` dosyasına çift tıklayın.
2. NodeMCU'yu bilgisayara USB ile bağlayın.
3. **Tools → Port** → `COM?` (Windows) veya `/dev/ttyUSB?` (Linux/Mac) seçin.
4. **Sketch → Upload** (veya `Ctrl+U`) tıklayın.
5. Upload tamamlandıktan sonra **Tools → Serial Monitor** açın, baud rate `115200` seçin.

Başarılı yükleme sonrası Serial Monitor'de şunu görmelisiniz:

```
[Boot] pc-switch başlıyor...
[Config] config.json yok
[WiFi] AP modu: SSID=pcswitch-setup  IP=192.168.4.1
[WiFi] Tarayıcıda http://192.168.4.1/config adresini aç
[HTTP] Sunucu başladı (port 80)
[Boot] KURULUM MODU — http://192.168.4.1/config
```

---

## 5. İlk Kurulum (Web Arayüzü)

İlk açılışta cihaz `pcswitch-setup` adlı bir WiFi ağı oluşturur.

### Adımlar

1. **Telefon veya bilgisayardan** `pcswitch-setup` WiFi ağına bağlanın (şifre yok).
2. Tarayıcıda **http://192.168.4.1/config** adresini açın.
3. Formu doldurun:

| Alan | Açıklama |
|---|---|
| Ağ Adı (SSID) | Evinizin WiFi adı |
| WiFi Şifre | Evinizin WiFi şifresi |
| MQTT Broker | HiveMQ Cloud adresiniz (bkz. Bölüm 6) |
| MQTT Port | `8883` (TLS — değiştirmeyin) |
| MQTT Kullanıcı Adı | HiveMQ'da oluşturduğunuz kullanıcı |
| MQTT Şifre | HiveMQ kullanıcı şifresi |
| Client ID | `pcswitch-01` (olduğu gibi bırakabilirsiniz) |
| HTTP API Anahtarı | Kendinizin belirlediği uzun bir şifre |
| mDNS Hostname | `pcswitch` (olduğu gibi bırakabilirsiniz) |
| OTA Şifresi | Kablosuz güncelleme şifresi |

4. **Kaydet & Yeniden Başlat** butonuna tıklayın.
5. Cihaz yeniden başlar ve evinizin WiFi'sine bağlanır.

Serial Monitor'de şunu görmelisiniz:

```
[WiFi] Bağlandı: 192.168.1.45
[mDNS] pcswitch.local
[MQTT] Bağlandı
[Boot] Hazır — http://192.168.1.45/  veya  http://pcswitch.local/
```

---

## 6. HiveMQ Cloud Broker Kurulumu

MQTT broker, cihazla internet üzerinden iletişimi sağlar. HiveMQ Cloud ücretsiz tier yeterlidir.

1. **https://www.hivemq.com/mqtt-cloud-broker/** adresine gidin.
2. **"Start Free"** butonuna tıklayın, hesap oluşturun.
3. Cluster oluşturulduktan sonra **"Manage Cluster"** sayfasını açın.
4. Broker adresinizi kopyalayın: `xxxxxxxx.s2.eu.hivemq.cloud`
5. Sol menüden **"Access Management"** → **"Add New Credentials"**
6. Kullanıcı adı: `pcswitch`, şifre belirleyin.
7. Bu bilgileri web arayüzündeki MQTT alanlarına girin.

---

## 7. Kullanım

### 7.1 Python İstemcisi (Önerilen)

```bash
# Kütüphaneleri kur (bir kez)
pip install paho-mqtt requests
```

Tekrar tekrar yazmamak için ortam değişkenleri tanımlayın:

```bash
# Windows PowerShell
$env:PC_SWITCH_MQTT_BROKER = "xxxxxxxx.s2.eu.hivemq.cloud"
$env:PC_SWITCH_MQTT_USER   = "pcswitch"
$env:PC_SWITCH_MQTT_PASS   = "mqtt-sifreniz"
$env:PC_SWITCH_API_KEY     = "api-anahtariniz"
```

```bash
# Kullanım
python client.py power          # Güç düğmesine bas (PC açar/kapar)
python client.py reset          # Reset düğmesine bas
python client.py status         # Cihaz durumunu göster
python client.py online         # Cihaz çevrimiçi mi?
python client.py online --watch # Anlık izleme (Ctrl+C ile çık)
```

#### Örnek çıktılar

```
> python client.py status
  Durum     : Çevrimiçi
  Röle      : Kapalı
  Uptime    : 3600s (1s 0dk 0sn)
  WiFi RSSI : -58 dBm
  IP        : 192.168.1.45
  Serbest heap : 32456 byte

> python client.py online
[Durum] Cihaz: CEVRIMICI

> python client.py power
[OK] 'power' komutu gönderildi
```

### 7.2 Yerel Ağdan HTTP API (curl)

LAN'dayken doğrudan HTTP ile de kontrol edebilirsiniz:

```bash
# Durum sorgula
curl http://pcswitch.local/status -H "X-API-Key: api-anahtariniz"

# Güç düğmesi
curl -X POST http://pcswitch.local/power -H "X-API-Key: api-anahtariniz"

# Reset düğmesi
curl -X POST http://pcswitch.local/reset -H "X-API-Key: api-anahtariniz"
```

#### API Yanıt Örnekleri

**GET /status**
```json
{
  "online": true,
  "relay_active": false,
  "uptime_ms": 3600000,
  "rssi": -58,
  "ip": "192.168.1.45",
  "heap": 32456
}
```

**POST /power**
```json
{
  "status": "ok",
  "action": "power",
  "pulse_ms": 500
}
```

#### HTTP Durum Kodları

| Kod | Anlam |
|---|---|
| 200 | Başarılı |
| 401 | API anahtarı yanlış |
| 409 | Röle şu an meşgul (önceki komut bitmedi) |
| 503 | Cihaz henüz yapılandırılmamış |

### 7.3 Online Durum İzleme

Cihaz iki mekanizma ile online durumunu bildirir:

| Mekanizma | Topic | Açıklama |
|---|---|---|
| **LWT** | `pcswitch/lwt` | Bağlantı koparsa broker otomatik `{"online":false}` yayınlar |
| **Heartbeat** | `pcswitch/heartbeat` | Her 30 saniyede `{"online":true}` gönderilir |

```bash
# Tek seferlik kontrol
python client.py online

# Sürekli izleme (60s heartbeat gelmezse uyarı verir)
python client.py online --watch
```

---

## 8. Webhook Sunucusu (Website & Uygulama Entegrasyonu)

Webhook sunucusu; website, mobil uygulama veya MQTT bilmeyen her servisin cihazı
**HTTP üzerinden** kontrol etmesini sağlar. İnternet üzerinde çalışan bir sunucuya kurulur,
gelen HTTP isteklerini MQTT'ye çevirir.

```
Website / Uygulama / Home Assistant
         │  HTTP POST /api/power
         ▼
   [webhook.py]  ← internetteki bir sunucuda
         │  MQTT publish
         ▼
   [HiveMQ Cloud]
         │
         ▼
   [NodeMCU] → Röle → PC
```

### 8.1 Kurulum

```bash
cd webhook
pip install -r requirements.txt
cp .env.example .env       # .env dosyasını düzenleyin
```

`.env` dosyasını doldurun:

```
MQTT_BROKER=xxxxxxxx.s2.eu.hivemq.cloud
MQTT_USER=pcswitch
MQTT_PASS=mqtt-sifreniz
WEBHOOK_API_KEY=webhook-icin-ayri-uzun-sifre
ALLOWED_ORIGINS=https://siteniz.com    # * = herkese açık
```

### 8.2 Çalıştırma

```bash
uvicorn webhook:app --host 0.0.0.0 --port 8000
```

Swagger arayüzü otomatik gelir: `http://sunucu:8000/docs`

### 8.3 Ücretsiz Deploy Seçenekleri

| Platform | Ücretsiz Plan | Notlar |
|---|---|---|
| [Railway](https://railway.app) | 500 saat/ay | En kolay, GitHub'dan deploy |
| [Render](https://render.com) | Sınırlı | 15dk hareketsizlikte uyur |
| [Fly.io](https://fly.io) | 3 VM | Daha teknik |

Her platformda `.env` değişkenlerini platform panelinden girebilirsiniz.

### 8.4 Endpointler

| Method | Endpoint | Açıklama |
|---|---|---|
| `GET` | `/health` | Sunucu sağlık kontrolü (auth gerekmez) |
| `POST` | `/api/power` | Güç düğmesi |
| `POST` | `/api/reset` | Reset düğmesi |
| `GET` | `/api/status` | Cihaz durumu (maks 8s bekler) |

Tüm `/api/*` endpointleri `X-API-Key` header'ı gerektirir.

### 8.5 curl ile Test

```bash
# Sağlık kontrolü
curl https://sunucunuz.com/health

# Güç düğmesi
curl -X POST https://sunucunuz.com/api/power \
  -H "X-API-Key: webhook-sifreniz"

# Durum
curl https://sunucunuz.com/api/status \
  -H "X-API-Key: webhook-sifreniz"
```

### 8.6 Website'ye Ekleme

`webhook/website_example.html` dosyasını referans alın. İki satırı değiştirin:

```js
const WEBHOOK_URL = "https://sunucunuz.com";
const API_KEY     = "webhook-sifreniz";
```

> ⚠️ **Güvenlik:** API anahtarını doğrudan frontend JavaScript koduna yazmak
> anahtarı herkese görünür kılar. Üretim sitelerinde bir backend proxy kullanın:
> tarayıcı kendi sunucunuza istek atar, sunucunuz webhook'a iletir.

---

## 9. Home Assistant Entegrasyonu

Home Assistant, cihaza **doğrudan MQTT** üzerinden bağlanır — webhook sunucusuna gerek yoktur.

### 9.1 MQTT Broker Ekleme

1. **Ayarlar → Cihazlar & Hizmetler → Entegrasyon Ekle → MQTT**
2. Broker: `xxxxxxxx.s2.eu.hivemq.cloud`
3. Port: `8883`
4. TLS: **Açık**
5. Kullanıcı adı ve şifre: HiveMQ bilgileriniz

### 9.2 Entity'leri Ekleme

`home_assistant/configuration.yaml` içeriğini kendi HA `configuration.yaml`'ınıza yapıştırın,
ardından **Geliştirici Araçları → YAML → Tümünü Yenile** yapın.

HA'da otomatik oluşan entity'ler:

| Entity | Tür | Açıklama |
|---|---|---|
| `button.pc_guc` | Buton | Güç düğmesine basar |
| `button.pc_reset` | Buton | Reset düğmesine basar |
| `binary_sensor.pc_switch_baglanti` | Binary Sensör | online / offline |
| `sensor.pc_switch_wifi_sinyal` | Sensör | WiFi RSSI (dBm) |
| `sensor.pc_switch_uptime` | Sensör | Çalışma süresi (saat) |

### 9.3 Dashboard'a Ekleme

HA Lovelace dashboard'una eklemek için YAML:

```yaml
type: entities
title: PC Switch
entities:
  - entity: binary_sensor.pc_switch_baglanti
    name: Bağlantı
  - entity: button.pc_guc
    name: Güç
  - entity: button.pc_reset
    name: Reset
  - entity: sensor.pc_switch_wifi_sinyal
    name: WiFi
```

### 9.4 Örnek Otomasyonlar

`home_assistant/automations.yaml` dosyasında hazır örnekler bulunmaktadır:

- **Cihaz çevrimdışı olduğunda bildirim gönder**
- **Belirli saatte PC'yi otomatik aç**

---

## 10. Ayarları Değiştirme

Kurulum tamamlandıktan sonra ayarları web arayüzünden değiştirebilirsiniz:

1. Tarayıcıda **http://pcswitch.local/config** adresini açın.
2. Kullanıcı adı: `admin` — Şifre: API anahtarınız
3. İstediğiniz alanı değiştirin.
   - Şifre alanlarını boş bırakırsanız mevcut şifre korunur.
4. **Kaydet & Yeniden Başlat** → Cihaz yeni ayarlarla başlar.

> IP adresiyle de erişebilirsiniz: `http://192.168.1.45/config`

---

## 11. Fabrika Sıfırlama

Üç farklı yöntemle sıfırlayabilirsiniz:

### Yöntem 1 — Web Arayüzü

`http://pcswitch.local/config` → sayfanın altındaki **"Fabrika Sıfırla"** butonu → onayla.

### Yöntem 2 — HTTP API

```bash
curl -X POST http://pcswitch.local/config/reset \
  -u "admin:api-anahtariniz"
```

### Yöntem 3 — Fiziksel Buton (Ağa Erişim Yoksa)

NodeMCU üzerindeki **FLASH** yazılı butona **5 saniye** basılı tutun:

```
Basılı tutulurken:
  0s ──────────────────── 5s
  LED yavaş yanıp söner → giderek hızlanır → sürekli yanar → SIFIRLAMA
```

Sıfırlama sonrası cihaz `pcswitch-setup` AP moduna geçer, Bölüm 5'ten yeniden kurulum yapabilirsiniz.

---

## 12. Nokia 5110 LCD Ekran (Opsiyonel)

LCD ekran tamamen opsiyoneldir. Takılı olmasa bile kod sorunsuz çalışır.

### 12.1 Ekranda Gösterilen Bilgiler

```
┌────────────────┐
│   pc-switch    │  ← Başlık (ortalı)
│ W:EvinizinWiFi │  ← Bağlı ağ adı (AP modunda: "AP:kurulum modu")
│ 192.168.1.45   │  ← Yerel IP adresi
│ MQTT:BAGLI     │  ← MQTT bağlantı durumu
│ Role:Pasif -58 │  ← Röle durumu + WiFi RSSI (dBm, sağa hizalı)
└────────────────┘
```

### 12.2 Donanım Bağlantısı

> ⚠️ **Voltaj:** Nokia 5110 modülünün **3.3V uyumlu** olduğundan emin olun. Çoğu mavi PCB modül 3.3V–5V arasında çalışır; sinyal pinleri için 3.3V yeterlidir.

```
NodeMCU V3          Nokia 5110
──────────          ──────────────────────
D5 (GPIO14) ─────── 5: CLK
D7 (GPIO13) ─────── 4: DIN
D6 (GPIO12) ─────── 3: DC
D2 (GPIO4)  ─────── 2: CE
D0 (GPIO16) ─────── 1: RST
3V3         ─────── 6: VCC
3V3         ─────── 7: BL  (arka ışık — her zaman açık)
GND         ─────── 8: GND
```

> BL pinini NodeMCU'nun herhangi bir dijital pinine bağlarsanız arka ışığı yazılımdan kontrol edebilirsiniz. Sürekli açık olmasını istiyorsanız 3V3'e bağlamak yeterlidir.

### 12.3 Yazılım Aktivasyonu

1. `config.h` dosyasını Arduino IDE'de açın.
2. Şu satırı bulun:
   ```cpp
   // #define LCD_ENABLED
   ```
3. Başındaki `//` yi kaldırın:
   ```cpp
   #define LCD_ENABLED
   ```
4. Kodu derleyip yükleyin.

LCD takılı **değilken** `LCD_ENABLED` tanımlıysa ekran kütüphanesi boşlukta yazacağından garip davranışlar olabilir. Ekranı çıkarmadan önce `//` yi geri ekleyin.

### 12.4 Kontrast Ayarı

Ekranda hiçbir şey görünmüyorsa veya ekran tamamen siyahsa `config.h` içindeki değeri ayarlayın:

```cpp
#define LCD_CONTRAST   50   // 0-127; genellikle 40-60 arası iyi çalışır
```

Düşük değer → açık/soluk, yüksek değer → koyu/dolu. Her ekran farklı olabilir.

---

## 13. Otomatik Güncelleme (OTA)

Cihaz **her açılışta** GitHub deposundaki en son sürümü kontrol eder. Daha yeni bir firmware yayınlanmışsa otomatik olarak indirir, flashlar ve yeniden başlar — USB kablosu veya manuel müdahale gerekmez.

> ℹ️ Bu, yerel ağdaki Arduino IDE OTA'sından (Tools → Port → `pcswitch-ota`) farklıdır. Bu sistem internet üzerinden, otomatik ve "pull" mantığıyla çalışır.

### Nasıl çalışır?

```
[Açılış] → WiFi bağlandı → version.json kontrol et
   │
   ├─ Sunucu sürümü > cihaz sürümü?
   │     ├─ HAYIR → "Firmware güncel" → normal çalış
   │     └─ EVET  → firmware.bin indir → flashla → yeniden başla
   │
   └─ İnternet yok / dosya yok → sessizce geç, normal çalış
```

Cihaz, GitHub'ın `…/releases/latest/download/version.json` kalıcı linkindeki minik dosyayı (`{"version":"1.1.0"}`) okur, kendi sürümüyle (semver) karşılaştırır. LCD takılıysa süreç ekranda da gösterilir ("Guncelleme kontrol ediliyor", "Yeni surum!", "%50" gibi).

### Tek seferlik kurulum

1. Proje zaten **public** `ibrahim-kaya/nodemcu-pc-switch` deposunda.
   > Repo public olmalı: özel repo'da indirme linki kimlik doğrulama ister.
2. `config.h` içindeki depo bilgileri doğru olmalı:
   ```cpp
   #define GITHUB_OWNER  "ibrahim-kaya"
   #define GITHUB_REPO   "nodemcu-pc-switch"
   ```
3. Bu haliyle bir kez cihaza flashlayın (bundan sonrası otomatik).

### Yeni sürüm yayınlama

Kodu değiştirip yeni bir sürüm yayınlamak için sadece bir tag push'lamanız yeterli:

```bash
git add -A && git commit -m "Yeni özellik"
git tag v1.1.0
git push origin v1.1.0
```

Ardından `.github/workflows/build.yml` GitHub Actions iş akışı otomatik olarak:
1. Firmware'i derler (sürümü tag'den, `v1.1.0` → `1.1.0`, alır),
2. `firmware.bin` + `version.json` üretir,
3. Bunları GitHub Release'e asset olarak ekler.

Cihaz bir sonraki açılışında (elektrik kesintisi / `pcswitch.local/config` üzerinden yeniden başlatma / fişi çekip takma) yeni sürümü görüp kendini günceller.

> 💡 **Sürüm numarası:** Daima `v` öneki + [semver](https://semver.org) kullanın (`v1.0.1`, `v1.2.0`, `v2.0.0`). Cihaz sadece **daha büyük** sürüm numarasına günceller.

### Çalışan sürümü öğrenme

`/status` yanıtında `fw_version` alanı bulunur:

```bash
curl http://pcswitch.local/status -H "X-API-Key: ANAHTARINIZ"
# {"online":true,"relay_active":false,"fw_version":"1.1.0", ...}
```

### Güncellemeyi kapatma

İstemezseniz `config.h` içinde:
```cpp
#define OTA_UPDATE_ENABLED   false
```

---

## 14. Sorun Giderme

### Cihaz WiFi'ye bağlanamıyor

**Belirti:** Serial Monitor'de `Bağlanamadı — AP moduna geçiliyor` mesajı.

1. `pcswitch-setup` ağına bağlanıp `http://192.168.4.1/config` adresini açın.
2. SSID ve şifreyi kontrol edin (büyük/küçük harf duyarlıdır).
3. 2.4 GHz ağ olduğunu doğrulayın (ESP8266 5 GHz desteklemez).

---

### MQTT bağlantısı kurulamıyor

**Belirti:** `[MQTT] Başarısız rc=-2` veya `rc=5`

| Hata Kodu | Anlam | Çözüm |
|---|---|---|
| rc=-2 | Broker'a ulaşılamıyor | Broker adresini kontrol edin |
| rc=4 | Kullanıcı adı/şifre yanlış | HiveMQ'dan kontrol edin |
| rc=5 | Yetkilendirme reddedildi | HiveMQ Access Management'tan izin verin |

---

### Röle tıklamıyor

1. Serial Monitor'de `[RELAY] Activated for 500 ms` mesajı var mı?
   - **Varsa:** Bağlantı sorunu — GND veya IN kablosunu kontrol edin.
   - **Yoksa:** Komut cihaza ulaşmıyor — MQTT/HTTP bağlantısını kontrol edin.
2. VCC kablosunu **3V3**'ten **Vin** pinine taşıyın.
3. Röle modülündeki LED'in yanıp yanmadığını kontrol edin.

---

### `pcswitch.local` tarayıcıda açılmıyor

Windows'ta mDNS bazen sorunlu olabilir. IP adresiyle deneyin:

```bash
# IP adresini öğrenmek için
python client.py status --http --host 192.168.1.1  # router'dan bakın
```

veya Serial Monitor'deki `[Boot] Hazır — http://192.168.1.XX/` satırındaki IP'yi kullanın.

---

### Yerel OTA (Arduino IDE) çalışmıyor

1. Arduino IDE'de **Tools → Port** menüsünü açın.
2. `pcswitch-ota` adlı ağ portu görünüyor mu?
   - **Görünmüyorsa:** Cihaz ve bilgisayar aynı ağda olmalı.
3. OTA şifresini `config.h`'deki varsayılandan değiştirdiyseniz IDE soracaktır.

---

### Otomatik güncelleme (GitHub) çalışmıyor

Serial Monitor'de açılışta görünen `[Update]` loglarına bakın:

| Log | Anlam | Çözüm |
|---|---|---|
| `version.json alınamadı (HTTP 404)` | Release veya asset yok | Tag push ettiniz mi? Actions yeşil mi? Release'de `version.json` var mı? |
| `version.json bağlantısı kurulamadı` | İnternet/DNS sorunu | WiFi'nin internet erişimi olduğunu doğrulayın |
| `Firmware güncel` | Sunucu sürümü ≤ cihaz sürümü | Daha büyük bir sürüm tag'leyin (`v1.0.1`) |
| `BAŞARISIZ` | İndirme/flash hatası | Flash boyutu yeterli mi? (`4MB FS:2MB, OTA:~1019kB`) |

Diğer kontroller:
- `config.h`'deki `GITHUB_OWNER` ve `GITHUB_REPO` doğru mu?
- Repo **public** mi? (Private repo doğrudan indirmeye izin vermez.)
- `git push origin v1.1.0` ile tag'i gerçekten push ettiniz mi?

---

### Cihaz bootloop'a girdi (sürekli yeniden başlıyor)

1. FLASH butonuna basılı tutarken USB'yi bağlayın → flash moduna girer.
2. Arduino IDE'den kodu yeniden yükleyin.
3. Sorun devam ederse **Tools → Erase Flash → "All Flash Contents"** seçip yeniden yükleyin (ayarlar silinir).

---

## Teknik Referans

### Pin Tablosu

| Pin | Kullanım | Notlar |
|---|---|---|
| D1 (GPIO5) | Röle IN | Boot'ta HIGH — güvenli |
| D3 (GPIO0) | Reset butonu | NodeMCU FLASH butonu |
| LED_BUILTIN | Durum LED'i | Active LOW |
| D5 (GPIO14) | LCD CLK *(opsiyonel)* | `LCD_ENABLED` tanımlıysa |
| D7 (GPIO13) | LCD DIN *(opsiyonel)* | `LCD_ENABLED` tanımlıysa |
| D6 (GPIO12) | LCD DC *(opsiyonel)* | `LCD_ENABLED` tanımlıysa |
| D2 (GPIO4) | LCD CE *(opsiyonel)* | `LCD_ENABLED` tanımlıysa |
| D0 (GPIO16) | LCD RST *(opsiyonel)* | `LCD_ENABLED` tanımlıysa |

### MQTT Topic'leri

| Topic | Yön | İçerik |
|---|---|---|
| `pcswitch/command` | Cihaz dinler | `{"action":"power"}` / `{"action":"reset"}` / `{"action":"status"}` |
| `pcswitch/state` | Cihaz yayınlar | Durum JSON'u |
| `pcswitch/heartbeat` | Cihaz yayınlar (30s) | `{"online":true,"uptime_ms":...,"rssi":...}` |
| `pcswitch/lwt` | Broker yayınlar | `{"online":false}` — bağlantı koparsa |

### Varsayılan Değerler

| Parametre | Varsayılan |
|---|---|
| Güç basış süresi | 500 ms |
| Reset basış süresi | 200 ms |
| Heartbeat aralığı | 30 saniye |
| WiFi bağlanma zaman aşımı | 15 saniye |
| Reset buton basış süresi | 5 saniye |
| mDNS hostname | `pcswitch.local` |
| HTTP port | 80 |
| MQTT port | 8883 (TLS) |
