import paho.mqtt.client as mqtt
import datetime
import os
import csv
import json

# ================= CONFIG =================
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
LOG_FILE = r"D:\MOSQUITTO\system_log.csv"

EVENT_TOPICS = ["esp32s3/lampState", "esp32s3/pumpState", "esp32s3/modeState"]
SENSOR_TOPICS = ["esp32s3/temp", "esp32s3/lux"]

# ============== GLOBAL DATA ==============
latest_data = {"temp": "", "lux": ""}
sensor_history = []

device_state = {"lamp": "N/A", "pump": "N/A", "mode": "N/A"}
device_user  = {"lamp": "UNKNOWN", "pump": "UNKNOWN", "mode": "UNKNOWN"}

last_logged_state = {"lamp": None, "pump": None, "mode": None}

# ============== CREATE FILE ==============
if not os.path.exists(LOG_FILE):
    os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
    with open(LOG_FILE, mode='w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['Date', 'Time', 'Lamp', 'Pump', 'Mode', 'User', 'Lux', 'Temp'])

# ============== GET DATA -1s ==============
def get_data_1s_before(now):
    target_time = now - datetime.timedelta(seconds=1)
    for record in reversed(sensor_history):
        if record[0] <= target_time:
            return record
    return sensor_history[-1] if sensor_history else None

# ============== MQTT CALLBACK ==============
def on_message(client, userdata, message):
    global latest_data, sensor_history, device_state, device_user, last_logged_state

    try:
        topic = message.topic
        raw_payload = message.payload.decode("utf-8")
        now = datetime.datetime.now()

        # ===== Parse payload (JSON hoặc text thường) =====
        try:
            data = json.loads(raw_payload)
            payload = data.get("state", "").upper()
            user = data.get("user", "UNKNOWN")
        except:
            payload = raw_payload.upper()
            user = "UNKNOWN"

        # ---------- SENSOR ----------
        if topic == "esp32s3/temp":
            latest_data["temp"] = payload
        elif topic == "esp32s3/lux":
            latest_data["lux"] = payload

        if topic in SENSOR_TOPICS:
            sensor_history.append((now, latest_data["temp"], latest_data["lux"]))
            sensor_history = [x for x in sensor_history if (now - x[0]).total_seconds() <= 15]
            return

        # ---------- EVENT ----------
        key = ""
        if topic == "esp32s3/lampState":
            key = "lamp"
        elif topic == "esp32s3/pumpState":
            key = "pump"
        elif topic == "esp32s3/modeState":
            key = "mode"
        else:
            return

        device_state[key] = payload
        device_user[key] = user

        # Chỉ log khi có thay đổi
        if last_logged_state[key] == payload:
            return
        last_logged_state[key] = payload

        # Lấy sensor trước 1 giây
        past_data = get_data_1s_before(now)
        temp_val, lux_val = (past_data[1], past_data[2]) if past_data else ("", "")

        # ---------- GHI CSV ----------
        try:
            with open(LOG_FILE, mode='a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow([
                    now.strftime("%Y-%m-%d"),
                    now.strftime("%H:%M:%S"),
                    device_state["lamp"],
                    device_state["pump"],
                    device_state["mode"],
                    device_user[key],   
                    lux_val,
                    temp_val
                ])
        except PermissionError:
            pass

    except Exception:
        pass

# ============== RUN ==============
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_message = on_message

try:
    client.connect(MQTT_BROKER, MQTT_PORT)

    for topic in EVENT_TOPICS + SENSOR_TOPICS:
        client.subscribe(topic)

    client.loop_forever()

except:
    pass