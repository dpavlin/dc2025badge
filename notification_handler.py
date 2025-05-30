import argparse
import queue
import subprocess
import sys
import serial
import threading
import time


def badge_thread(q, time_to_die, port):
    # TODO: read badge output and react to buttons and nfc tags
    while True:
        if time_to_die.is_set():
            break
        try:
            data = q.get(timeout=1)
            print(f"\n[Notification]\nApp: {data['app_name']}\nTitle: {data['summary']}\nBody: {data['body']}\n")

            s = serial.Serial(port, 115200)
            s.write(b'C\r\n')    # Clear screen
            for i in range(39):
                s.write(b'L%02d1\r\n' % i)    # light up leds
                time.sleep(0.02)
            s.write(b'S%s\r\n' % data['body'].encode())    # Scroll text
            time.sleep(5)
            for i in range(39):
                s.write(b'L%02d0\r\n' % i)    # light up leds
                time.sleep(0.02)
            s.write(b'C\r\n')    # Clear screen
        except queue.Empty:
            # display time
            s = serial.Serial(port, 115200)
            s.write(b'C\r\n')    # Clear screen
            s.write(time.strftime("S %02H%02M \r\n").encode())    # Show time


def main():
    parser = argparse.ArgumentParser(description="DCBadge2025 Notification handler")
    parser.add_argument("port", help="Serial port to use")
    args = parser.parse_args()

    # Start dbus-monitor
    monitor = subprocess.Popen(
        ["dbus-monitor", "interface='org.freedesktop.Notifications'"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        universal_newlines=True,
        bufsize=1,
    )

    if not monitor.stdout:
        print("Error: dbus-monitor failed to start")
        sys.exit(1)

    dataq = queue.Queue()
    time_to_die = threading.Event()
    bthread = threading.Thread(target=badge_thread, args=(dataq, time_to_die, args.port))

    bthread.start()

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
                    dataq.put({"app_name": app_name, "summary": summary, "body": body})
                    inside_notify = False  # Reset for next notification

    except KeyboardInterrupt:
        print("Exiting.")
    finally:
        monitor.terminate()
    time_to_die.set()
    bthread.join()
    print("bye")


if __name__ == "__main__":
    main()

