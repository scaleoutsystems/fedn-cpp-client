#!/usr/bin/env python3
"""
Run multiple C++ FEDn clients with async (online/offline) behavior.

Examples
--------
# Register via Controller API; transport decided by server
python run_clients_async.py --host http://10.0.0.5:8092 --token ABC --count 50 \
  --online-for 120 --offline-for 60 --cycles 5 --delay 0.5

# Force direct gRPC via NodeIP:NodePort to a specific combiner (legacy global override)
python run_clients_async.py --host https://controller/api --token ABC \
  --node-ip 100.64.0.12 --node-port 32090 \
  --count 20 --online-for 90 --offline-for 30 --cycles 10

# Map multiple combiners and auto-assign clients round-robin
python run_clients_async.py --host https://controller/api --token ABC \
  --combiner comb-a@100.64.0.12:32090 --combiner comb-b@100.64.0.13:32091 \
  --assign round-robin --count 20

# Use JSON map file instead of repeated --combiner flags
# map.json: {"comb-a":{"ip":"100.64.0.12","port":32090}, "comb-b":{"ip":"100.64.0.13","port":32091}}
python run_clients_async.py --host https://controller/api --token ABC \
  --combiner-map map.json --assign hash --count 100
"""
import argparse
import pathlib
import subprocess
import signal
import sys
import time
import threading
import random
import json
from datetime import datetime
import os

# repo_root/examples/my-client/this_script.py  -> repo_root
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_BIN = REPO_ROOT / "build" / "examples" / "my-client" / "my-client"

p = argparse.ArgumentParser(description="Run multiple C++ FEDn clients with async online/offline cycles.")
p.add_argument("--bin", type=pathlib.Path, default=DEFAULT_BIN,
               help="Path to the C++ client binary (default: build/examples/my-client/my-client).")
p.add_argument("--count", type=int, default=10, help="Number of clients to run.")
p.add_argument("--start-id", type=int, default=1, help="First numeric client_id to use.")
p.add_argument("--host", type=str, required=True, help="Controller API base (discover_host).")
p.add_argument("--token", type=str, required=True, help="Client token for the FEDn Controller.")
p.add_argument("--delay", type=float, default=0.10, help="Initial stagger (seconds) between starting clients.")
p.add_argument("--log-dir", type=pathlib.Path, default=None,
               help="Directory to write per-client logs (one pair per cycle).")
# Async behavior
p.add_argument("--online-for", type=float, default=120.0, help="Seconds each client stays online per cycle.")
p.add_argument("--offline-for", type=float, default=30.0, help="Seconds each client stays offline per cycle.")
p.add_argument("--cycles", type=int, default=100, help="Number of (online+offline) cycles per client. Use 0 for infinite.")
p.add_argument("--jitter", type=float, default=20.0,
               help="Max +/- seconds jitter added independently to online and offline intervals.")

# Legacy single NodePort override (kept for backward compatibility)
p.add_argument("--node-ip", type=str, default=None, help="Override combiner host to Node IP (e.g., Tailscale IP).")
p.add_argument("--node-port", type=int, default=None, help="Override combiner gRPC NodePort (requires --node-ip).")

# NEW: Multiple combiners mapping
p.add_argument("--combiner", dest="combiners", action="append", default=[],
               help="Add mapping 'NAME@IP:PORT'. Can be given multiple times.")
p.add_argument("--combiner-map", type=pathlib.Path, default=None,
               help="JSON file with mapping: {NAME: {\"ip\": \"100.64.0.12\", \"port\": 32090}, ...}")
p.add_argument("--assign", choices=["round-robin", "hash", "random"], default="round-robin",
               help="Strategy to assign clients across provided combiners (default: round-robin).")

args = p.parse_args()

print("LAUNCH ARGS:",
      "node_ip=", repr(args.node_ip),
      "node_port=", args.node_port,
      "host=", args.host)

BIN = args.bin.resolve()
if not BIN.exists():
    print(f"ERROR: client binary not found at: {BIN}", file=sys.stderr)
    sys.exit(1)

# Validate legacy pair
if (args.node_ip is None) ^ (args.node_port is None):
    print("ERROR: --node-ip and --node-port must be provided together.", file=sys.stderr)
    sys.exit(2)
if args.node_port is not None and not (1 <= args.node_port <= 65535):
    print("ERROR: --node-port must be in range 1..65535.", file=sys.stderr)
    sys.exit(2)

if args.log_dir:
    args.log_dir.mkdir(parents=True, exist_ok=True)

# -----------------------
# Combiner mapping logic
# -----------------------
def parse_inline_combiners(specs: list[str]) -> dict[str, dict]:
    """
    Parse repeated --combiner NAME@IP:PORT entries into a dict:
    { NAME: {"ip": IP, "port": PORT} }
    """
    mapping: dict[str, dict] = {}
    for s in specs:
        try:
            name, rest = s.split("@", 1)
            host, port_s = rest.rsplit(":", 1)
            name = name.strip()
            host = host.strip()
            port = int(port_s.strip())
            if not name or not host or not (1 <= port <= 65535):
                raise ValueError
            mapping[name] = {"ip": host, "port": port}
        except Exception:
            print(f"ERROR: invalid --combiner spec '{s}'. Expected NAME@IP:PORT", file=sys.stderr)
            sys.exit(2)
    return mapping

def load_map_file(path: pathlib.Path | None) -> dict[str, dict]:
    if not path:
        return {}
    if not path.exists():
        print(f"ERROR: --combiner-map not found: {path}", file=sys.stderr)
        sys.exit(2)
    try:
        data = json.loads(path.read_text())
    except Exception as e:
        print(f"ERROR: failed to read JSON from {path}: {e}", file=sys.stderr)
        sys.exit(2)
    mapping: dict[str, dict] = {}
    for name, item in data.items():
        try:
            ip = item["ip"]
            port = int(item["port"])
            if not ip or not (1 <= port <= 65535):
                raise ValueError
            mapping[str(name)] = {"ip": ip, "port": port}
        except Exception:
            print(f"ERROR: invalid mapping entry for '{name}' in {path}. "
                  f"Expected {{\"ip\":\"...\",\"port\":12345}}", file=sys.stderr)
            sys.exit(2)
    return mapping

# Merge inline and file maps (inline wins on conflicts)
COMBINER_MAP = load_map_file(args.combiner_map)
COMBINER_MAP.update(parse_inline_combiners(args.combiners))

COMBINER_NAMES: list[str] = sorted(COMBINER_MAP.keys())

def choose_combiner_for_client(client_id: int) -> tuple[str, str, int] | None:
    """
    Returns (name, ip, port) for the client, or None if no mapping provided.
    """
    if not COMBINER_NAMES:
        return None
    if args.assign == "round-robin":
        idx = (client_id - args.start_id) % len(COMBINER_NAMES)
    elif args.assign == "hash":
        idx = hash(client_id) % len(COMBINER_NAMES)
    else:  # random
        idx = random.randrange(len(COMBINER_NAMES))
    name = COMBINER_NAMES[idx]
    entry = COMBINER_MAP[name]
    return name, entry["ip"], int(entry["port"])

stop_event = threading.Event()
client_threads: list[threading.Thread] = []
procs_lock = threading.Lock()
# Track live PIDs so we can shut them down on SIGINT/SIGTERM
live_procs: dict[int, subprocess.Popen] = {}

def jittered(seconds: float) -> float:
    if args.jitter <= 0:
        return max(0.0, seconds)
    delta = random.uniform(-args.jitter, args.jitter)
    return max(0.0, seconds + delta)

def bounded_sleep(total: float) -> bool:
    """Sleep up to total seconds, waking periodically to check stop_event.
    Returns True if stopped early."""
    end = time.time() + total
    while time.time() < end:
        if stop_event.is_set():
            return True
        time.sleep(min(0.2, end - time.time()))
    return stop_event.is_set()

def make_cmd(client_id: int, name: str) -> list[str]:
    cmd = [
        str(BIN),
        f"--discover_host={args.host}",
        f"--token={args.token}",
        f"--name={name}",
        f"--client_id={client_id}",
    ]

    # Prefer per-combiner mapping if provided
    selected = choose_combiner_for_client(client_id)
    if selected:
        comb_name, ip, port = selected
        cmd.append(f"--preferred_combiner={comb_name}")
        cmd.append(f"--node_ip={ip}")
        cmd.append(f"--node_port={port}")
    else:
        # Fallback to legacy single override if present
        if args.node_ip and args.node_port:
            cmd.append(f"--node_ip={args.node_ip}")
            cmd.append(f"--node_port={args.node_port}")
        # else: controller-only; no NodePort override, no preferred_combiner

    return cmd

def launch(client_id: int, cycle_idx: int) -> subprocess.Popen:
    name = f"client-{client_id}"
    cmd = make_cmd(client_id, name)
    ts = datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")

    stdout = stderr = None
    if args.log_dir:
        # one file per cycle so logs don’t interleave across sessions
        out_path = args.log_dir / f"{name}.cycle{cycle_idx}.{ts}.out"
        err_path = args.log_dir / f"{name}.cycle{cycle_idx}.{ts}.err"
        stdout = open(out_path, "w")
        stderr = open(err_path, "w")

    print("CMD:", " ".join(cmd))
    env = os.environ.copy()
    selected = choose_combiner_for_client(client_id)
    if selected:
        comb_name, ip, port = selected
        env["FEDN_PREFERRED_COMBINER"] = comb_name  # <-- key line
        # (cmd already has --node_ip and --node_port)
    else:
        env.pop("FEDN_PREFERRED_COMBINER", None)

    proc = subprocess.Popen(cmd, stdout=stdout, stderr=stderr,
                            start_new_session=True, env=env)
    with procs_lock:
        live_procs[client_id] = proc
    return proc

def stop_proc(proc: subprocess.Popen, client_id: int, grace: float = 3.0):
    try:
        proc.terminate()
    except Exception:
        pass
    try:
        proc.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        try:
            proc.kill()
        except Exception:
            pass
    with procs_lock:
        live_procs.pop(client_id, None)

def client_worker(client_id: int, initial_delay: float):
    # per-client start delay (stagger)
    if bounded_sleep(initial_delay):
        return

    cycle = 0
    while not stop_event.is_set() and (args.cycles == 0 or cycle < args.cycles):
        # ONLINE
        proc = launch(client_id, cycle)
        online = jittered(args.online_for)
        early = bounded_sleep(online)
        stop_proc(proc, client_id)
        if early:
            break

        # OFFLINE
        offline = jittered(args.offline_for)
        if bounded_sleep(offline):
            break

        cycle += 1

def shutdown(*_):
    print("Shutting down all clients...")
    stop_event.set()
    # Stop all child processes
    with procs_lock:
        procs = list(live_procs.items())
    for cid, proc in procs:
        stop_proc(proc, cid, grace=2.0)
    sys.exit(0)

signal.signal(signal.SIGINT, shutdown)
signal.signal(signal.SIGTERM, shutdown)

# Start threads (one per client), staggered by --delay
for i in range(args.count):
    cid = args.start_id + i
    t = threading.Thread(target=client_worker, args=(cid, i * args.delay), daemon=True)
    client_threads.append(t)
    t.start()

# Join threads
try:
    for t in client_threads:
        while t.is_alive():
            t.join(timeout=0.5)
except KeyboardInterrupt:
    shutdown()

print("All clients have finished.")
