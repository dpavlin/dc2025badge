# badge_mqtt_bridge.py
import paho.mqtt.client as mqtt
import serial
import time
import json
import threading
import logging

# --- Configuration ---
BADGE_SERIAL_PORT = "/dev/ttyACM0"  # <<< CHANGE THIS if your port is different
MQTT_BROKER = "rpi2" # <<< CHANGE THIS
MQTT_PORT = 1883
MQTT_USER = "your_mqtt_user"          # <<< CHANGE OR REMOVE if no auth
MQTT_PASSWORD = "your_mqtt_password"  # <<< CHANGE OR REMOVE if no auth
MQTT_KEEPALIVE = 60 # Seconds

DEVICE_NAME = "DORS/CLUC Badge Notifier"
DEVICE_MANUFACTURER = "Hyperglitch Ltd / DORS/CLUC"
DEVICE_MODEL = "DC2025 Badge"
DEVICE_SW_VERSION = "mqtt_bridge_v1.1"

# Unique ID for the device
DEVICE_UNIQUE_ID = "dc2025_badge_notifier_01" # Ensure this is unique if you have multiple badges/bridges
BASE_TOPIC_PREFIX = "badge_notifier" # Prefix for specific command/state topics for this badge
DEVICE_BASE_TOPIC = f"{BASE_TOPIC_PREFIX}/{DEVICE_UNIQUE_ID}" # e.g., badge_notifier/dc2025_badge_notifier_01

# MQTT Discovery Prefix (usually "homeassistant")
DISCOVERY_PREFIX = "homeassistant"

# Default states
DEFAULT_BADGE_BRIGHTNESS = 20 # 0-99
TEXT_DISPLAY_DURATION_SECONDS = 3 # Minimum time to show a message
CHARS_PER_SECOND_ESTIMATE = 5 # For extending display time based on text length

# --- Logging ---
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# --- Global Variables ---
ser = None
serial_lock = threading.Lock()
mqtt_client = None
time_to_die = threading.Event()

# State management for display (text vs. time)
display_mode_lock = threading.Lock()
is_showing_custom_text = False
custom_text_end_time = 0
last_displayed_text = ""
last_set_brightness = DEFAULT_BADGE_BRIGHTNESS

# --- Badge Serial Communication ---
def connect_serial():
    global ser
    try:
        logging.info(f"Attempting to connect to badge on {BADGE_SERIAL_PORT}...")
        ser = serial.Serial(BADGE_SERIAL_PORT, 115200, timeout=1, write_timeout=1)
        time.sleep(0.2)  # Allow port to open
        logging.info(f"Successfully connected to badge on {BADGE_SERIAL_PORT}")
        return True
    except serial.SerialException as e:
        logging.error(f"Serial connection error: {e}")
        ser = None
        return False
    except Exception as e: # Catch any other potential exceptions
        logging.error(f"Unexpected error connecting to serial: {e}")
        ser = None
        return False

def send_to_badge(command_str, max_retries=1):
    global ser
    if not command_str.endswith('\r\n'):
        command_str += '\r\n'

    with serial_lock:
        for attempt in range(max_retries + 1):
            if ser is None or not ser.is_open:
                if not connect_serial():
                    if attempt < max_retries:
                        logging.warning("Badge not connected, retrying send...")
                        time.sleep(1) # Wait a bit before retrying connection
                        continue
                    else:
                        logging.error("Badge not connected, command failed after retries.")
                        return False
            try:
                logging.info(f"Sending to badge: {command_str.strip()}")
                ser.write(command_str.encode('utf-8'))
                time.sleep(0.05) # Small delay for command processing by badge
                return True
            except serial.SerialTimeoutException:
                logging.error("Serial write timeout writing to badge.")
            except Exception as e:
                logging.error(f"Error writing to badge: {e}")
            
            # If write failed, close port and prepare for reconnect on next send
            if ser:
                try:
                    ser.close()
                except: pass # Ignore errors on close
            ser = None
            if attempt < max_retries:
                logging.warning("Retrying send after error...")
                time.sleep(0.5)
            else:
                logging.error("Command failed after retries due to write error.")
                return False
        return False


# --- Display Management Thread ---
def display_manager_thread():
    global is_showing_custom_text, custom_text_end_time, last_displayed_text, last_set_brightness
    
    # Initial state for the badge when script starts
    send_to_badge(f"B{last_set_brightness:02}")
    send_to_badge("C") # Clear

    while not time_to_die.is_set():
        current_time = time.time()
        show_time_now = False

        with display_mode_lock:
            if is_showing_custom_text:
                if current_time >= custom_text_end_time:
                    is_showing_custom_text = False
                    show_time_now = True
                    logging.info("Custom text duration ended. Switching to time display.")
            else: # Not showing custom text, so should show time
                show_time_now = True

        if show_time_now:
            time_str = time.strftime("%H%M%S") # HHMM format
            # Add a space if it's an even second for a subtle blink effect on some displays
            # Or just use static format: time_str = time.strftime(" %H%M ")
            # For 9-segment, HHMM might be better than HH:MM as ':' is hard.
            # If display_text_update handles the colon from ':', then "%H:%M"
            current_time_display_text = f"{time_str}"
            if current_time_display_text != last_displayed_text or not is_showing_custom_text: # Update if time changed or switched from custom
                send_to_badge(f"S{current_time_display_text}")
                last_displayed_text = current_time_display_text
                # Ensure brightness is at the default/low level for time display
                # This assumes 'B' command sets brightness for subsequent 'S' commands.
                # If brightness is very dynamic, might need to re-send with S.
                # send_to_badge(f"B{DEFAULT_BADGE_BRIGHTNESS:02}") # Or a specific time brightness

        time.sleep(1) # Update time display approximately every second


# --- MQTT Callbacks ---
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logging.info("Connected to MQTT Broker!")
        client.publish(f"{DEVICE_BASE_TOPIC}/status", "online", retain=True, qos=1)
        publish_discovery_messages(client)
        set_initial_badge_and_mqtt_states(client) # Publish initial states

        # Subscribe to command topics
        client.subscribe(f"{DEVICE_BASE_TOPIC}/text_display/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/brightness/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/fade_button/press")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/clear_button/press")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/logo_alert_light/set")
        logging.info("Subscribed to command topics.")
    else:
        logging.error(f"Failed to connect to MQTT, return code {rc}")

def on_disconnect(client, userdata, rc, properties=None):
    logging.warning(f"Disconnected from MQTT Broker with result code {rc}. Will attempt to reconnect.")
    # LWT should handle publishing "offline"

def on_message(client, userdata, msg):
    global is_showing_custom_text, custom_text_end_time, last_displayed_text, last_set_brightness
    topic = msg.topic
    payload_str = msg.payload.decode('utf-8', errors='replace').strip()
    logging.info(f"Received MQTT: Topic='{topic}', Payload='{payload_str}'")

    entity_topic = topic.replace(f"{DEVICE_BASE_TOPIC}/", "")

    if entity_topic == "text_display/set":
        with display_mode_lock:
            text_to_show = payload_str[:30] # Badge S command limit (badge handles scrolling)
            send_to_badge(f"S{text_to_show}")
            last_displayed_text = text_to_show
            is_showing_custom_text = True
            # Calculate duration: base + extra for length
            duration = TEXT_DISPLAY_DURATION_SECONDS + (len(text_to_show) / CHARS_PER_SECOND_ESTIMATE)
            custom_text_end_time = time.time() + duration
            logging.info(f"Displaying custom text '{text_to_show}' for ~{duration:.1f} seconds.")
        client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", text_to_show, retain=True, qos=1)

    elif entity_topic == "brightness/set":
        try:
            brightness_val = int(payload_str)
            if 0 <= brightness_val <= 99:
                if send_to_badge(f"B{brightness_val:02}"):
                    last_set_brightness = brightness_val
                    client.publish(f"{DEVICE_BASE_TOPIC}/brightness/state", str(brightness_val), retain=True, qos=1)
            else:
                logging.warning(f"Brightness value out of range (0-99): {brightness_val}")
        except ValueError:
            logging.warning(f"Invalid brightness value: {payload_str}")

    elif entity_topic == "fade_button/press" and payload_str.upper() == "PRESS":
        send_to_badge("F")

    elif entity_topic == "clear_button/press" and payload_str.upper() == "PRESS":
        send_to_badge("C")
        with display_mode_lock: # Clear current text display state
            is_showing_custom_text = False
            custom_text_end_time = 0
            last_displayed_text = "" # So time will be displayed next
        client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
        # Optionally turn off logo light state too
        client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)


    elif entity_topic == "logo_alert_light/set":
        if payload_str.upper() == "ON":
            for i in range(39): send_to_badge(f"L{i:02}1")
            client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "ON", retain=True, qos=1)
        elif payload_str.upper() == "OFF":
            for i in range(39): send_to_badge(f"L{i:02}0")
            client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)

# --- MQTT Discovery and Initial State ---
def publish_discovery_messages(client):
    logging.info("Publishing MQTT discovery messages...")
    device_info = {
        "identifiers": [DEVICE_UNIQUE_ID], "name": DEVICE_NAME,
        "manufacturer": DEVICE_MANUFACTURER, "model": DEVICE_MODEL,
        "sw_version": DEVICE_SW_VERSION
    }
    common_payload = {
        "device": device_info, "optimistic": False, "retain": True, "qos":1,
        "availability_topic": f"{DEVICE_BASE_TOPIC}/status",
        "payload_available": "online", "payload_not_available": "offline"
    }

    # Text Entity
    text_cfg_topic = f"{DISCOVERY_PREFIX}/text/{DEVICE_UNIQUE_ID}/badge_display_text/config"
    text_payload = {**common_payload, "name": "Badge Text", "unique_id": f"{DEVICE_UNIQUE_ID}_text",
                    "state_topic": f"{DEVICE_BASE_TOPIC}/text_display/state",
                    "command_topic": f"{DEVICE_BASE_TOPIC}/text_display/set",
                    "min": 0, "max": 30, "pattern": "^[ -~]*$"} # Allow empty string
    client.publish(text_cfg_topic, json.dumps(text_payload), retain=True, qos=1)

    # Brightness Number Entity
    bright_cfg_topic = f"{DISCOVERY_PREFIX}/number/{DEVICE_UNIQUE_ID}/badge_brightness/config"
    bright_payload = {**common_payload, "name": "Badge Brightness", "unique_id": f"{DEVICE_UNIQUE_ID}_brightness",
                      "state_topic": f"{DEVICE_BASE_TOPIC}/brightness/state",
                      "command_topic": f"{DEVICE_BASE_TOPIC}/brightness/set",
                      "min": 0, "max": 99, "step": 1, "unit_of_measurement": "%", "mode": "slider"}
    client.publish(bright_cfg_topic, json.dumps(bright_payload), retain=True, qos=1)

    # Fade Button
    fade_btn_cfg_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/badge_trigger_fade/config"
    fade_btn_payload = {**common_payload, "name": "Badge Fade", "unique_id": f"{DEVICE_UNIQUE_ID}_fade",
                        "command_topic": f"{DEVICE_BASE_TOPIC}/fade_button/press", "retain": False} # Buttons don't retain command
    client.publish(fade_btn_cfg_topic, json.dumps(fade_btn_payload), retain=True, qos=1)

    # Clear Button
    clear_btn_cfg_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/badge_clear_display/config"
    clear_btn_payload = {**common_payload, "name": "Badge Clear", "unique_id": f"{DEVICE_UNIQUE_ID}_clear",
                         "command_topic": f"{DEVICE_BASE_TOPIC}/clear_button/press", "retain": False}
    client.publish(clear_btn_cfg_topic, json.dumps(clear_btn_payload), retain=True, qos=1)

    # Logo Alert Light
    logo_light_cfg_topic = f"{DISCOVERY_PREFIX}/light/{DEVICE_UNIQUE_ID}/badge_logo_alert/config"
    logo_light_payload = {**common_payload, "name": "Badge Logo Alert", "unique_id": f"{DEVICE_UNIQUE_ID}_logo_light",
                          "state_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/state",
                          "command_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/set",
                          "payload_on": "ON", "payload_off": "OFF"}
    client.publish(logo_light_cfg_topic, json.dumps(logo_light_payload), retain=True, qos=1)

    logging.info("Discovery messages published.")

def set_initial_badge_and_mqtt_states(client):
    global last_set_brightness, last_displayed_text
    logging.info("Setting initial badge state and publishing to MQTT...")
    
    # Set initial brightness on badge and publish state
    last_set_brightness = DEFAULT_BADGE_BRIGHTNESS
    if send_to_badge(f"B{last_set_brightness:02}"):
        client.publish(f"{DEVICE_BASE_TOPIC}/brightness/state", str(last_set_brightness), retain=True, qos=1)
    
    # Clear badge display and publish empty text state
    if send_to_badge("C"):
        last_displayed_text = "" # So time will display
        client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
    
    # Set logo light to OFF and publish state
    all_logo_off_success = True
    for i in range(39):
        if not send_to_badge(f"L{i:02}0"):
            all_logo_off_success = False; break
    if all_logo_off_success:
        client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)


# --- Main Script Execution ---
def main():
    global mqtt_client # Allow keep_alive_thread to access the client

    # Start display manager thread
    display_thread = threading.Thread(target=display_manager_thread, daemon=True)
    display_thread.start()

    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"badge_bridge_{DEVICE_UNIQUE_ID}")
    if MQTT_USER and MQTT_PASSWORD:
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
    # Set LWT (Last Will and Testament)
    mqtt_client.will_set(f"{DEVICE_BASE_TOPIC}/status", payload="offline", qos=1, retain=True)
    
    mqtt_client.on_connect = on_connect
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.on_message = on_message

    while not time_to_die.is_set():
        try:
            if not mqtt_client.is_connected():
                logging.info(f"Attempting to connect to MQTT broker: {MQTT_BROKER}...")
                mqtt_client.connect(MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE)
                mqtt_client.loop_start() # Start network loop in background thread
            # Keep main thread alive, or do other periodic tasks if needed
            time.sleep(5) # Check connection periodically or just let loop_start handle it

        except ConnectionRefusedError:
            logging.error("MQTT connection refused. Retrying in 10 seconds...")
            mqtt_client.loop_stop() # Stop loop if connect failed
            time.sleep(10)
        except Exception as e:
            logging.error(f"MQTT Error: {e}. Retrying in 10 seconds...")
            if mqtt_client.is_connected():
                mqtt_client.loop_stop()
            time.sleep(10)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        logging.info("Keyboard interrupt received. Shutting down...")
    finally:
        time_to_die.set()
        logging.info("Cleaning up MQTT...")
        if mqtt_client and mqtt_client.is_connected():
            mqtt_client.publish(f"{DEVICE_BASE_TOPIC}/status", "offline", retain=True, qos=1)
            mqtt_client.loop_stop(force=False) # Allow time for LWT to publish if needed
            mqtt_client.disconnect()
        
        logging.info("Cleaning up serial port...")
        with serial_lock: # Ensure exclusive access for final commands
            if ser and ser.is_open:
                try:
                    # Attempt to send a final clear/bye message
                    send_to_badge(f"B{DEFAULT_BADGE_BRIGHTNESS:02}", max_retries=0)
                    send_to_badge("C", max_retries=0)
                    send_to_badge("SBye...", max_retries=0)
                    time.sleep(0.1) # Give badge time for last command
                    ser.close()
                except Exception as e:
                    logging.error(f"Error during final serial cleanup: {e}")
        logging.info("Badge bridge stopped.")
