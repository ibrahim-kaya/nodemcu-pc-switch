#!/usr/bin/env python3
"""
pc-switch client — NodeMCU PC güç kontrolü

Kurulum:
    pip install paho-mqtt requests

Kullanım:
    python client.py power                  # PC güç düğmesi (MQTT)
    python client.py reset                  # Reset düğmesi (MQTT)
    python client.py status                 # Cihaz durumu (MQTT)
    python client.py online                 # Çevrimiçi mi? (LWT retained mesajından)
    python client.py online --watch         # Anlık izleme modu

    # Yerel HTTP API (LAN'dayken)
    python client.py power  --http
    python client.py reset  --http
    python client.py status --http
    python client.py status --http --host 192.168.1.45

Ortam değişkenleri (shell history'ye düşmemesi için):
    PC_SWITCH_MQTT_BROKER   → --broker yerine
    PC_SWITCH_MQTT_USER     → --mqtt-user yerine
    PC_SWITCH_MQTT_PASS     → --mqtt-pass yerine
    PC_SWITCH_API_KEY       → --key yerine
"""

import argparse
import json
import os
import sys
import time
import ssl
from typing import Optional

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("Eksik bağımlılık: pip install paho-mqtt")

try:
    import requests
except ImportError:
    sys.exit("Eksik bağımlılık: pip install requests")

# ─── Topic sabitleri ──────────────────────────────────────────────────────────
TOPIC_CMD       = "pcswitch/command"
TOPIC_STATE     = "pcswitch/state"
TOPIC_HEARTBEAT = "pcswitch/heartbeat"
TOPIC_LWT       = "pcswitch/lwt"

# ─── MQTT yardımcıları ────────────────────────────────────────────────────────

def _build_tls_context() -> ssl.SSLContext:
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx


def mqtt_command(broker: str, user: str, password: str, action: str, timeout: int = 8) -> Optional[dict]:
    """MQTT üzerinden komut gönder ve yanıtı bekle."""
    result: Optional[dict] = None
    received = False

    def on_connect(client, userdata, connect_flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"[MQTT] Bağlantı hatası: {reason_code}", file=sys.stderr)
        else:
            client.subscribe(TOPIC_STATE)

    def on_message(client, userdata, msg):
        nonlocal result, received
        try:
            result = json.loads(msg.payload.decode())
        except json.JSONDecodeError:
            result = {"raw": msg.payload.decode()}
        received = True
        client.disconnect()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="pcswitch-cli", clean_session=True)
    client.username_pw_set(user, password)
    client.tls_set_context(_build_tls_context())
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(broker, 8883, keepalive=10)
    except Exception as e:
        sys.exit(f"[MQTT] Bağlanılamadı: {e}")

    # action == "status" ise cihazın yanıt vermesini bekle
    # action == "power" / "reset" ise yanıt gelmeyebilir, 2s bekle
    wait_for_reply = (action == "status")

    payload = json.dumps({"action": action})
    client.loop_start()
    time.sleep(0.5)  # subscribe'ın gelmesi için kısa bekleme
    client.publish(TOPIC_CMD, payload, qos=1)

    deadline = time.time() + timeout
    while time.time() < deadline:
        if received or not wait_for_reply and time.time() > deadline - (timeout - 2):
            break
        time.sleep(0.1)

    client.loop_stop()
    client.disconnect()
    return result


def mqtt_online_check(broker: str, user: str, password: str, watch: bool = False, timeout: int = 5) -> None:
    """LWT retained mesajından online durumunu oku; --watch ile sürekli izle."""
    last_heartbeat_time: Optional[float] = None
    lwt_data: Optional[dict] = None

    def on_connect(client, userdata, connect_flags, reason_code, properties):
        if not reason_code.is_failure:
            client.subscribe(TOPIC_LWT)
            client.subscribe(TOPIC_HEARTBEAT)

    def on_message(client, userdata, msg):
        nonlocal last_heartbeat_time, lwt_data
        try:
            data = json.loads(msg.payload.decode())
        except json.JSONDecodeError:
            return

        if msg.topic == TOPIC_LWT:
            lwt_data = data
            online = data.get("online", False)
            status = "CEVRIMICI" if online else "CEVRIMDISI"
            print(f"[Durum] Cihaz: {status}")
            if not watch:
                client.disconnect()

        elif msg.topic == TOPIC_HEARTBEAT:
            last_heartbeat_time = time.time()
            uptime  = data.get("uptime_ms", 0)
            rssi    = data.get("rssi", 0)
            secs    = uptime // 1000
            print(f"[Heartbeat] Uptime: {secs}s  RSSI: {rssi} dBm  ({time.strftime('%H:%M:%S')})")

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="pcswitch-cli-watch", clean_session=True)
    client.username_pw_set(user, password)
    client.tls_set_context(_build_tls_context())
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(broker, 8883, keepalive=30)
    except Exception as e:
        sys.exit(f"[MQTT] Bağlanılamadı: {e}")

    if not watch:
        client.loop_start()
        time.sleep(timeout)
        if lwt_data is None:
            print("[Durum] Cihazdan yanıt alınamadı (timeout)")
        client.loop_stop()
        client.disconnect()
        return

    # --watch modu
    print("[İzleme] Çıkmak için Ctrl+C\n")
    client.loop_start()
    try:
        while True:
            time.sleep(5)
            if last_heartbeat_time and time.time() - last_heartbeat_time > 60:
                print("[UYARI] 60s'den fazla heartbeat gelmedi — cihaz yanıt vermiyor olabilir")
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()


# ─── HTTP API yardımcıları ────────────────────────────────────────────────────

def http_request(host: str, api_key: str, method: str, path: str, timeout: int = 5) -> dict:
    url = f"http://{host}{path}"
    headers = {"X-API-Key": api_key}
    try:
        resp = getattr(requests, method)(url, headers=headers, timeout=timeout)
    except requests.exceptions.ConnectionError:
        sys.exit(f"[HTTP] Bağlantı hatası: {url} — Cihaz açık ve aynı ağda mı?")
    except requests.exceptions.Timeout:
        sys.exit(f"[HTTP] Zaman aşımı: {url}")

    if resp.status_code == 401:
        sys.exit("[HTTP] 401 Unauthorized — API anahtarı yanlış")
    if resp.status_code == 409:
        sys.exit("[HTTP] 409 Conflict — Röle meşgul, biraz bekleyip tekrar deneyin")
    if not resp.ok:
        sys.exit(f"[HTTP] Hata {resp.status_code}: {resp.text}")

    try:
        return resp.json()
    except ValueError:
        return {"raw": resp.text}


def _format_status(data: dict) -> str:
    lines = []
    online = data.get("online", data.get("relay_active") is not None)
    lines.append(f"  Durum     : {'Çevrimiçi' if online else 'Çevrimdışı'}")
    if "relay_active" in data:
        lines.append(f"  Röle      : {'AÇIK' if data['relay_active'] else 'Kapalı'}")
    if "uptime_ms" in data:
        secs = data["uptime_ms"] // 1000
        lines.append(f"  Uptime    : {secs}s ({secs // 3600}s {(secs % 3600) // 60}dk {secs % 60}sn)")
    if "rssi" in data:
        lines.append(f"  WiFi RSSI : {data['rssi']} dBm")
    if "ip" in data:
        lines.append(f"  IP        : {data['ip']}")
    if "heap" in data:
        lines.append(f"  Serbest heap : {data['heap']} byte")
    return "\n".join(lines)


# ─── CLI ─────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="pc-switch — ESP8266 PC güç kontrolü",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "action",
        choices=["power", "reset", "status", "online"],
        help="Gerçekleştirilecek eylem",
    )
    parser.add_argument("--http",   action="store_true", help="MQTT yerine HTTP API kullan (sadece LAN)")
    parser.add_argument("--watch",  action="store_true", help="online komutunda sürekli izleme modu")
    parser.add_argument("--host",   default="pcswitch.local", help="HTTP API host (varsayılan: pcswitch.local)")
    parser.add_argument("--broker", default=None,  help="MQTT broker adresi")
    parser.add_argument("--mqtt-user", dest="mqtt_user", default=None, help="MQTT kullanıcı adı")
    parser.add_argument("--mqtt-pass", dest="mqtt_pass", default=None, help="MQTT şifresi")
    parser.add_argument("--key",    default=None,  help="HTTP API anahtarı (X-API-Key)")
    parser.add_argument("--timeout", type=int, default=8, help="Zaman aşımı süresi (sn, varsayılan: 8)")
    parser.add_argument("--json",   action="store_true", help="Ham JSON çıktısı")

    args = parser.parse_args()

    # Ortam değişkeni fallback'leri
    broker    = args.broker    or os.environ.get("PC_SWITCH_MQTT_BROKER", "")
    mqtt_user = args.mqtt_user or os.environ.get("PC_SWITCH_MQTT_USER", "")
    mqtt_pass = args.mqtt_pass or os.environ.get("PC_SWITCH_MQTT_PASS", "")
    api_key   = args.key       or os.environ.get("PC_SWITCH_API_KEY", "")

    # ── HTTP modu ───────────────────────────────────────────────────────────
    if args.http:
        if not api_key:
            sys.exit("API anahtarı gerekli: --key veya PC_SWITCH_API_KEY env değişkeni")
        if args.action == "online":
            sys.exit("'online' komutu sadece MQTT ile çalışır")

        path_map = {"power": "/power", "reset": "/reset", "status": "/status"}
        method   = "get" if args.action == "status" else "post"
        data     = http_request(args.host, api_key, method, path_map[args.action], args.timeout)

        if args.json:
            print(json.dumps(data, indent=2, ensure_ascii=False))
        elif args.action == "status":
            print(_format_status(data))
        else:
            print(f"[OK] {data.get('action','?')} — {data.get('pulse_ms','?')} ms")
        return

    # ── MQTT modu ──────────────────────────────────────────────────────────
    if not broker:
        sys.exit("MQTT broker gerekli: --broker veya PC_SWITCH_MQTT_BROKER env değişkeni")
    if not mqtt_user or not mqtt_pass:
        sys.exit("MQTT kimlik bilgileri gerekli: --mqtt-user / --mqtt-pass veya ortam değişkenleri")

    if args.action == "online":
        mqtt_online_check(broker, mqtt_user, mqtt_pass, watch=args.watch, timeout=args.timeout)
        return

    result = mqtt_command(broker, mqtt_user, mqtt_pass, args.action, timeout=args.timeout)

    if args.action == "status":
        if result is None:
            sys.exit("[MQTT] Yanıt alınamadı — cihaz açık mı?")
        if args.json:
            print(json.dumps(result, indent=2, ensure_ascii=False))
        else:
            print(_format_status(result))
    else:
        print(f"[OK] '{args.action}' komutu gönderildi")
        if result and args.json:
            print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
