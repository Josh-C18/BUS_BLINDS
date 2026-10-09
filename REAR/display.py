"""
Display a bus number on the ESP32 matrix panel WITHOUT re-flashing it.

The ESP32 runs as a WiFi access point ("BUS_DISPLAY") with a TCP server on
port 7654. This script connects to it and sends a number (1-999), which the
ESP32 renders on the panel instantly.

Usage:
    python3 display.py              # interactive: prompts for a number
    python3 display.py 42           # show bus number 42 then exit
    python3 display.py --ip 192.168.4.1 99
    python3 display.py --loop 99    # send 99 repeatedly, every second
"""

import socket
import argparse
import sys
import time

HOST = "192.168.8.165"
PORT = 7654


def send_number(number: int, host: str = HOST, port: int = PORT) -> bool:
    if not (1 <= number <= 999):
        print(f"  error: number must be between 1 and 999 (got {number})", file=sys.stderr)
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
        s.sendall(f"{number}\n".encode("ascii"))
    finally:
        s.close()

    print(f"  sent bus number {number} to ESP32 ({host}:{port})")
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

    print(f"  sent CLR to ESP32 ({host}:{port}) — cleared stored number")
    return True


def prompt_number(host: str, port: int) -> bool:
    while True:
        raw = input("Enter bus number (1-999), 'clr' to clear, or 'q' to quit: ").strip()
        if raw.lower() in ("q", "quit", "exit"):
            print("Bye.")
            return True
        if raw.lower() in ("clr", "clear"):
            send_clr(host, port)
            continue
        try:
            number = int(raw)
        except ValueError:
            print("  please enter a whole number, 'clr' to clear, or 'q' to quit.")
            continue
        if not send_number(number, host, port):
            continue
        break
    return True


def interactive(host: str, port: int) -> None:
    print(f"Connecting to ESP32 at {host}:{port} ...")
    if not prompt_number(host, port):
        return
    while prompt_number(host, port):
        pass


def loop(host: str, port: int, number: int, interval: float) -> None:
    print(f"Sending {number} every {interval}s (Ctrl+C to stop) ...")
    try:
        while True:
            send_number(number, host, port)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Display a bus number on the ESP32 panel.")
    parser.add_argument("number", type=int, nargs="?", help="Bus number to display (1-999)")
    parser.add_argument("--ip", default=HOST, help=f"ESP32 IP address (default: {HOST})")
    parser.add_argument("--port", type=int, default=PORT, help=f"ESP32 TCP port (default: {PORT})")
    parser.add_argument(
        "--loop",
        type=float,
        metavar="SECONDS",
        help="Send the number repeatedly every SECONDS (needs a --number).",
    )
    parser.add_argument(
        "--clr",
        action="store_true",
        help="Send the CLR command to clear the stored number.",
    )
    args = parser.parse_args()

    if args.clr:
        if args.number is not None or args.loop:
            print("error: --clr cannot be combined with a number or --loop", file=sys.stderr)
            sys.exit(1)
        if not send_clr(args.ip, args.port):
            sys.exit(1)
    elif args.loop:
        if args.number is None:
            print("error: --loop requires a number to send", file=sys.stderr)
            sys.exit(1)
        loop(args.ip, args.port, args.number, args.loop)
    elif args.number is None:
        interactive(args.ip, args.port)
    else:
        send_number(args.number, args.ip, args.port)


if __name__ == "__main__":
    main()
