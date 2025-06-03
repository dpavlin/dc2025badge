# badge_mqtt_bridge.py
import paho.mqtt.client as mqtt
import serial
import time
import json
import threading # For keepalive/re-announcement

# --- Configuration ---
BADGE_SERIAL_PORT = "/dev/ttyACM0"  # Change if needed
MQTT_BROKER = "rpi2"
MQTT_PORT = 1883
MQTT_USER = "your_mqtt_user"      # Optional
MQTT_PASSWORD = "your_mqtt_password"  # Optional

DEVICE_NAME = "DORS/CLUC Badge"
DEVICE_MANUFACTURER = "Hyperglitch Ltd / DORS/CLUC"
DEVICE_MODEL = "DC2025 Badge"
DEVICE_SW_VERSION = "mqtt_bridge_v1.0" # Bridge script version
# You can get the badge's actual firmware version if it's accessible somehow,
# or use the __GIT_VERSION from instance_id.h if the bridge could read it
# For now, this is the bridge's version.

# Unique ID for the device (important for HA) - could be derived from badge serial if available
# or just a fixed string for a single badge setup. For multiple badges, this needs to be unique per badge.
DEVICE_UNIQUE_ID = "dc2025_badge_notifier_01"
BASE_TOPIC = f"homeassistant/device/{DEVICE_UNIQUE_ID}" # Custom base for our topics

# MQTT Discovery Prefix (usually "homeassistant")
DISCOVERY_PREFIX = "homeassistant"

# Global serial object and lock for thread safety
ser = None
serial_lock = threading.Lock()
mqtt_client = None

# --- Helper to Send Serial Commands Safely ---
def send_to_badge(command_str):
    global ser
    with serial_lock:
        if ser is None or not ser.is_open:
            try:
                ser = serial.Serial(BADGE_SERIAL_PORT, 115200, timeout=1)
                time.sleep(0.2)  # Allow port to open
                print(f"Successfully (re)connected to badge on {BADGE_SERIAL_PORT}")
            except Exception as e:
                print(f"Error connecting to badge: {e}")
                ser = None
                return False
        try:
            print(f"Sending to badge: {command_str.strip()}")
            ser.write(command_str.encode())
            time.sleep(0.05) # Small delay for command processing by badge
            return True
        except Exception as e:
            print(f"Error writing to badge: {e}")
            if ser:
                ser.close()
            ser = None
            return False

# --- MQTT Callback Functions ---
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("Connected to MQTT Broker!")
        publish_discovery_messages(client) # Announce device on connect/reconnect

        # Subscribe to command topics
        client.subscribe(f"{BASE_TOPIC}/text_display/set")
        client.subscribe(f"{BASE_TOPIC}/brightness/set")
        client.subscribe(f"{BASE_TOPIC}/fade_button/press") # HA button "press" topic
        client.subscribe(f"{BASE_TOPIC}/clear_button/press")
        client.subscribe(f"{BASE_TOPIC}/logo_alert_light/set")
        print("Subscribed to command topics.")
    else:
        print(f"Failed to connect to MQTT, return code {rc}\n")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload_str = msg.payload.decode()
    print(f"Received MQTT: Topic='{topic}', Payload='{payload_str}'")

    # Remove base topic part to get entity specific topic
    entity_topic = topic.replace(f"{BASE_TOPIC}/", "")

    if entity_topic == "text_display/set":
        send_to_badge(f"S{payload_str[:30]}\r\n") # Limit text length
        # Optionally publish back the state if text entity supports it
        client.publish(f"{BASE_TOPIC}/text_display/state", payload_str[:30], retain=True)

    elif entity_topic == "brightness/set":
        try:
            brightness_val = int(payload_str)
            if 0 <= brightness_val <= 99: # Badge takes 00-99
                send_to_badge(f"B{brightness_val:02}\r\n")
                client.publish(f"{BASE_TOPIC}/brightness/state", str(brightness_val), retain=True)
            else:
                print(f"Brightness value out of range (0-99): {brightness_val}")
        except ValueError:
            print(f"Invalid brightness value: {payload_str}")

    elif entity_topic == "fade_button/press": # Button commands are just "PRESS"
        send_to_badge("F\r\n")

    elif entity_topic == "clear_button/press":
        send_to_badge("C\r\n")
        # Clear the text state in HA too
        client.publish(f"{BASE_TOPIC}/text_display/state", "", retain=True)


    elif entity_topic == "logo_alert_light/set":
        # Example: Payload is JSON: {"state": "ON", "brightness": 50}
        # Or simpler: just "ON" or "OFF"
        if payload_str.upper() == "ON":
            # Turn all logo LEDs on (example pattern)
            for i in range(39):
                send_to_badge(f"L{i:02}1\r\n")
            client.publish(f"{BASE_TOPIC}/logo_alert_light/state", "ON", retain=True)
        elif payload_str.upper() == "OFF":
            # Turn all logo LEDs off
            for i in range(39):
                send_to_badge(f"L{i:02}0\r\n")
            client.publish(f"{BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True)
        # Could also handle brightness from payload for this light if desired.

# --- MQTT Discovery Message Publishing ---
def publish_discovery_messages(client):
    print("Publishing MQTT discovery messages...")

    # Device Information (common to all entities)
    device_info = {
        "identifiers": [DEVICE_UNIQUE_ID],
        "name": DEVICE_NAME,
        "manufacturer": DEVICE_MANUFACTURER,
        "model": DEVICE_MODEL,
        "sw_version": DEVICE_SW_VERSION
    }

    # 1. Text Entity for Display
    text_config_topic = f"{DISCOVERY_PREFIX}/text/{DEVICE_UNIQUE_ID}/badge_display_text/config"
    text_payload = {
        "name": "Badge Display Text",
        "unique_id": f"{DEVICE_UNIQUE_ID}_display_text",
        "state_topic": f"{BASE_TOPIC}/text_display/state", # To read back current text
        "command_topic": f"{BASE_TOPIC}/text_display/set",
        "min": 1, # Min text length
        "max": 30, # Max text length for S command (badge handles scrolling longer)
        "pattern": "^[ -~]*$", # Allow printable ASCII
        "device": device_info,
        "optimistic": False, # Set to true if no state topic
        "retain": True
    }
    client.publish(text_config_topic, json.dumps(text_payload), retain=True)

    # 2. Number Entity for Brightness
    brightness_config_topic = f"{DISCOVERY_PREFIX}/number/{DEVICE_UNIQUE_ID}/badge_brightness/config"
    brightness_payload = {
        "name": "Badge Brightness",
        "unique_id": f"{DEVICE_UNIQUE_ID}_brightness",
        "state_topic": f"{BASE_TOPIC}/brightness/state",
        "command_topic": f"{BASE_TOPIC}/brightness/set",
        "min": 0,
        "max": 99, # Badge takes 00-99
        "step": 1,
        "unit_of_measurement": "%",
        "mode": "slider", # or "box"
        "device": device_info,
        "optimistic": False,
        "retain": True
    }
    client.publish(brightness_config_topic, json.dumps(brightness_payload), retain=True)

    # 3. Button Entity for Fade
    fade_button_config_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/badge_trigger_fade/config"
    fade_button_payload = {
        "name": "Badge Trigger Fade",
        "unique_id": f"{DEVICE_UNIQUE_ID}_trigger_fade",
        "command_topic": f"{BASE_TOPIC}/fade_button/press", # HA sends "PRESS" here
        "device": device_info,
        "retain": False # Buttons are stateless commands
    }
    client.publish(fade_button_config_topic, json.dumps(fade_button_payload), retain=True)

    # 4. Button Entity for Clear
    clear_button_config_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/badge_clear_display/config"
    clear_button_payload = {
        "name": "Badge Clear Display",
        "unique_id": f"{DEVICE_UNIQUE_ID}_clear_display",
        "command_topic": f"{BASE_TOPIC}/clear_button/press",
        "device": device_info,
        "retain": False
    }
    client.publish(clear_button_config_topic, json.dumps(clear_button_payload), retain=True)

    # 5. Light Entity for Logo Alert (simple on/off all LEDs)
    logo_light_config_topic = f"{DISCOVERY_PREFIX}/light/{DEVICE_UNIQUE_ID}/badge_logo_alert/config"
    logo_light_payload = {
        "name": "Badge Logo Alert Light",
        "unique_id": f"{DEVICE_UNIQUE_ID}_logo_alert",
        "state_topic": f"{BASE_TOPIC}/logo_alert_light/state",
        "command_topic": f"{BASE_TOPIC}/logo_alert_light/set",
        # "brightness_state_topic": f"{BASE_TOPIC}/brightness/state", # Can share brightness
        # "brightness_command_topic": f"{BASE_TOPIC}/brightness/set",
        # "brightness_scale": 99, # If using shared brightness
        "payload_on": "ON",
        "payload_off": "OFF",
        "optimistic": False,
        "device": device_info,
        "retain": True
    }
    client.publish(logo_light_config_topic, json.dumps(logo_light_payload), retain=True)

    print("Discovery messages published.")

# --- Main Script Logic ---
def keep_alive_discovery(client_ref):
    """Periodically re-publish discovery messages and current states."""
    while True:
        time.sleep(300) # e.g., every 5 minutes
        if client_ref and client_ref.is_connected():
            print("Re-publishing discovery and states for keep-alive...")
            publish_discovery_messages(client_ref)
            # You might also want to re-publish current states if they are not retained
            # or if you want to ensure HA has the latest.
            # For example, if brightness was changed by another means.
        else:
            print("Keep-alive: MQTT client not connected.")


if __name__ == "__main__":
    # Initial connection to badge
    if not send_to_badge("C\r\n"): # Try initial clear to test connection
        print(f"Could not connect to badge on {BADGE_SERIAL_PORT}. Please check connection and permissions.")
        # exit(1) # Optionally exit if badge not found on startup, or keep retrying.

    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"badge_bridge_{DEVICE_UNIQUE_ID}")
    if MQTT_USER and MQTT_PASSWORD:
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    # Start keep-alive thread for discovery (daemon so it exits with main)
    # Pass the client instance by reference if using a class, or ensure it's accessible
    discovery_thread = threading.Thread(target=keep_alive_discovery, args=(mqtt_client,), daemon=True)
    discovery_thread.start()

    while True: # Main loop for MQTT connection retries
        try:
            print(f"Attempting to connect to MQTT broker: {MQTT_BROKER}")
            mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
            mqtt_client.loop_forever() # Blocks until disconnect
        except KeyboardInterrupt:
            print("Exiting badge bridge.")
            break
        except ConnectionRefusedError:
            print("MQTT connection refused. Retrying in 10 seconds...")
        except Exception as e:
            print(f"MQTT Error: {e}. Retrying in 10 seconds...")
        
        # If loop_forever() exited due to disconnect or error
        if mqtt_client.is_connected():
            mqtt_client.disconnect()
        time.sleep(10) # Wait before retrying connection

    if ser and ser.is_open:
        send_to_badge("C\r\nSBye!\r\n") # Clear and say bye on exit
        ser.close()
    print("Badge bridge stopped.")
