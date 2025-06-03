# badge_mqtt_bridge.py
import paho.mqtt.client as mqtt
import serial
import time
import json
import threading
import logging
import importlib.metadata # For version checking if needed later

# --- Configuration ---
BADGE_SERIAL_PORT = "/dev/ttyACM0"  # <<< CHANGE THIS if your port is different
MQTT_BROKER = "rpi2" # <<< UPDATED BROKER IP/HOSTNAME
MQTT_PORT = 1883
MQTT_USER = "your_mqtt_user"          # <<< CHANGE OR REMOVE if no auth
MQTT_PASSWORD = "your_mqtt_password"  # <<< CHANGE OR REMOVE if no auth
MQTT_KEEPALIVE = 60 # Seconds for MQTT keepalive

DEVICE_NAME = "DORS/CLUC Badge Notifier"
DEVICE_MANUFACTURER = "Hyperglitch Ltd / DORS/CLUC"
DEVICE_MODEL = "DC2025 Badge"
DEVICE_SW_VERSION = "mqtt_bridge_v1.3" # Bridge script version (incremented)

# Unique ID for the device (important for HA)
DEVICE_UNIQUE_ID = "dc2025_badge_notifier_01" # Ensure unique if multiple badges
BASE_TOPIC_PREFIX = "badge_notifier" # Prefix for specific command/state topics for this badge
DEVICE_BASE_TOPIC = f"{BASE_TOPIC_PREFIX}/{DEVICE_UNIQUE_ID}" # e.g., badge_notifier/dc2025_badge_notifier_01

# MQTT Discovery Prefix (usually "homeassistant")
DISCOVERY_PREFIX = "homeassistant"

# Default states and display behavior
DEFAULT_BADGE_BRIGHTNESS = 20 # 0-99 (as per badge firmware Bxx command)
TEXT_DISPLAY_MIN_DURATION_SECONDS = 3
CHARS_PER_SECOND_ESTIMATE = 5 # For extending display time based on text length
TIME_UPDATE_INTERVAL_SECONDS = 1 # How often to update time on badge when idle

# --- Logging ---
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger("BadgeMQTTBridge")

# --- Global Variables ---
ser = None
serial_lock = threading.Lock() # For thread-safe access to serial port
mqtt_client_global = None # To allow access from signal handler
time_to_die_event = threading.Event() # For signaling threads to stop

# State management for display (text vs. time)
display_mode_lock = threading.Lock()
is_showing_custom_text = False
custom_text_end_time = 0
last_displayed_content_on_badge = "" # Tracks what was last sent as "S..."
current_badge_brightness = DEFAULT_BADGE_BRIGHTNESS # Assumed initial, updated by commands


# --- Badge Serial Communication ---
def connect_serial_port():
    global ser
    if ser and ser.is_open:
        return True
    try:
        logger.info(f"Attempting to connect to badge on {BADGE_SERIAL_PORT}...")
        ser = serial.Serial(BADGE_SERIAL_PORT, 115200, timeout=1, write_timeout=1)
        time.sleep(0.2)  # Allow port to open
        logger.info(f"Successfully connected to badge on {BADGE_SERIAL_PORT}")
        return True
    except serial.SerialException as e:
        logger.error(f"Serial connection error: {e}")
        ser = None
        return False
    except Exception as e:
        logger.error(f"Unexpected error connecting to serial: {e}")
        ser = None
        return False

def send_to_badge(command_str, max_retries=1):
    global ser
    if not command_str.endswith('\r\n'):
        command_str += '\r\n'

    with serial_lock:
        for attempt in range(max_retries + 1):
            if ser is None or not ser.is_open:
                if not connect_serial_port():
                    if attempt < max_retries:
                        logger.warning("Badge not connected, retrying send...")
                        time.sleep(1)
                        continue
                    else:
                        logger.error("Badge not connected, command failed after serial retries.")
                        return False
            try:
                logger.info(f"Sending to badge: {command_str.strip()}")
                ser.write(command_str.encode('utf-8'))
                time.sleep(0.05) # Give badge a moment
                return True
            except serial.SerialTimeoutException:
                logger.error(f"Serial write timeout writing to badge (attempt {attempt+1}).")
            except Exception as e:
                logger.error(f"Error writing to badge (attempt {attempt+1}): {e}")
            
            if ser: # If an error occurred, close and nullify to force reconnect
                try: ser.close()
                except: pass
            ser = None
            if attempt < max_retries: time.sleep(0.5)
            else: logger.error("Command failed after retries due to write error.")
        return False

# --- Display Management Thread ---
def display_manager_thread_func():
    global is_showing_custom_text, custom_text_end_time, last_displayed_content_on_badge, current_badge_brightness
    
    # Set initial brightness on badge when this thread starts
    send_to_badge(f"B{current_badge_brightness:02}")
    send_to_badge("C") # Initial clear

    logger.info("Display manager thread started.")
    while not time_to_die_event.is_set():
        current_time_epoch = time.time()
        display_time_now = False

        with display_mode_lock:
            if is_showing_custom_text:
                if current_time_epoch >= custom_text_end_time:
                    is_showing_custom_text = False
                    display_time_now = True
                    logger.info("Custom text duration ended. Switching to time display.")
            else: # Not showing custom text, so should show time
                display_time_now = True

        if display_time_now:
            # UPDATED TIME FORMAT to HHMMSS (6 characters, fits perfectly)
            time_str_for_badge = time.strftime("%H%M%S")
            
            # Only update if different from last content or if switched from custom text
            if time_str_for_badge != last_displayed_content_on_badge or not is_showing_custom_text:
                logger.debug(f"Displaying time: {time_str_for_badge}")
                if send_to_badge(f"S{time_str_for_badge}"):
                    last_displayed_content_on_badge = time_str_for_badge

        time.sleep(TIME_UPDATE_INTERVAL_SECONDS) # Control update frequency
    logger.info("Display manager thread stopped.")

# --- MQTT Callback Functions ---
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info("Connected to MQTT Broker!")
        client.publish(f"{DEVICE_BASE_TOPIC}/status", "online", retain=True, qos=1)
        publish_discovery_messages(client)
        set_initial_badge_and_mqtt_states(client) # Publish initial states

        # Subscribe to command topics
        client.subscribe(f"{DEVICE_BASE_TOPIC}/text_display/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/brightness/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/fade_button/press")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/clear_button/press")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/logo_alert_light/set")
        logger.info("Subscribed to command topics.")
    else:
        logger.error(f"Failed to connect to MQTT, return code {rc}")

def on_disconnect(client, userdata, rc, properties=None):
    logger.warning(f"Disconnected from MQTT Broker with result code {rc}.")
    # LWT should publish "offline"

def on_message(client, userdata, msg):
    global is_showing_custom_text, custom_text_end_time, last_displayed_content_on_badge, current_badge_brightness
    topic = msg.topic
    payload_str = msg.payload.decode('utf-8', errors='replace').strip()
    logger.info(f"Received MQTT: Topic='{topic}', Payload='{payload_str}'")

    entity_topic = topic.replace(f"{DEVICE_BASE_TOPIC}/", "")

    if entity_topic == "text_display/set":
        with display_mode_lock:
            text_to_show = payload_str[:30]
            if send_to_badge(f"S{text_to_show}"):
                last_displayed_content_on_badge = text_to_show # Update what's on badge
                is_showing_custom_text = True
                duration = TEXT_DISPLAY_MIN_DURATION_SECONDS + (len(text_to_show) / CHARS_PER_SECOND_ESTIMATE)
                custom_text_end_time = time.time() + duration
                logger.info(f"Displaying custom text '{text_to_show}' for ~{duration:.1f} seconds.")
                client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", text_to_show, retain=True, qos=1)

    elif entity_topic == "brightness/set":
        try:
            brightness_val = int(payload_str)
            if 0 <= brightness_val <= 99:
                if send_to_badge(f"B{brightness_val:02}"):
                    current_badge_brightness = brightness_val # Update our tracked brightness
                    client.publish(f"{DEVICE_BASE_TOPIC}/brightness/state", str(brightness_val), retain=True, qos=1)
            else:
                logger.warning(f"Brightness value out of range (0-99): {brightness_val}")
        except ValueError:
            logger.warning(f"Invalid brightness value: {payload_str}")

    elif entity_topic == "fade_button/press" and payload_str.upper() == "PRESS":
        send_to_badge("F")

    elif entity_topic == "clear_button/press" and payload_str.upper() == "PRESS":
        if send_to_badge("C"):
            with display_mode_lock: # Clear current text display state
                is_showing_custom_text = False
                custom_text_end_time = 0 # Stop any custom text display
                last_displayed_content_on_badge = "" # Force time update next cycle
            client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
            client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)

    elif entity_topic == "logo_alert_light/set":
        all_leds_set_successfully = True
        target_state_str = "OFF"
        if payload_str.upper() == "ON":
            target_state_str = "ON"
            for i in range(39):
                if not send_to_badge(f"L{i:02}1"): all_leds_set_successfully = False; break
        elif payload_str.upper() == "OFF":
            for i in range(39):
                if not send_to_badge(f"L{i:02}0"): all_leds_set_successfully = False; break
        
        if all_leds_set_successfully:
            client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", target_state_str, retain=True, qos=1)

# --- MQTT Discovery and Initial State Publishing ---
def publish_discovery_messages(client):
    logger.info("Publishing MQTT discovery messages...")
    device_info = {
        "identifiers": [DEVICE_UNIQUE_ID], "name": DEVICE_NAME,
        "manufacturer": DEVICE_MANUFACTURER, "model": DEVICE_MODEL,
        "sw_version": DEVICE_SW_VERSION,
        "configuration_url": "http://hyperglitch.com/articles/dc2025-badge" # Example
    }
    common_entity_payload = {
        "device": device_info, "optimistic": False, "retain": True, "qos":1,
        "availability_topic": f"{DEVICE_BASE_TOPIC}/status",
        "payload_available": "online", "payload_not_available": "offline"
    }

    text_cfg_topic = f"{DISCOVERY_PREFIX}/text/{DEVICE_UNIQUE_ID}/badge_display_text/config"
    text_payload = {**common_entity_payload, "name": "Badge Text", "unique_id": f"{DEVICE_UNIQUE_ID}_text",
                    "state_topic": f"{DEVICE_BASE_TOPIC}/text_display/state",
                    "command_topic": f"{DEVICE_BASE_TOPIC}/text_display/set",
                    "min": 0, "max": 30, "pattern": "^[ -~]*$"}
    client.publish(text_cfg_topic, json.dumps(text_payload), retain=True, qos=1)

    bright_cfg_topic = f"{DISCOVERY_PREFIX}/number/{DEVICE_UNIQUE_ID}/badge_brightness/config"
    bright_payload = {**common_entity_payload, "name": "Badge Brightness", "unique_id": f"{DEVICE_UNIQUE_ID}_brightness",
                      "state_topic": f"{DEVICE_BASE_TOPIC}/brightness/state",
                      "command_topic": f"{DEVICE_BASE_TOPIC}/brightness/set",
                      "min": 0, "max": 99, "step": 1, "unit_of_measurement": "%", "mode": "slider"}
    client.publish(bright_cfg_topic, json.dumps(bright_payload), retain=True, qos=1)

    fade_btn_cfg_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/badge_trigger_fade/config"
    fade_btn_payload = {"name": "Badge Fade", "unique_id": f"{DEVICE_UNIQUE_ID}_fade",
                        "command_topic": f"{DEVICE_BASE_TOPIC}/fade_button/press",
                        "device": device_info, "retain": False, "qos":0,
                        "availability_topic": f"{DEVICE_BASE_TOPIC}/status",
                        "payload_available": "online", "payload_not_available": "offline"}
    client.publish(fade_btn_cfg_topic, json.dumps(fade_btn_payload), retain=True, qos=1)

    clear_btn_cfg_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/badge_clear_display/config"
    clear_btn_payload = {"name": "Badge Clear", "unique_id": f"{DEVICE_UNIQUE_ID}_clear",
                         "command_topic": f"{DEVICE_BASE_TOPIC}/clear_button/press",
                         "device": device_info, "retain": False, "qos":0,
                         "availability_topic": f"{DEVICE_BASE_TOPIC}/status",
                         "payload_available": "online", "payload_not_available": "offline"}
    client.publish(clear_btn_cfg_topic, json.dumps(clear_btn_payload), retain=True, qos=1)

    logo_light_cfg_topic = f"{DISCOVERY_PREFIX}/light/{DEVICE_UNIQUE_ID}/badge_logo_alert/config"
    logo_light_payload = {**common_entity_payload, "name": "Badge Logo Alert", "unique_id": f"{DEVICE_UNIQUE_ID}_logo_light",
                          "schema": "basic", 
                          "state_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/state",
                          "command_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/set",
                          "payload_on": "ON", "payload_off": "OFF"}
    client.publish(logo_light_cfg_topic, json.dumps(logo_light_payload), retain=True, qos=1)

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
    
    all_logo_off_success = True
    for i in range(39):
        if not send_to_badge(f"L{i:02}0"): all_logo_off_success = False; break
    if all_logo_off_success:
        client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)

# --- Main Script Execution ---
def main_mqtt_loop():
    global mqtt_client_global 

    display_thread = threading.Thread(target=display_manager_thread_func, daemon=True)
    display_thread.start()

    mqtt_client_global = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"badge_bridge_{DEVICE_UNIQUE_ID}")
    if MQTT_USER and MQTT_PASSWORD:
        mqtt_client_global.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
    mqtt_client_global.will_set(f"{DEVICE_BASE_TOPIC}/status", payload="offline", qos=1, retain=True)
    
    mqtt_client_global.on_connect = on_connect
    mqtt_client_global.on_disconnect = on_disconnect
    mqtt_client_global.on_message = on_message

    while not time_to_die_event.is_set():
        try:
            if not mqtt_client_global.is_connected():
                logger.info(f"Attempting to connect to MQTT broker: {MQTT_BROKER}...")
                # Add a timeout to the connect call
                mqtt_client_global.connect(MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE)
                mqtt_client_global.loop_start()
            time.sleep(5) 
        except ConnectionRefusedError:
            logger.error("MQTT connection refused. Retrying in 10 seconds...")
            if mqtt_client_global.is_connected(): mqtt_client_global.loop_stop()
            time.sleep(10)
        except Exception as e:
            logger.error(f"MQTT or other main loop error: {e}. Retrying in 10 seconds...")
            if mqtt_client_global and mqtt_client_global.is_connected(): mqtt_client_global.loop_stop()
            time.sleep(10)

if __name__ == "__main__":
    try:
        main_mqtt_loop()
    except KeyboardInterrupt:
        logger.info("Keyboard interrupt received. Shutting down...")
    finally:
        logger.info("Initiating shutdown sequence...")
        time_to_die_event.set() 

        logger.info("Cleaning up MQTT...")
        if mqtt_client_global:
            if mqtt_client_global.is_connected():
                try:
                    mqtt_client_global.publish(f"{DEVICE_BASE_TOPIC}/status", "offline", retain=True, qos=1)
                    logger.info("Published 'offline' status via MQTT.")
                except Exception as e:
                    logger.error(f"Error publishing 'offline' status: {e}")
                try:
                    mqtt_client_global.loop_stop() # No 'force' argument
                    logger.info("MQTT loop stopped.")
                except Exception as e:
                    logger.error(f"Error stopping MQTT loop: {e}")
                try:
                    mqtt_client_global.disconnect()
                    logger.info("Disconnected from MQTT broker.")
                except Exception as e:
                    logger.error(f"Error disconnecting from MQTT broker: {e}")
            else:
                 try: mqtt_client_global.loop_stop()
                 except: pass
        
        # Wait for display manager thread to finish its current iteration if it's crucial
        # display_thread.join(timeout=2) # This was not defined in main's scope

        logger.info("Cleaning up serial port...")
        with serial_lock:
            if ser and ser.is_open:
                try:
                    send_to_badge(f"B{DEFAULT_BADGE_BRIGHTNESS:02}", max_retries=0)
                    send_to_badge("C", max_retries=0)
                    send_to_badge("SBye...", max_retries=0)
                    time.sleep(0.2) 
                    ser.close()
                    logger.info("Serial port closed.")
                except Exception as e:
                    logger.error(f"Error during final serial cleanup: {e}")
        logger.info("Badge bridge stopped.")