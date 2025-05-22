import subprocess
import re
import sys
import serial
import time

# Replace this with your desired logic
def process_notification(app_name, summary, body):
    print(f"\n[Notification]\nApp: {app_name}\nTitle: {summary}\nBody: {body}\n")
    s = serial.Serial(sys.argv[1], 115200)
    s.write(b'C\r\n')    # Clear screen
    for i in range(39):
        s.write(b'L%02d1\r\n' % i)    # light up leds
        time.sleep(0.02)
    s.write(b'S%s\r\n' % body.encode())    # Scroll text
    time.sleep(5)
    for i in range(39):
        s.write(b'L%02d0\r\n' % i)    # light up leds
        time.sleep(0.02)
    s.write(b'C\r\n')    # Clear screen


def main():
    # Start dbus-monitor
    monitor = subprocess.Popen(
        ["dbus-monitor", "interface='org.freedesktop.Notifications'"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        universal_newlines=True,
        bufsize=1,
    )

    notification_data = []
    inside_notify = False

    try:
        for line in monitor.stdout:
            line = line.strip()

            # Detect the start of a Notify method call
            if line.startswith("method call") and "Notify" in line:
                notification_data = []
                inside_notify = True
                continue

            if inside_notify:
                if line.startswith("string "):
                    value = line.split("string", 1)[1].strip().strip('"')
                    notification_data.append(value)

                # Once we have enough fields (5 strings: app_name, app_icon, summary, body, etc.)
                if len(notification_data) >= 4:
                    app_name = notification_data[0]
                    summary = notification_data[2]
                    body = notification_data[3]
                    process_notification(app_name, summary, body)
                    inside_notify = False  # Reset for next notification

    except KeyboardInterrupt:
        print("Exiting.")
    finally:
        monitor.terminate()

if __name__ == "__main__":
    main()

