# mqtt.py
#
# A robust MQTT to Serial bridge for controlling the DORS/CLUC 2025 conference badge.
# This script connects to an MQTT broker, exposes badge functionalities to Home Assistant
# via MQTT Discovery, manages the badge display (text/clock), and listens for button
# presses (click and longpress) from the badge to publish as MQTT events.

import paho.mqtt.client as mqtt
import serial
import time
import json
import threading
import logging
import signal

# --- Configuration ---
BADGE_SERIAL_PORT = "/dev/ttyACM0"  # <<< CHANGE THIS if your badge's serial port is different
MQTT_BROKER = "rpi2"               # <<< CHANGE THIS to your MQTT broker's IP or hostname
MQTT_PORT = 1883
MQTT_USER = "your_mqtt_user"       # <<< CHANGE OR REMOVE if your broker has no authentication
MQTT_PASSWORD = "your_mqtt_password" # <<< CHANGE OR REMOVE if your broker has no authentication
MQTT_KEEPALIVE = 60                # Seconds for MQTT keepalive

# --- Home Assistant Device Information ---
DEVICE_NAME = "DORS/CLUC Badge Notifier"
DEVICE_MANUFACTURER = "Hyperglitch Ltd / DORS/CLUC"
DEVICE_MODEL = "DC2025 Badge"
DEVICE_SW_VERSION = "mqtt_bridge_v1.9" # Version incremented for critical bug fix

# Unique ID for the device (ensure this is unique if you run bridges for multiple badges)
DEVICE_UNIQUE_ID = "dc2025_badge_notifier_01"
BASE_TOPIC_PREFIX = "badge_notifier"
DEVICE_BASE_TOPIC = f"{BASE_TOPIC_PREFIX}/{DEVICE_UNIQUE_ID}"

# MQTT Discovery Prefix (usually "homeassistant" for Home Assistant)
DISCOVERY_PREFIX = "homeassistant"

# --- Badge Behavior Configuration ---
DEFAULT_BADGE_BRIGHTNESS = 20
TEXT_DISPLAY_MIN_DURATION_SECONDS = 3
CHARS_PER_SECOND_ESTIMATE = 5
TIME_UPDATE_INTERVAL_SECONDS = 1

# --- Logging Setup ---
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger("BadgeMQTTBridge")

# --- Global Variables ---
ser = None
serial_lock = threading.Lock()
mqtt_client_global = None
time_to_die_event = threading.Event()
init_done_event = threading.Event()
display_thread_global = None
serial_reader_thread_global = None

# --- Display State Management ---
display_mode_lock = threading.Lock()
is_showing_custom_text = False
custom_text_end_time = 0
last_displayed_content_on_badge = ""
current_badge_brightness = DEFAULT_BADGE_BRIGHTNESS

# --- Signal Handler for Graceful Shutdown ---
def signal_handler(sig, frame):
    logger.info(f"Signal {sig} received. Initiating graceful shutdown.")
    time_to_die_event.set()

# --- Badge Serial Communication ---
def connect_serial_port():
    global ser
    if ser and ser.is_open: return True
    try:
        logger.info(f"Attempting to connect to badge on {BADGE_SERIAL_PORT}...")
        ser = serial.Serial(BADGE_SERIAL_PORT, 115200, timeout=1, write_timeout=1)
        time.sleep(0.2); logger.info(f"Successfully connected to badge on {BADGE_SERIAL_PORT}"); return True
    except Exception as e:
        logger.error(f"Serial connection error: {e}"); ser = None; return False

def send_to_badge(command_str, max_retries=1):
    global ser
    if not command_str.endswith('\r\n'): command_str += '\r\n'
    with serial_lock:
        for attempt in range(max_retries + 1):
            if ser is None or not ser.is_open:
                if not connect_serial_port():
                    if attempt < max_retries: time.sleep(1); continue
                    else: logger.error("Command failed: Badge not connected."); return False
            try:
                logger.info(f"Sending to badge: {command_str.strip()}")
                ser.write(command_str.encode('utf-8')); time.sleep(0.05); return True
            except Exception as e:
                logger.error(f"Error writing to badge on attempt {attempt+1}: {e}")
            if ser:
                try: ser.close()
                except Exception: pass
            ser = None
            if attempt < max_retries: time.sleep(0.5)
    return False

# --- Display Management Thread ---
def display_manager_thread_func():
    ### MODIFIED: Added global declarations to fix UnboundLocalError ###
    global is_showing_custom_text, last_displayed_content_on_badge

    logger.info("Display manager thread started, waiting for initialization...")
    init_done_event.wait()
    logger.info("Initialization complete, display manager taking over.")
    next_time_update_epoch = time.monotonic()
    last_displayed_content_on_badge_was_custom_text = False
    while not time_to_die_event.is_set():
        loop_start_time = time.monotonic(); current_wall_time = time.time(); should_display_time = False
        with display_mode_lock:
            if is_showing_custom_text:
                if current_wall_time >= custom_text_end_time:
                    is_showing_custom_text = False; should_display_time = True
                    next_time_update_epoch = loop_start_time; last_displayed_content_on_badge_was_custom_text = True
            else: should_display_time = True
        if should_display_time and loop_start_time >= next_time_update_epoch:
            time_str = time.strftime("%H%M%S", time.localtime(current_wall_time))
            if last_displayed_content_on_badge_was_custom_text or time_str != last_displayed_content_on_badge:
                if send_to_badge(f"S{time_str}"): last_displayed_content_on_badge = time_str
                last_displayed_content_on_badge_was_custom_text = False
            next_time_update_epoch += TIME_UPDATE_INTERVAL_SECONDS
            if next_time_update_epoch < loop_start_time: next_time_update_epoch = loop_start_time + TIME_UPDATE_INTERVAL_SECONDS
        sleep_duration = min(max(0, custom_text_end_time - current_wall_time), 0.1) if is_showing_custom_text else max(0, next_time_update_epoch - time.monotonic())
        time_to_die_event.wait(timeout=max(0.01, sleep_duration))
    logger.info("Display manager thread stopped.")

# --- Serial Reader Thread for Button Events ---
def serial_reader_thread_func():
    logger.info("Serial reader thread started.")
    while not time_to_die_event.is_set():
        if ser is None or not ser.is_open:
            time_to_die_event.wait(timeout=1.0); continue
        try:
            line_bytes = ser.readline()
            if line_bytes:
                line_str = line_bytes.decode('utf-8', errors='ignore').strip()
                if line_str.startswith('#BTN::') and line_str.endswith('$'):
                    parts = line_str.strip('#$').split('::')
                    if len(parts) == 3 and parts[0] == 'BTN':
                        event_name = f"BTN_{int(parts[1])}_{parts[2]}"
                        logger.info(f"Parsed button event: {event_name}")
                        payload = {"event_type": event_name}
                        if mqtt_client_global and mqtt_client_global.is_connected():
                             mqtt_client_global.publish(f"{DEVICE_BASE_TOPIC}/button_event", json.dumps(payload), qos=1)
        except (serial.SerialException, ValueError, IndexError) as e:
            logger.warning(f"Error processing serial data: {e}"); time_to_die_event.wait(timeout=2.0)
    logger.info("Serial reader thread stopped.")

# --- MQTT Callback Functions ---
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info("Connected to MQTT Broker!")
        client.publish(f"{DEVICE_BASE_TOPIC}/status", "online", retain=True, qos=1)
        publish_discovery_messages(client)
        set_initial_badge_and_mqtt_states(client)
        logger.info("Subscribing to command topics...")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/text_display/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/brightness/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/clear_button/press")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/logo_alert_light/set")
        init_done_event.set()
    else: logger.error(f"Failed to connect to MQTT, return code {rc}")

def on_disconnect(client, userdata, flags, reason_code, properties=None):
    logger.warning("Disconnected from MQTT Broker")
    init_done_event.clear()

def on_message(client, userdata, msg):
    global is_showing_custom_text, custom_text_end_time, last_displayed_content_on_badge, current_badge_brightness
    topic, payload_str = msg.topic, msg.payload.decode('utf-8').strip()
    entity_topic = topic.replace(f"{DEVICE_BASE_TOPIC}/", "")
    logger.info(f"Received MQTT: Topic='{entity_topic}', Payload='{payload_str}'")

    if entity_topic == "text_display/set":
        with display_mode_lock:
            text = payload_str[:30]
            if send_to_badge(f"S{text}"):
                last_displayed_content_on_badge = text; is_showing_custom_text = True
                duration = TEXT_DISPLAY_MIN_DURATION_SECONDS + (len(text) / CHARS_PER_SECOND_ESTIMATE)
                custom_text_end_time = time.time() + duration
                client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", text, retain=True, qos=1)
    elif entity_topic == "brightness/set":
        try:
            val = int(payload_str)
            if 0 <= val <= 99 and send_to_badge(f"B{val:02}"):
                current_badge_brightness = val
                client.publish(f"{DEVICE_BASE_TOPIC}/brightness/state", str(val), retain=True, qos=1)
        except ValueError: pass
    elif entity_topic == "clear_button/press" and payload_str.upper() == "PRESS":
        if send_to_badge("C"):
            with display_mode_lock: is_showing_custom_text = False; custom_text_end_time = 0; last_displayed_content_on_badge = ""
            client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
            client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)
    elif entity_topic == "logo_alert_light/set":
        target_state = payload_str.upper()
        if target_state == "ON":
            if all(send_to_badge(f"L{i:02}1") for i in range(39)):
                client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "ON", retain=True, qos=1)
        elif target_state == "OFF":
            if send_to_badge("C"):
                with display_mode_lock: is_showing_custom_text = False; custom_text_end_time = 0; last_displayed_content_on_badge = ""
                client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
                client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)

# --- MQTT Discovery and Initial State Publishing ---
def publish_discovery_messages(client):
    logger.info("Publishing MQTT discovery messages...")
    device_info = {"identifiers": [DEVICE_UNIQUE_ID], "name": DEVICE_NAME, "manufacturer": DEVICE_MANUFACTURER, "model": DEVICE_MODEL, "sw_version": DEVICE_SW_VERSION}
    common_payload = {"device": device_info, "availability_topic": f"{DEVICE_BASE_TOPIC}/status", "payload_available": "online", "payload_not_available": "offline"}

    text_cfg_topic = f"{DISCOVERY_PREFIX}/text/{DEVICE_UNIQUE_ID}/badge_display_text/config"
    text_payload = {**common_payload, "name": "Badge Text", "unique_id": f"{DEVICE_UNIQUE_ID}_text", "qos":1,
                    "state_topic": f"{DEVICE_BASE_TOPIC}/text_display/state", "command_topic": f"{DEVICE_BASE_TOPIC}/text_display/set"}
    client.publish(text_cfg_topic, json.dumps(text_payload), retain=True)

    bright_cfg_topic = f"{DISCOVERY_PREFIX}/number/{DEVICE_UNIQUE_ID}/badge_brightness/config"
    bright_payload = {**common_payload, "name": "Badge Brightness", "unique_id": f"{DEVICE_UNIQUE_ID}_brightness", "qos":1,
                      "state_topic": f"{DEVICE_BASE_TOPIC}/brightness/state", "command_topic": f"{DEVICE_BASE_TOPIC}/brightness/set",
                      "min": 0, "max": 99, "mode": "slider"}
    client.publish(bright_cfg_topic, json.dumps(bright_payload), retain=True)

    clear_btn_cfg_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/badge_clear_display/config"
    clear_btn_payload = {**common_payload, "name": "Badge Clear", "unique_id": f"{DEVICE_UNIQUE_ID}_clear", "qos":0,
                         "command_topic": f"{DEVICE_BASE_TOPIC}/clear_button/press", "payload_press": "PRESS"}
    client.publish(clear_btn_cfg_topic, json.dumps(clear_btn_payload), retain=True)

    logo_light_cfg_topic = f"{DISCOVERY_PREFIX}/light/{DEVICE_UNIQUE_ID}/badge_logo_alert/config"
    logo_light_payload = {**common_payload, "name": "Badge Logo Alert", "unique_id": f"{DEVICE_UNIQUE_ID}_logo_light", "qos":1,
                          "schema": "basic", "state_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/state",
                          "command_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/set"}
    client.publish(logo_light_cfg_topic, json.dumps(logo_light_payload), retain=True)

    event_cfg_topic = f"{DISCOVERY_PREFIX}/event/{DEVICE_UNIQUE_ID}/button_press/config"
    event_payload = {**common_payload, "name": "Badge Button Press", "unique_id": f"{DEVICE_UNIQUE_ID}_button_event", "qos": 1,
                     "topic": f"{DEVICE_BASE_TOPIC}/button_event",
                     "event_types": ["BTN_0_click", "BTN_0_longpress", "BTN_1_click", "BTN_1_longpress"],
                     "value_template": "{{ value_json.event_type }}"}
    client.publish(event_cfg_topic, json.dumps(event_payload), retain=True)
    logger.info("Discovery messages published.")

def set_initial_badge_and_mqtt_states(client):
    global current_badge_brightness, last_displayed_content_on_badge
    logger.info("Setting initial badge state and publishing to MQTT...")
    current_badge_brightness = DEFAULT_BADGE_BRIGHTNESS
    if send_to_badge(f"B{current_badge_brightness:02}"):
        client.publish(f"{DEVICE_BASE_TOPIC}/brightness/state", str(current_badge_brightness), retain=True, qos=1)
    if send_to_badge("C"):
        last_displayed_content_on_badge = ""
        client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
        client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)
        logger.info("Cleared display and logo LEDs with a single 'C' command.")

# --- Main Script Execution ---
def main_mqtt_loop():
    global mqtt_client_global, display_thread_global, serial_reader_thread_global
    display_thread_global = threading.Thread(target=display_manager_thread_func, daemon=False)
    serial_reader_thread_global = threading.Thread(target=serial_reader_thread_func, daemon=False)
    display_thread_global.start(); serial_reader_thread_global.start()
    mqtt_client_global = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"badge_bridge_{DEVICE_UNIQUE_ID}")
    if MQTT_USER and MQTT_PASSWORD: mqtt_client_global.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    mqtt_client_global.will_set(f"{DEVICE_BASE_TOPIC}/status", payload="offline", qos=1, retain=True)
    mqtt_client_global.on_connect = on_connect; mqtt_client_global.on_disconnect = on_disconnect; mqtt_client_global.on_message = on_message
    while not time_to_die_event.is_set():
        try:
            if not mqtt_client_global.is_connected():
                logger.info(f"Attempting to connect to MQTT broker: {MQTT_BROKER}...")
                mqtt_client_global.connect(MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE)
                mqtt_client_global.loop_start()
            time_to_die_event.wait(timeout=5)
        except Exception as e:
            logger.error(f"MQTT or main loop error: {e}. Retrying...")
            if mqtt_client_global and mqtt_client_global.is_connected(): mqtt_client_global.loop_stop()
            time_to_die_event.wait(timeout=10)

if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal_handler); signal.signal(signal.SIGTERM, signal_handler)
    try:
        main_mqtt_loop()
    finally:
        logger.info("Shutdown sequence started..."); time_to_die_event.set()
        if display_thread_global: display_thread_global.join(2)
        if serial_reader_thread_global: serial_reader_thread_global.join(2)
        if mqtt_client_global:
            if mqtt_client_global.is_connected():
                mqtt_client_global.publish(f"{DEVICE_BASE_TOPIC}/status", "offline", retain=True, qos=1); time.sleep(0.1)
            mqtt_client_global.loop_stop()
            if mqtt_client_global.is_connected(): mqtt_client_global.disconnect()
        with serial_lock:
            if ser and ser.is_open:
                send_to_badge("SBye...", max_retries=0); ser.close()
        logger.info("Badge bridge script stopped.")