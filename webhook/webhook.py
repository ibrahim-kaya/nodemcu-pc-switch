"""
pc-switch Webhook Sunucusu
==========================
MQTT ile konuşamayan her şeyi (website, Home Assistant REST, Postman, mobil uygulama…)
bu sunucu üzerinden cihaza bağlar.

Kurulum:
    pip install -r requirements.txt

Çalıştırma:
    uvicorn webhook:app --host 0.0.0.0 --port 8000

Ortam değişkenleri (.env veya export ile):
    MQTT_BROKER      → HiveMQ Cloud adresi
    MQTT_USER        → MQTT kullanıcı adı
    MQTT_PASS        → MQTT şifresi
    WEBHOOK_API_KEY  → Bu sunucuya erişim için ayrı bir anahtar
    ALLOWED_ORIGINS  → CORS için izin verilen domain'ler (virgülle, * = hepsi)

Endpointler:
    POST /api/power   → Güç düğmesi
    POST /api/reset   → Reset düğmesi
    GET  /api/status  → Cihaz durumu (cihazdan MQTT yanıtı beklenir)
    GET  /health      → Sunucu sağlık kontrolü (auth gerektirmez)
"""

import json
import os
import ssl
import threading
import time
from typing import Optional

import paho.mqtt.client as mqtt
import paho.mqtt.publish as mqtt_publish
from fastapi import FastAPI, HTTPException, Security
from fastapi.middleware.cors import CORSMiddleware
from fastapi.security.api_key import APIKeyHeader

# ─── Config (ortam değişkenlerinden) ─────────────────────────────────────────
MQTT_BROKER     = os.environ.get("MQTT_BROKER",     "")
MQTT_USER       = os.environ.get("MQTT_USER",       "")
MQTT_PASS       = os.environ.get("MQTT_PASS",       "")
WEBHOOK_API_KEY = os.environ.get("WEBHOOK_API_KEY", "")
ALLOWED_ORIGINS = os.environ.get("ALLOWED_ORIGINS", "*").split(",")

MQTT_PORT       = 8883
TOPIC_CMD       = "pcswitch/command"
TOPIC_STATE     = "pcswitch/state"
STATUS_TIMEOUT  = 8  # saniye

if not all([MQTT_BROKER, MQTT_USER, MQTT_PASS, WEBHOOK_API_KEY]):
    raise RuntimeError(
        "Eksik ortam değişkeni! "
        "MQTT_BROKER, MQTT_USER, MQTT_PASS ve WEBHOOK_API_KEY tanımlanmalı."
    )

# ─── FastAPI ──────────────────────────────────────────────────────────────────
app = FastAPI(
    title="pc-switch Webhook",
    description="NodeMCU PC güç kontrolü — HTTP → MQTT köprüsü",
    version="1.0.0",
    docs_url="/docs",
    redoc_url=None,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=ALLOWED_ORIGINS,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["*"],
)

# ─── Kimlik doğrulama ─────────────────────────────────────────────────────────
api_key_header = APIKeyHeader(name="X-API-Key", auto_error=False)

async def require_api_key(key: Optional[str] = Security(api_key_header)) -> str:
    if key != WEBHOOK_API_KEY:
        raise HTTPException(status_code=401, detail="Geçersiz API anahtarı")
    return key

# ─── MQTT TLS bağlamı ─────────────────────────────────────────────────────────
def _tls_context() -> ssl.SSLContext:
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx

# ─── Tek seferlik MQTT publish ────────────────────────────────────────────────
def _publish(action: str) -> None:
    payload = json.dumps({"action": action})
    try:
        mqtt_publish.single(
            topic=TOPIC_CMD,
            payload=payload,
            hostname=MQTT_BROKER,
            port=MQTT_PORT,
            auth={"username": MQTT_USER, "password": MQTT_PASS},
            tls={"tls_version": ssl.PROTOCOL_TLS_CLIENT, "cert_reqs": ssl.CERT_NONE},
            qos=1,
        )
    except Exception as e:
        raise HTTPException(status_code=502, detail=f"MQTT publish hatası: {e}")

# ─── Durum sorgusu (subscribe + bekle) ───────────────────────────────────────
def _fetch_status() -> dict:
    state: dict = {}
    event = threading.Event()

    def on_connect(client, userdata, connect_flags, reason_code, properties):
        if not reason_code.is_failure:
            client.subscribe(TOPIC_STATE)
            # Subscribe tamamlandıktan sonra status isteği gönder
            client.publish(TOPIC_CMD, json.dumps({"action": "status"}), qos=1)

    def on_message(client, userdata, msg):
        try:
            state.update(json.loads(msg.payload.decode()))
        except Exception:
            pass
        event.set()
        client.disconnect()

    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id="pcswitch-webhook-status",
        clean_session=True,
    )
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.tls_set_context(_tls_context())
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, keepalive=15)
    except Exception as e:
        raise HTTPException(status_code=502, detail=f"MQTT bağlantı hatası: {e}")

    client.loop_start()
    event.wait(timeout=STATUS_TIMEOUT)
    client.loop_stop()
    client.disconnect()

    if not state:
        raise HTTPException(
            status_code=504,
            detail="Cihazdan yanıt gelmedi — kapalı veya bağlantısız olabilir",
        )
    return state

# ═════════════════════════════════════════════════════════════════════════════
# Endpointler
# ═════════════════════════════════════════════════════════════════════════════

@app.get("/health", tags=["sistem"])
async def health():
    """Sunucu çalışıyor mu? (kimlik doğrulaması gerektirmez)"""
    return {"status": "ok"}


@app.post("/api/power", tags=["kontrol"])
async def power(key: str = Security(require_api_key)):
    """
    PC güç düğmesine 500ms basar.
    - PC kapalıysa açar
    - PC açıksa kapatır (işletim sistemi normal kapatma yapar)
    """
    _publish("power")
    return {"status": "ok", "action": "power", "pulse_ms": 500}


@app.post("/api/reset", tags=["kontrol"])
async def reset(key: str = Security(require_api_key)):
    """PC reset düğmesine 200ms basar."""
    _publish("reset")
    return {"status": "ok", "action": "reset", "pulse_ms": 200}


@app.get("/api/status", tags=["kontrol"])
async def status(key: str = Security(require_api_key)):
    """
    Cihazın anlık durumunu döner.
    Cihaza MQTT ile status komutu gönderir ve yanıtı bekler (maks 8s).
    """
    return _fetch_status()
