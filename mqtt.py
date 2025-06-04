# badge_mqtt_bridge.py
import paho.mqtt.client as mqtt
import serial
import time
import json
import threading
import logging
import signal # For graceful shutdown

# --- CONFIGURATION ---
BADGE_SERIAL_PORT = "/dev/ttyACM0"      # <<<--- CHANGE THIS if your port is different
MQTT_BROKER_HOST = "rpi2"             # <<<--- CHANGE THIS to your MQTT broker IP/hostname
MQTT_BROKER_PORT = 1883
MQTT_USER = None                      # <<<--- SET YOUR MQTT USER if auth is needed, else None
MQTT_PASSWORD = None                  # <<<--- SET YOUR MQTT PASSWORD if auth is needed, else None
MQTT_KEEPALIVE_INTERVAL = 60          # Seconds for MQTT keepalive

# Device specific identifiers for MQTT Discovery
DEVICE_NAME_FRIENDLY = "DORS/CLUC Badge Notifier"
DEVICE_MANUFACTURER = "Hyperglitch Ltd / DORS/CLUC"
DEVICE_MODEL_NAME = "DC2025 Badge"
BRIDGE_SCRIPT_VERSION = "mqtt_bridge_v1.4"
DEVICE_UNIQUE_ID = "dc2025_badge_notifier_01" # MUST be unique for each badge/bridge instance

# MQTT Topic Structure
BASE_TOPIC_PREFIX = "badgenotifier" # Keeps topics tidy, e.g., badgenotifier/dc2025_badge_01/...
DEVICE_BASE_TOPIC = f"{BASE_TOPIC_PREFIX}/{DEVICE_UNIQUE_ID}"
AVAILABILITY_TOPIC = f"{DEVICE_BASE_TOPIC}/status"
DISCOVERY_PREFIX = "homeassistant"    # Standard HA discovery prefix

# Badge & Display Behavior
DEFAULT_BADGE_BRIGHTNESS = 20         # 0-99 (as per badge firmware Bxx command)
TEXT_MSG_MIN_DURATION_S = 3
TEXT_MSG_CHARS_PER_SEC = 5            # For extending display time based on text length
TIME_DISPLAY_UPDATE_INTERVAL_S = 1    # How often to update time on badge when idle
SERIAL_RECONNECT_DELAY_S = 5
MQTT_RECONNECT_DELAY_S = 10

# --- Logging Setup ---
logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                    datefmt='%Y-%m-%d %H:%M:%S')
logger = logging.getLogger("BadgeMQTTBridge")

# --- Global Variables & Threading Primitives ---
serial_port_instance = None
serial_port_lock = threading.Lock()
mqtt_client_instance = None
shutdown_event = threading.Event() # Used to signal all threads to terminate

# Display state management (protected by display_state_lock)
display_state_lock = threading.Lock()
is_displaying_custom_message = False
custom_message_expiry_time = 0 # time.monotonic() value
last_content_sent_to_badge = ""  # To avoid redundant 'S' commands
current_badge_brightness_sw_copy = DEFAULT_BADGE_BRIGHTNESS # Software's belief of badge brightness


# --- Serial Communication Functions ---
def connect_to_serial_port():
    global serial_port_instance
    if serial_port_instance and serial_port_instance.is_open:
        return True
    try:
        logger.info(f"Attempting to connect to badge via serial: {BADGE_SERIAL_PORT}")
        serial_port_instance = serial.Serial(BADGE_SERIAL_PORT, 115200, timeout=1, write_timeout=1)
        time.sleep(0.2) # Allow port to open and device to settle
        logger.info(f"Successfully connected to badge on {BADGE_SERIAL_PORT}")
        return True
    except serial.SerialException as e:
        logger.error(f"Serial connection error: {e}")
        serial_port_instance = None
        return False
    except Exception as e:
        logger.error(f"Unexpected error connecting to serial port: {e}")
        serial_port_instance = None
        return False

def send_command_to_badge(command_string, max_retries=1):
    global serial_port_instance
    if not command_string.endswith('\r\n'):
        command_string += '\r\n'

    with serial_port_lock:
        for attempt in range(max_retries + 1):
            if not serial_port_instance or not serial_port_instance.is_open:
                if not connect_to_serial_port():
                    if attempt < max_retries:
                        logger.warning(f"Badge not connected. Retrying send for '{command_string.strip()}'...")
                        time.sleep(SERIAL_RECONNECT_DELAY_S)
                        continue
                    else:
                        logger.error(f"Command '{command_string.strip()}' failed: Badge not connected after retries.")
                        return False
            try:
                logger.info(f"Sending to badge: {command_string.strip()}")
                serial_port_instance.write(command_string.encode('utf-8'))
                time.sleep(0.05) # Brief pause for badge to process
                return True
            except serial.SerialTimeoutException:
                logger.error(f"Serial write timeout for command: {command_string.strip()} (attempt {attempt + 1})")
            except Exception as e:
                logger.error(f"Error writing to badge for command {command_string.strip()} (attempt {attempt + 1}): {e}")
            
            # If error, close port to force reconnect on next attempt/call
            if serial_port_instance:
                try: serial_port_instance.close()
                except: pass
            serial_port_instance = None # Nullify to trigger reconnect
            if attempt < max_retries: time.sleep(0.5) # Wait before retry
            else: logger.error(f"Command '{command_string.strip()}' failed after retries due to write error.")
        return False

# --- Display Management Thread ---
def display_manager_thread():
    global is_displaying_custom_message, custom_message_expiry_time, last_content_sent_to_badge
    logger.info("Display manager thread started.")

    # Initial setup commands sent once after serial port is confirmed open by first send_command_to_badge call
    # This is now handled by set_initial_badge_and_mqtt_states called via on_connect

    while not shutdown_event.is_set():
        current_monotonic_time = time.monotonic()
        should_display_time = False

        with display_state_lock:
            if is_displaying_custom_message:
                if current_monotonic_time >= custom_message_expiry_time:
                    is_displaying_custom_message = False
                    should_display_time = True # Custom message duration ended
                    logger.info("Custom message duration expired. Switching to time display.")
            else: # Not showing custom message, so default to time
                should_display_time = True

        if should_display_time:
            # Badge uses HHMMSS format (6 characters)
            time_string_for_badge = time.strftime("%H%M%S")
            if time_string_for_badge != last_content_sent_to_badge or not is_displaying_custom_message:
                # Update if time changed or if we just switched from custom message
                logger.debug(f"Updating badge time display to: {time_string_for_badge}")
                if send_command_to_badge(f"S{time_string_for_badge}"):
                    last_content_sent_to_badge = time_string_for_badge
        
        # Calculate sleep time to align with the next second boundary
        # This aims for more accurate "every second" updates.
        iteration_processing_time = time.monotonic() - current_monotonic_time
        sleep_duration = max(0, TIME_DISPLAY_UPDATE_INTERVAL_S - iteration_processing_time)
        
        shutdown_event.wait(timeout=sleep_duration) # Sleep but be interruptible by shutdown_event

    logger.info("Display manager thread stopped.")


# --- MQTT Callbacks and Logic ---
def on_mqtt_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info("Successfully connected to MQTT Broker!")
        client.publish(AVAILABILITY_TOPIC, "online", retain=True, qos=1)
        publish_mqtt_discovery_messages(client)
        set_initial_badge_and_mqtt_states(client)

        # Subscribe to all relevant command topics
        client.subscribe(f"{DEVICE_BASE_TOPIC}/text_display/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/brightness/set")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/fade_button/press")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/clear_button/press")
        client.subscribe(f"{DEVICE_BASE_TOPIC}/logo_alert_light/set")
        logger.info("Subscribed to badge command topics.")
    else:
        logger.error(f"Failed to connect to MQTT, return code: {rc}")

def on_mqtt_disconnect(client, userdata, flags, reason_code, properties=None): # Paho MQTT v2 API signature
    # Note: LWT should publish "offline". This callback is for logging.
    if isinstance(reason_code, int): # Older style rc
        logger.warning(f"Disconnected from MQTT Broker with result code: {reason_code}")
    elif reason_code: # MQTTReasonCode object for v5
        logger.warning(f"Disconnected from MQTT Broker. Reason: {reason_code.getName()} ({reason_code.value})")
    else: # Client initiated disconnect (rc might be None or 0)
        logger.info("Disconnected from MQTT Broker (client initiated).")

def on_mqtt_message(client, userdata, msg):
    global is_displaying_custom_message, custom_message_expiry_time, last_content_sent_to_badge, current_badge_brightness_sw_copy
    topic = msg.topic
    payload_str = msg.payload.decode('utf-8', errors='replace').strip()
    logger.info(f"MQTT Message Received: Topic='{topic}', Payload='{payload_str}'")

    entity_topic_suffix = topic.replace(f"{DEVICE_BASE_TOPIC}/", "")

    if entity_topic_suffix == "text_display/set":
        with display_state_lock:
            text_to_show = payload_str[:30] # Badge 'S' command handles up to 30 chars for scrolling
            if send_command_to_badge(f"S{text_to_show}"):
                last_content_sent_to_badge = text_to_show
                is_displaying_custom_message = True
                text_len_for_duration = len(text_to_show) if text_to_show else 6 # Assume time length if empty
                duration = TEXT_MSG_MIN_DURATION_S + (text_len_for_duration / TEXT_MSG_CHARS_PER_SEC)
                custom_message_expiry_time = time.monotonic() + duration
                logger.info(f"Displaying custom text '{text_to_show}' for ~{duration:.1f} seconds.")
                client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", text_to_show, retain=True, qos=1)

    elif entity_topic_suffix == "brightness/set":
        try:
            brightness_value = int(payload_str)
            if 0 <= brightness_value <= 99: # Badge command Bxx takes 00-99
                if send_command_to_badge(f"B{brightness_value:02}"):
                    current_badge_brightness_sw_copy = brightness_value
                    client.publish(f"{DEVICE_BASE_TOPIC}/brightness/state", str(brightness_value), retain=True, qos=1)
            else:
                logger.warning(f"Received brightness value out of range (0-99): {brightness_value}")
        except ValueError:
            logger.warning(f"Received invalid brightness value: {payload_str}")

    elif entity_topic_suffix == "fade_button/press" and payload_str.upper() == "PRESS":
        send_command_to_badge("F")

    elif entity_topic_suffix == "clear_button/press" and payload_str.upper() == "PRESS":
        if send_command_to_badge("C"): # 'C' command clears screen and logo LEDs on badge
            with display_state_lock:
                is_displaying_custom_message = False
                custom_message_expiry_time = 0
                last_content_sent_to_badge = "" # Force time update by display_manager
            client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
            client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1) # Reflect in HA

    elif entity_topic_suffix == "logo_alert_light/set":
        all_leds_action_success = True
        ha_light_state_to_publish = "OFF"
        if payload_str.upper() == "ON":
            ha_light_state_to_publish = "ON"
            for i in range(39): # Turn all 39 logo LEDs ON
                if not send_command_to_badge(f"L{i:02}1"): all_leds_action_success = False; break
        elif payload_str.upper() == "OFF":
            for i in range(39): # Turn all 39 logo LEDs OFF
                if not send_command_to_badge(f"L{i:02}0"): all_leds_action_success = False; break
        
        if all_leds_action_success:
            client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", ha_light_state_to_publish, retain=True, qos=1)

def publish_mqtt_discovery_messages(client):
    logger.info("Publishing MQTT discovery configuration messages...")
    device_info_payload = {
        "identifiers": [DEVICE_UNIQUE_ID], "name": DEVICE_NAME_FRIENDLY,
        "manufacturer": DEVICE_MANUFACTURER, "model": DEVICE_MODEL_NAME,
        "sw_version": BRIDGE_SCRIPT_VERSION,
        "configuration_url": "https://www.dorscluc.org/badge-2025/" # Example URL
    }
    common_entity_params = {
        "device": device_info_payload, "optimistic": False, "retain": True, "qos": 1,
        "availability_topic": AVAILABILITY_TOPIC,
        "payload_available": "online", "payload_not_available": "offline"
    }

    # Text Entity
    text_entity_id = "badge_display_text"
    text_config_topic = f"{DISCOVERY_PREFIX}/text/{DEVICE_UNIQUE_ID}/{text_entity_id}/config"
    text_payload = {**common_entity_params, "name": "Badge Text", "unique_id": f"{DEVICE_UNIQUE_ID}_{text_entity_id}",
                    "state_topic": f"{DEVICE_BASE_TOPIC}/text_display/state",
                    "command_topic": f"{DEVICE_BASE_TOPIC}/text_display/set",
                    "min": 0, "max": 30, "pattern": "^[ -~]*$"}
    client.publish(text_config_topic, json.dumps(text_payload), retain=True, qos=1)

    # Brightness Number Entity
    brightness_entity_id = "badge_brightness"
    bright_config_topic = f"{DISCOVERY_PREFIX}/number/{DEVICE_UNIQUE_ID}/{brightness_entity_id}/config"
    bright_payload = {**common_entity_params, "name": "Badge Brightness", "unique_id": f"{DEVICE_UNIQUE_ID}_{brightness_entity_id}",
                      "state_topic": f"{DEVICE_BASE_TOPIC}/brightness/state",
                      "command_topic": f"{DEVICE_BASE_TOPIC}/brightness/set",
                      "min": 0, "max": 99, "step": 1, "unit_of_measurement": "%", "mode": "slider"}
    client.publish(bright_config_topic, json.dumps(bright_payload), retain=True, qos=1)

    # Button common params (no state, qos 0 for commands)
    button_common_params = {"device": device_info_payload, "retain": False, "qos": 0,
                            "availability_topic": AVAILABILITY_TOPIC,
                            "payload_available": "online", "payload_not_available": "offline"}
    # Fade Button
    fade_btn_entity_id = "badge_trigger_fade"
    fade_btn_cfg_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/{fade_btn_entity_id}/config"
    fade_btn_payload = {**button_common_params, "name": "Badge Fade", "unique_id": f"{DEVICE_UNIQUE_ID}_{fade_btn_entity_id}",
                        "command_topic": f"{DEVICE_BASE_TOPIC}/fade_button/press"}
    client.publish(fade_btn_cfg_topic, json.dumps(fade_btn_payload), retain=True, qos=1) # Config is retained

    # Clear Button
    clear_btn_entity_id = "badge_clear_display"
    clear_btn_cfg_topic = f"{DISCOVERY_PREFIX}/button/{DEVICE_UNIQUE_ID}/{clear_btn_entity_id}/config"
    clear_btn_payload = {**button_common_params, "name": "Badge Clear", "unique_id": f"{DEVICE_UNIQUE_ID}_{clear_btn_entity_id}",
                         "command_topic": f"{DEVICE_BASE_TOPIC}/clear_button/press"}
    client.publish(clear_btn_cfg_topic, json.dumps(clear_btn_payload), retain=True, qos=1)

    # Logo Alert Light
    logo_light_entity_id = "badge_logo_alert"
    logo_light_cfg_topic = f"{DISCOVERY_PREFIX}/light/{DEVICE_UNIQUE_ID}/{logo_light_entity_id}/config"
    logo_light_payload = {**common_entity_params, "name": "Badge Logo Alert", "unique_id": f"{DEVICE_UNIQUE_ID}_{logo_light_entity_id}",
                          "schema": "basic", # For simple on/off
                          "state_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/state",
                          "command_topic": f"{DEVICE_BASE_TOPIC}/logo_alert_light/set",
                          "payload_on": "ON", "payload_off": "OFF"}
    client.publish(logo_light_cfg_topic, json.dumps(logo_light_payload), retain=True, qos=1)

    logger.info("MQTT Discovery messages published.")

def set_initial_badge_and_mqtt_states(client):
    global current_badge_brightness_sw_copy, last_content_sent_to_badge
    logger.info("Setting initial badge hardware state and publishing initial MQTT states...")
    
    current_badge_brightness_sw_copy = DEFAULT_BADGE_BRIGHTNESS
    if send_command_to_badge(f"B{current_badge_brightness_sw_copy:02}"):
        client.publish(f"{DEVICE_BASE_TOPIC}/brightness/state", str(current_badge_brightness_sw_copy), retain=True, qos=1)
    
    if send_command_to_badge("C"): # Clear physical badge display
        last_content_sent_to_badge = "" # So display_manager will show time initially
        client.publish(f"{DEVICE_BASE_TOPIC}/text_display/state", "", retain=True, qos=1)
    
    all_logo_off_ok = True
    for i in range(39):
        if not send_command_to_badge(f"L{i:02}0"): all_logo_off_ok = False; break
    if all_logo_off_ok:
        client.publish(f"{DEVICE_BASE_TOPIC}/logo_alert_light/state", "OFF", retain=True, qos=1)

# --- Main Application Logic & Signal Handling ---
display_manager_thread_instance = None # To join it on shutdown

def signal_handler(sig, frame):
    logger.info(f"Signal {sig} received. Initiating graceful shutdown.")
    shutdown_event.set() # Signal all threads to stop

def main_application_loop():
    global mqtt_client_instance, display_manager_thread_instance

    # Start the display manager thread
    display_manager_thread_instance = threading.Thread(target=display_manager_thread, daemon=False) # Non-daemon
    display_manager_thread_instance.start()

    mqtt_client_instance = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"badge_bridge_{DEVICE_UNIQUE_ID}")
    if MQTT_USER and MQTT_PASSWORD:
        mqtt_client_instance.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
    mqtt_client_instance.will_set(AVAILABILITY_TOPIC, payload="offline", qos=1, retain=True) # LWT
    
    mqtt_client_instance.on_connect = on_mqtt_connect
    mqtt_client_instance.on_disconnect = on_mqtt_disconnect
    mqtt_client_instance.on_message = on_mqtt_message

    while not shutdown_event.is_set():
        try:
            if not mqtt_client_instance.is_connected():
                logger.info(f"Attempting to connect to MQTT broker: {MQTT_BROKER_HOST}...")
                mqtt_client_instance.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_KEEPALIVE_INTERVAL)
                mqtt_client_instance.loop_start() # Handles reconnections automatically in background thread
            
            # Main thread can do other things or just wait for shutdown signal
            shutdown_event.wait(timeout=5.0) # Check event every 5s or when set

        except ConnectionRefusedError:
            logger.error(f"MQTT connection refused by broker {MQTT_BROKER_HOST}. Retrying in {MQTT_RECONNECT_DELAY_S}s...")
            if mqtt_client_instance.is_connected(): mqtt_client_instance.loop_stop() # Stop if connect failed after loop_start
            shutdown_event.wait(MQTT_RECONNECT_DELAY_S)
        except Exception as e:
            logger.error(f"Unhandled MQTT or main loop error: {e}. Retrying in {MQTT_RECONNECT_DELAY_S}s...")
            if mqtt_client_instance and mqtt_client_instance.is_connected(): mqtt_client_instance.loop_stop()
            shutdown_event.wait(MQTT_RECONNECT_DELAY_S)
    
    logger.info("Main application loop terminated due to shutdown signal.")

if __name__ == "__main__":
    # Setup signal handlers for graceful shutdown on Ctrl+C (SIGINT) and kill (SIGTERM)
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        main_application_loop()
    except Exception as e: # Catch-all for unexpected errors in main_application_loop itself
        logger.error(f"Critical error in main_application_loop: {e}", exc_info=True)
    finally:
        logger.info("Shutdown sequence initiated in finally block...")
        # Ensure event is set (might be already by signal handler)
        shutdown_event.set() 

        # 1. Join the display manager thread
        if display_manager_thread_instance and display_manager_thread_instance.is_alive():
            logger.info("Waiting for display manager thread to join...")
            display_manager_thread_instance.join(timeout=1.5) # Reduced timeout slightly
            if display_manager_thread_instance.is_alive():
                logger.warning("Display manager thread did not join cleanly.")
            else:
                logger.info("Display manager thread joined.")
        
        # 2. MQTT Cleanup
        logger.info("Cleaning up MQTT connection...")
        if mqtt_client_instance:
            if mqtt_client_instance.is_connected():
                try:
                    mqtt_client_instance.publish(AVAILABILITY_TOPIC, "offline", retain=True, qos=1)
                    logger.info("Published 'offline' availability status.")
                except Exception as e_pub: 
                    logger.error(f"Error publishing 'offline': {e_pub}")
            try:
                mqtt_client_instance.loop_stop() # Stop the network loop
                logger.info("MQTT network loop stopped.")
            except Exception as e_loop: 
                logger.error(f"Error stopping MQTT loop: {e_loop}")
            try:
                if mqtt_client_instance.is_connected(): # Check again before disconnect
                    mqtt_client_instance.disconnect()
                    logger.info("Disconnected from MQTT broker.")
            except Exception as e_disc: 
                logger.error(f"Error disconnecting from MQTT: {e_disc}")
            else: # Not connected, but ensure loop is stopped if it was started
                try: mqtt_client_instance.loop_stop()
                except: pass # Ignore if loop_stop fails on non-started/already-stopped loop
        
        # 3. Serial Cleanup
        logger.info("Attempting final serial cleanup...")
        with serial_port_lock:
            if serial_port_instance and serial_port_instance.is_open:
                try:
                    # Send final commands with max_retries=0 to avoid hanging
                    send_command_to_badge(f"B{DEFAULT_BADGE_BRIGHTNESS:02}", max_retries=0)
                    send_command_to_badge("C", max_retries=0)
                    send_command_to_badge("SBye! :)", max_retries=0)
                    time.sleep(0.2) # Allow final commands to be sent
                    serial_port_instance.close()
                    logger.info("Serial port closed.")
                except Exception as e_serial:
                    logger.error(f"Error during final serial port cleanup: {e_serial}")
        logger.info("Badge MQTT bridge stopped.")