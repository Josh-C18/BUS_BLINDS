"""
Display a text string (e.g. a bus end stop / route name) on the ESP32 matrix
panel WITHOUT re-flashing it.

The ESP32 runs as a WiFi access point ("iRouter") with a TCP server on port
7654. This script connects and sends a text string, which the ESP32 renders on
the 80x40 panel instantly.

Usage:
    python3 display.py              # interactive: prompts for a string
    python3 display.py "END STOP"   # show that string then exit
    python3 display.py --ip 192.168.4.1 "ROUTE 5"
    python3 display.py --loop 99 "ROUTE 5"   # repeat every second
    python3 display.py --clr                          # clear the display
"""

import socket
import argparse
import sys
import time

HOST = "192.168.8.165"
PORT = 7654

# Maximum characters that fit reasonably on an 80x40 panel.
MAX_LEN = 16


def send_text(text: str, host: str = HOST, port: int = PORT) -> bool:
    if not text or len(text) > MAX_LEN:
        print(
            f"  error: text must be 1-{MAX_LEN} characters (got {len(text)})",
            file=sys.stderr,
        )
        return False

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    try:
        s.connect((host, port))
    except OSError as e:
        errno = getattr(e, "errno", None)
        code = getattr(e, "code", None)
        msg = f"  error: could not connect to ESP32 at {host}:{port}: {e.strerror}"
        if errno == 61 or code == "ECONNREFUSED":
            print(msg + "\n  Nothing is listening on port 7654. The ESP32 may have failed to start its server. Check the serial monitor for a '[wifi] TCP server listening' line.", file=sys.stderr)
        elif errno == 60 or errno == 113 or code in ("ETIMEDOUT", "ENETUNREACH", "EHOSTUNREACH"):
            print(msg + "\n  This usually means the router is blocking devices from talking to each other (AP isolation / client isolation), or the ESP32 isn't actually on the same network. Try pinging it: ping 192.168.8.165", file=sys.stderr)
        else:
            print(msg, file=sys.stderr)
        return False

    try:
        s.sendall(f"{text}\n".encode("ascii"))
    finally:
        s.close()

    print(f"  sent text '{text}' to ESP32 ({host}:{port})")
    return True


def send_clr(host: str, port: int) -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    try:
        s.connect((host, port))
    except OSError as e:
        errno = getattr(e, "errno", None)
        code = getattr(e, "code", None)
        msg = f"  error: could not connect to ESP32 at {host}:{port}: {e.strerror}"
        if errno == 61 or code == "ECONNREFUSED":
            print(msg + "\n  Nothing is listening on port 7654. The ESP32 may have failed to start its server. Check the serial monitor for a '[wifi] TCP server listening' line.", file=sys.stderr)
        elif errno == 60 or errno == 113 or code in ("ETIMEDOUT", "ENETUNREACH", "EHOSTUNREACH"):
            print(msg + "\n  This usually means the router is blocking devices from talking to each other (AP isolation / client isolation), or the ESP32 isn't actually on the same network. Try pinging it: ping 192.168.8.165", file=sys.stderr)
        else:
            print(msg, file=sys.stderr)
        return False

    try:
        s.sendall(b"CLR\n")
    finally:
        s.close()

    print(f"  sent CLR to ESP32 ({host}:{port}) — cleared the display")
    return True


def prompt_text(host: str, port: int) -> bool:
    while True:
        raw = input("Enter text (1-16 chars), 'clr' to clear, or 'q' to quit: ").strip()
        if raw.lower() in ("q", "quit", "exit"):
            print("Bye.")
            return True
        if raw.lower() in ("clr", "clear"):
            send_clr(host, port)
            continue
        if len(raw) > MAX_LEN:
            print(f"  please enter 1-{MAX_LEN} characters, 'clr' to clear, or 'q' to quit.")
            continue
        if not send_text(raw, host, port):
            continue
        break
    return True


def interactive(host: str, port: int) -> None:
    print(f"Connecting to ESP32 at {host}:{port} ...")
    if not prompt_text(host, port):
        return
    while prompt_text(host, port):
        pass


def loop(host: str, port: int, text: str, interval: float) -> None:
    print(f"Sending '{text}' every {interval}s (Ctrl+C to stop) ...")
    try:
        while True:
            send_text(text, host, port)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Display a bus end stop on the ESP32 panel.")
    parser.add_argument("text", nargs="?", help="Text to display (1-16 chars)")
    parser.add_argument("--ip", default=HOST, help=f"ESP32 IP address (default: {HOST})")
    parser.add_argument("--port", type=int, default=PORT, help=f"ESP32 TCP port (default: {PORT})")
    parser.add_argument(
        "--loop",
        type=float,
        metavar="SECONDS",
        help="Send the text repeatedly every SECONDS (needs a text).",
    )
    parser.add_argument(
        "--clr",
        action="store_true",
        help="Send the CLR command to clear the display.",
    )
    args = parser.parse_args()

    if args.clr:
        if args.text is not None or args.loop:
            print("error: --clr cannot be combined with text or --loop", file=sys.stderr)
            sys.exit(1)
        if not send_clr(args.ip, args.port):
            sys.exit(1)
    elif args.loop:
        if args.text is None:
            print("error: --loop requires text to send", file=sys.stderr)
            sys.exit(1)
        loop(args.ip, args.port, args.text, args.loop)
    elif args.text is None:
        interactive(args.ip, args.port)
    else:
        send_text(args.text, args.ip, args.port)


if __name__ == "__main__":
    import time
    main()
