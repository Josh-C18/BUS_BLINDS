"""
Display a bus number and final stop on the OUTSIDE_FRONT 160x40 ESP32 panel
WITHOUT re-flashing it.

The ESP32 runs as a WiFi access point ("iRouter") with a TCP server on port
7654. This script connects and sends a number and/or a stop name; the ESP32
shows the number for 5s then the final stop for 5s in a repeating cycle.

Usage:
    python3 display.py                       # interactive
    python3 display.py 23                    # show bus number 23 then exit
    python3 display.py --stop "CITY CENTRE"  # show final stop then exit
    python3 display.py 23 --stop "CITY CENTRE"  # set both then exit
    python3 display.py --ip 192.168.4.1 23
    python3 display.py --loop 3 23           # re-send every 3 seconds (Ctrl+C)
    python3 display.py --clr                 # clear the display
"""

import socket
import argparse
import sys
import time

HOST = "192.168.8.165"
PORT = 7654


def _connect(host, port):
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
        return None
    return s


def send_command(text, host=HOST, port=PORT, quiet=False):
    """Send a raw line to the ESP32. Returns True on success."""
    if len(text) > 32:
        print(f"  error: command must be 1-32 characters (got {len(text)})", file=sys.stderr)
        return False
    s = _connect(host, port)
    if s is None:
        return False
    try:
        s.sendall(f"{text}\n".encode("ascii"))
    finally:
        s.close()
    if not quiet:
        print(f"  sent '{text}' to ESP32 ({host}:{port})")
    return True


def send_number(number, host=HOST, port=PORT):
    if not (1 <= number <= 9999):
        print(f"  error: number must be between 1 and 9999 (got {number})", file=sys.stderr)
        return False
    return send_command(f"NUM {number}", host, port)


def send_stop(text, host=HOST, port=PORT):
    if not text or len(text) > 32:
        print(f"  error: stop name must be 1-32 characters (got {len(text)})", file=sys.stderr)
        return False
    return send_command(f"STOP {text}", host, port)


def send_clr(host=HOST, port=PORT):
    return send_command("CLR", host, port)


def prompt(host=HOST, port=PORT):
    """Interactive: prompts for a number then a final stop, then keeps
    accepting updates until you type 'q' to quit."""
    print(f"Connecting to ESP32 at {host}:{port} ...")
    print("You can set the bus number and final stop, then quit with 'q'.")

    # --- Ask for the number ---
    while True:
        raw = input("\nEnter bus number (1-9999) or 'skip': ").strip()
        if raw.lower() in ("q", "quit", "exit"):
            print("Bye.")
            return
        if raw.lower() in ("skip", "s", "n"):
            print("No bus number set.")
            break
        try:
            n = int(raw)
        except ValueError:
            print("  please enter a whole number, or 'skip'.")
            continue
        if not send_number(n, host, port):
            continue
        break

    # --- Ask for the final stop ---
    while True:
        raw = input("\nEnter final stop (max 32 chars) or 'skip': ").strip()
        if raw.lower() in ("q", "quit", "exit"):
            print("Bye.")
            return
        if raw.lower() in ("skip", "s", "n", "clear"):
            print("No final stop set.")
            break
        if len(raw) > 32:
            print("  stop name must be 32 chars or fewer, or 'skip'.")
            continue
        if not send_stop(raw, host, port):
            continue
        break

    # --- Keep updating until quit ---
    while True:
        print("\nWhat next? Type a number, a stop name, 'clr' to clear, or 'q' to quit.")
        raw = input("> ").strip()
        if raw.lower() in ("q", "quit", "exit"):
            print("Bye.")
            return
        if raw.lower() in ("clr", "clear"):
            send_clr(host, port)
            continue
        low = raw.lower()
        if low.startswith("num ") or low.startswith("number ") or (
            low.startswith("stop ") or low.startswith("final ")
        ):
            # "num 23" or "stop CITY CENTRE"
            try:
                kind, value = raw.split(maxsplit=1)
            except ValueError:
                print("  usage: 'num 23' or 'stop CITY CENTRE'")
                continue
            kind = kind.lower()
            if kind.startswith("num"):
                try:
                    n = int(value)
                except ValueError:
                    print("  please enter 'num 23' with a whole number.")
                    continue
                send_number(n, host, port)
            else:
                if len(value) > 32:
                    print("  stop name must be 32 chars or fewer.")
                    continue
                send_stop(value, host, port)
        else:
            try:
                n = int(raw)
                send_number(n, host, port)
            except ValueError:
                if len(raw) > 32:
                    print("  stop name must be 32 chars or fewer.")
                    continue
                send_stop(raw, host, port)


def loop(host, port, number, interval):
    print(f"Re-sending number {number} every {interval}s (Ctrl+C to stop) ...")
    try:
        while True:
            send_number(number, host, port)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")


def main():
    parser = argparse.ArgumentParser(
        description="Display a bus number and final stop on the OUTSIDE_FRONT ESP32 panel.")
    parser.add_argument("number", type=int, nargs="?", help="Bus number to display (1-9999)")
    parser.add_argument("--stop", help="Final stop name (max 32 chars).")
    parser.add_argument("--ip", default=HOST, help=f"ESP32 IP address (default: {HOST})")
    parser.add_argument("--port", type=int, default=PORT, help=f"ESP32 TCP port (default: {PORT})")
    parser.add_argument("--loop", type=float, metavar="SECONDS",
                        help="Re-send the number every SECONDS (needs a --number).")
    parser.add_argument("--clr", action="store_true", help="Clear the number and stop.")
    args = parser.parse_args()

    if args.clr:
        if args.number is not None or args.stop is not None or args.loop:
            print("error: --clr cannot be combined with a number, --stop, or --loop", file=sys.stderr)
            sys.exit(1)
        send_clr(args.ip, args.port)
    elif args.loop:
        if args.number is None:
            print("error: --loop requires a --number", file=sys.stderr)
            sys.exit(1)
        loop(args.ip, args.port, args.number, args.loop)
    elif args.number is None and args.stop is None:
        prompt(args.ip, args.port)
    else:
        if args.number is not None and not send_number(args.number, args.ip, args.port):
            sys.exit(1)
        if args.stop is not None and not send_stop(args.stop, args.ip, args.port):
            sys.exit(1)


if __name__ == "__main__":
    main()
