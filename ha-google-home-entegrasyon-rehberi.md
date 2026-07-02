# Home Assistant - Google Home Entegrasyon Rehberi (2026 Güncel)

Bu rehber; yerel ağda çalışan Home Assistant sunucunuzu, güvenli bir Cloudflare Tüneli üzerinden **Google Home** ve **Google Asistan** ekosistemine entegre etmek için gerekli tüm adımları içerir.

---

## 🛠 1. Aşama: Google Home Developer Console Ayarları

1. [Google Home Developer Console](https://console.home.google.com/) adresine gidin.
2. **Create a project** diyerek yeni bir proje oluşturun (Örn: `PC-Switch-v2`).
3. Proje tipini **Cloud-to-cloud** olarak seçin ve **Start building** butonuna basın.
4. Açılan ana ekranda sol menüden **Cloud-to-cloud > Integration** sekmesine gelin:
   - **Fulfillment URL:** `https://HA_DOMAİNİNİZ.dev/api/google_assistant` yazın ve kaydedin.

---

## 🔐 2. Aşama: Account Linking (OAuth 2.0) Köprüsü

Google Home uygulamasından "Cihaz Ekle" dediğinizde Home Assistant kullanıcı giriş ekranının tetiklenmesi için bu formu eksiksiz doldurun:

1. Sol menüden **Account Linking** sekmesine geçin.
2. Alanları şu şekilde doldurun:
   - **OAuth Client ID:** `https://oauth-redirect.googleusercontent.com/r/PROJE_ID_INIZ`
     *(Proje ID'sini tarayıcı adres çubuğundaki `projects/` ifadesinden sonra gelen kısımdan alabilirsiniz).*
   - **Client Secret:** `rastgele_bir_kelime_123` (Boş kalamaz).
   - **Authorization URL:** `https://HA_DOMAİNİNİZ.dev/auth/authorize`
   - **Token URL:** `https://HA_DOMAİNİNİZ.dev/auth/token`

3. Sayfayı aşağı kaydırın:
   - **Scopes:** Sırasıyla `email` ve `profile` yazıp ayrı ayrı ekleyin.
   - **Google to configure:** *"No, I want to specify my client ID and client secret to Google"* seçeneğini işaretleyin.

4. **Save** diyerek kaydedin ve sağ üstteki **Test** butonuna basarak simülasyonu başlatın.

---

## 🔑 3. Aşama: Google Cloud Hizmet Hesabı ve JSON Anahtarı

Home Assistant'ın Google sunucularıyla şifreli konuşabilmesi ve anlık durum raporlaması (Report State) yapabilmesi için bir servis anahtarı üretmeliyiz.

1. [Google Cloud Console - Service Accounts](https://console.cloud.google.com/iam-admin/serviceaccounts) adresine gidin.
2. Üst menüden oluşturduğunuz **Proje ID**'sini seçin.
3. **`+ Create Service Account`** butonuna basın:
   - Bir isim verin (`home-assistant`) ve **Create and Continue** deyin.
   - **Role (Rol):** Arama kutusuna **`Service Account Token Creator`** yazın ve bu rolü seçip **Done** ile bitirin.

4. Listeye gelen hizmet e-postasına çift tıklayın, **Keys (Anahtarlar) > Add Key > Create new key** adımlarını izleyin.
5. Format olarak **JSON** seçip indirin. Bu dosyanın adını **`google_assistant_service_account.json`** olarak değiştirin.

### ⚠️ Kritik Adım: Home Graph API'yi Etkinleştirme (403 Hatası Çözümü)

Eğer bu adımı atlarsanız Home Assistant senkronizasyon sırasında `result code: 403` hatası verecektir.

1. [Google Cloud API Library](https://console.cloud.google.com/apis/library) sayfasına gidin.
2. Projeniz seçiliyken arama çubuğuna **`Home Graph API`** yazın.
3. Listeden bulup **`ENABLE` (Etkinleştir)** butonuna basın.

---

## 💻 4. Aşama: Sunucu ve Home Assistant Konfigürasyonu

İndirdiğiniz `.json` anahtar dosyasını ve modern şablon mimarisini Home Assistant sunucunuza işlememiz gerekir.

### 1. Dosya Transferi

İndirdiğiniz `google_assistant_service_account.json` dosyasını SFTP (WinSCP/FileZilla) kullanarak sunucunuzda tam olarak şu dizine yükleyin:

```text
/data/homeassistant/
```

### 2. `configuration.yaml` Güncellemesi

Google Home, anlık tetiklenen ham MQTT butonlarını (`button.`) doğrudan akıllı cihaz olarak tanımaz. Google'ın sesli komutla çalıştırabilmesi için butonları **Modern Şablon Şalterleri (`template: switch:`)** içerisine sarmalamalıyız.

`configuration.yaml` dosyanızın güncel ve modernize edilmiş tam şablonu:

```yaml
# Varsayılan Entegrasyonlar ve Temalar
default_config:
frontend:
  themes: !include_dir_merge_named themes

automation: !include automations.yaml
script: !include scripts.yaml
scene: !include scenes.yaml

# ── Fiziksel MQTT Cihaz Tanımlamaları (Örn: NodeMCU ESP8266) ─────────────────
mqtt:
  button:
    - unique_id: pc_switch_power
      name: "PC Güç"
      icon: mdi:power
      command_topic: "pcswitch/command"
      payload_press: '{"action":"power"}'
      qos: 1
      device:
        identifiers: ["pcswitch_01"]
        name: "PC Switch"
        model: "NodeMCU V3 ESP8266"
        manufacturer: "ESP8266 Community"

    - unique_id: pc_switch_reset
      name: "PC Reset"
      icon: mdi:restart
      command_topic: "pcswitch/command"
      payload_press: '{"action":"reset"}'
      qos: 1
      device:
        identifiers: ["pcswitch_01"]
        name: "PC Switch"

  binary_sensor:
    - unique_id: pc_switch_online
      name: "PC Switch Bağlantı"
      icon: mdi:lan-connect
      state_topic: "pcswitch/lwt"
      value_template: "{{ 'ON' if value_json.online else 'OFF' }}"
      payload_on: "ON"
      payload_off: "OFF"
      device_class: connectivity
      device:
        identifiers: ["pcswitch_01"]
        name: "PC Switch"

# ── Modern Şablon Tanımlamaları (Google Home'un Tanıyacağı Sınıflar) ─────────
template:
  - button:
      - name: "Google Assistant Cihazları senkronize edin"
        icon: mdi:sync
        press:
          action: google_assistant.request_sync

  - switch:
      - name: "Bilgisayar Güç"
        unique_id: pc_guc_salteri_modern
        state: "off"
        turn_on:
          action: button.press
          target:
            entity_id: button.pc_switch_pc_guc  # Geliştirici Araçlarındaki gerçek nesne adı
        turn_off:
          action: button.press
          target:
            entity_id: button.pc_switch_pc_guc

      - name: "Bilgisayar Reset"
        unique_id: pc_reset_salteri_modern
        state: "off"
        turn_on:
          action: button.press
          target:
            entity_id: button.pc_switch_pc_reset
        turn_off:
          action: button.press
          target:
            entity_id: button.pc_switch_pc_reset

# ── Reverse Proxy / Tünel Güvenlik Ayarları ────────────────────────────────
http:
  use_x_forwarded_for: true
  trusted_proxies:
    - 172.16.0.0/12    # Docker alt ağları
    - 192.168.0.0/16   # Yerel ağ
    - 127.0.0.1
    - ::1

# ── Ana Google Assistant Entegrasyon Bloğu ─────────────────────────────────
google_assistant:
  project_id: PROJE_ID_INIZ # Örn: pc-switch-v2-78182
  service_account: !include google_assistant_service_account.json
  report_state: true
```

### 3. Sistemi Yeniden Başlatma

Değişiklikleri kaydedip çıktıktan sonra terminalden Home Assistant container'ını yeniden başlatın:

```bash
sudo docker restart homeassistant
```

---

## 📱 5. Aşama: Mobil Cihaz Eşleştirme ve Canlı Test

1. Home Assistant arayüzü açıldığında, oluşturduğumuz **"Google Assistant Cihazları senkronize edin"** butonuna panonuzdan bir kez basın. Bu işlem Google sunucularını güncel cihaz listesi için tetikler.
2. Akıllı telefonunuzdan **Google Home** uygulamasını açın.
3. **`+` (Ekle) > Cihaz kur > Google ile Çalışır (Works with Google)** adımlarını takip edin.
4. Arama kısmına projenizin adını yazın ve listede başında test ibaresi bulunan **`[test] Proje_Adınız`** seçeneğine tıklayın.
5. Açılan tünel arayüzünden Home Assistant kullanıcı adınız ve şifrenizle giriş yapın.

**Sonuç:** Giriş başarılı olduktan sonra `Bilgisayar Güç` ve `Bilgisayar Reset` şalterleriniz Google Home uygulamanıza atanmaya hazır şekilde dökülecektir. Artık *"Hey Google, bilgisayar gücü aç"* komutuyla NodeMCU üzerindeki rölelerinizi dünyanın her yerinden sesle tetikleyebilirsiniz!
