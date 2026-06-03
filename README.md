# nodemcu-pc-switch

NodeMCU V3 (ESP8266) + 5V röle ile bilgisayarın güç/reset düğmesini **WiFi ve internet üzerinden** uzaktan kontrol etmeyi sağlayan açık kaynak proje.

Fiziksel güç düğmesi sökülmez — röle onunla paralel çalışır. Komutlar MQTT (bulut broker) üzerinden geldiği için **port forwarding gerekmez**; cihaz internetten her yerden tetiklenebilir. Yerel ağda HTTP API de çalışır.

---

## Özellikler

- 🔌 **Uzaktan güç & reset** — PC'yi internetten aç/kapat/yeniden başlat
- ☁️ **MQTT (bulut broker)** — port forwarding yok, push modeli
- 🌐 **Yerel HTTP API** — LAN'da `http://pcswitch.local/` (API anahtarı korumalı)
- 💓 **Online izleme** — LWT + 30 sn heartbeat ile çevrimiçi/çevrimdışı durumu
- 🔄 **Otomatik OTA güncelleme** — cihaz her açılışta GitHub Releases'i kontrol edip yeni firmware'i kendi indirir, flashlar, yeniden başlar
- 🖥️ **Web kurulum arayüzü** — ilk açılışta `pcswitch-setup` AP'si üzerinden ayar (kod değiştirmeden)
- 🏠 **Home Assistant** — doğrudan MQTT entegrasyonu (buton + sensör entity'leri)
- 🪝 **Webhook sunucusu** — website/uygulama için HTTP→MQTT köprüsü (FastAPI)
- 📟 **Nokia 5110 LCD** — opsiyonel durum ekranı
- 🔘 **Fabrika sıfırlama** — web, HTTP veya fiziksel FLASH butonu (5 sn basılı tut)

---

## Mimari

```
[Internet / LAN]
      │
      ▼
[MQTT Bulut Broker]  ◄──── client.py / website / Home Assistant
      │ subscribe
      ▼
[NodeMCU ESP8266] ── D1 (GPIO5) ──► [5V Röle (aktif LOW)] ──► [Anakart PWR_SW]
```

---

## Donanım

| Parça | Adet |
|---|---|
| NodeMCU V3 CH-340 (ESP8266) | 1 |
| 1 Kanal 5V Röle Modülü | 1 |
| Jumper kablo (dişi-dişi) | 3 |
| Micro USB kablo | 1 |
| Nokia 5110 LCD (opsiyonel) | 1 |

**Röle bağlantısı:** `D1 → IN`, `GND → GND`, `3V3 → VCC` (tetiklenmezse VIN'e taşıyın).
**Anakart:** röle `COM` + `NO` → anakart `PWR_SW` header (fiziksel düğme paralelde kalır).

> ⚠️ Kapalı PC'yi uzaktan **açabilmek** için NodeMCU'yu her zaman açık bir USB adaptöründen besleyin (ATX 5V çıkışları PC kapalıyken ölüdür).

---

## Hızlı Başlangıç

1. **Arduino IDE** kurun, ESP8266 board paketini ekleyin:
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
2. **Board:** `NodeMCU 1.0 (ESP-12E)`, **Flash Size:** `4MB (FS:2MB, OTA:~1019kB)`
3. **Kütüphaneler:** `PubSubClient`, `ArduinoJson`, (LCD için) `Adafruit GFX` + `Adafruit PCD8544`
4. `nodemcu-pc-switch.ino` dosyasını açıp cihaza yükleyin.
5. Telefonla `pcswitch-setup` ağına bağlanın → `http://192.168.4.1/config` → WiFi & MQTT bilgilerini girin.
6. Kullanın:
   ```bash
   pip install paho-mqtt requests
   python client.py power     # güç düğmesi
   python client.py status    # durum
   python client.py online    # çevrimiçi mi?
   ```

> 📖 Adım adım ayrıntılı anlatım: **[KULLANIM_KILAVUZU.md](KULLANIM_KILAVUZU.md)**

---

## Otomatik Güncelleme (OTA)

Cihaz her açılışta `…/releases/latest/download/version.json` dosyasını okuyup kendi sürümüyle karşılaştırır; daha yeni sürüm varsa `firmware.bin`'i indirip flashlar ve yeniden başlar.

Yeni sürüm yayınlamak için bir tag push'lamak yeterli — GitHub Actions ([`.github/workflows/build.yml`](.github/workflows/build.yml)) firmware'i derleyip Release'e ekler:

```bash
git tag v1.1.0
git push origin v1.1.0
```

> Bu özelliğin çalışması için repo **public** olmalıdır (indirme linki kimlik doğrulama istemez). Tüm sırlar (WiFi/MQTT/API anahtarı) cihazdaki LittleFS `config.json`'da tutulur, repoya **girmez**.

---

## Proje Yapısı

```
nodemcu-pc-switch/
├── nodemcu-pc-switch.ino       # Ana firmware (ESP8266)
├── config.h                    # Donanım/sürüm/OTA sabitleri
├── client.py                   # Python CLI istemci (MQTT + HTTP)
├── .github/workflows/build.yml # Tag'de otomatik derleme + release
├── home_assistant/             # Home Assistant config & otomasyon örnekleri
├── webhook/                    # FastAPI HTTP→MQTT köprüsü (website entegrasyonu)
└── KULLANIM_KILAVUZU.md        # Ayrıntılı Türkçe kılavuz
```

---

## API & MQTT Özeti

**HTTP (LAN, `X-API-Key` gerekli):** `GET /status`, `POST /power`, `POST /reset`

**MQTT topic'leri:**

| Topic | Yön | İçerik |
|---|---|---|
| `pcswitch/command` | Cihaz dinler | `{"action":"power\|reset\|status"}` |
| `pcswitch/state` | Cihaz yayınlar | Durum JSON'u |
| `pcswitch/heartbeat` | Cihaz yayınlar (30 sn) | `{"online":true,...}` |
| `pcswitch/lwt` | Broker yayınlar | `{"online":false}` (bağlantı koparsa) |

---

## Güvenlik

- MQTT TLS (8883) ile şifreli iletişim.
- HTTP API yalnızca LAN'da, API anahtarı korumalı — internete açmayın.
- Hiçbir sır repoda tutulmaz; cihazda LittleFS `config.json` içindedir.
- OTA `setInsecure()` kullanır (GitHub HTTPS'e güvenilir); sertifika sabitleme ileride eklenebilir.

---

## Lisans

Bu depo kişisel/eğitim amaçlı yayınlanmıştır. Kullanım tamamen kendi sorumluluğunuzdadır — elektrik bağlantılarını yaparken bilgisayarın fişini çekin.
