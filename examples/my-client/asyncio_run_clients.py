#!/usr/bin/env python3
import argparse, asyncio, contextlib, json, os, pathlib, random, signal, sys
from datetime import datetime, timezone

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
               help="Directory to write per-client logs (one pair per cycle). If omitted, logs go to DEVNULL.")

# Async behavior
p.add_argument("--online-for", type=float, default=120.0, help="Seconds each client stays online per cycle.")
p.add_argument("--offline-for", type=float, default=30.0, help="Seconds each client stays offline per cycle.")
p.add_argument("--cycles", type=int, default=0, help="Number of (online+offline) cycles per client. 0 = infinite.")
p.add_argument("--jitter", type=float, default=5.0,
               help="Max +/- seconds jitter added independently to online and offline intervals.")

# Concurrency cap (avoid resource exhaustion on laptops)
p.add_argument("--max-concurrent", type=int, default=0,
               help="Max clients online at once (0 or negative = unlimited).")

# Legacy single NodePort override (kept for backward compatibility)
p.add_argument("--node-ip", type=str, default=None, help="Override combiner host to Node IP (e.g., Tailscale IP).")
p.add_argument("--node-port", type=int, default=None, help="Override combiner gRPC NodePort (requires --node-ip).")

# Multiple combiners mapping
p.add_argument("--combiner", dest="combiners", action="append", default=[],
               help="Add mapping 'NAME@IP:PORT'. Can be given multiple times.")
p.add_argument("--combiner-map", type=pathlib.Path, default=None,
               help='JSON file with mapping: {"comb-a":{"ip":"100.64.0.12","port":32090}, ...}')
p.add_argument("--assign", choices=["round-robin", "hash", "random"], default="round-robin",
               help="Strategy to assign clients across provided combiners (default: round-robin).")

args = p.parse_args()

BIN = args.bin.resolve()
if not BIN.exists():
    print(f"ERROR: client binary not found at: {BIN}", file=sys.stderr); sys.exit(1)

if (args.node_ip is None) ^ (args.node_port is None):
    print("ERROR: --node-ip and --node-port must be provided together.", file=sys.stderr); sys.exit(2)
if args.node_port is not None and not (1 <= args.node_port <= 65535):
    print("ERROR: --node-port must be in range 1..65535.", file=sys.stderr); sys.exit(2)

if args.log_dir:
    args.log_dir.mkdir(parents=True, exist_ok=True)

def parse_inline_combiners(specs):
    mapping = {}
    for s in specs:
        try:
            name, rest = s.split("@", 1)
            host, port_s = rest.rsplit(":", 1)
            name, host, port = name.strip(), host.strip(), int(port_s.strip())
            if not name or not host or not (1 <= port <= 65535): raise ValueError
            mapping[name] = {"ip": host, "port": port}
        except Exception:
            print(f"ERROR: invalid --combiner spec '{s}'. Expected NAME@IP:PORT", file=sys.stderr); sys.exit(2)
    return mapping

def load_map_file(path: pathlib.Path | None):
    if not path: return {}
    if not path.exists():
        print(f"ERROR: --combiner-map not found: {path}", file=sys.stderr); sys.exit(2)
    try:
        data = json.loads(path.read_text())
    except Exception as e:
        print(f"ERROR: failed to read JSON from {path}: {e}", file=sys.stderr); sys.exit(2)
    mapping = {}
    for name, item in data.items():
        try:
            ip, port = item["ip"], int(item["port"])
            if not ip or not (1 <= port <= 65535): raise ValueError
            mapping[str(name)] = {"ip": ip, "port": port}
        except Exception:
            print(f"ERROR: invalid mapping entry for '{name}' in {path}. Expected {{\"ip\":\"...\",\"port\":12345}}",
                  file=sys.stderr); sys.exit(2)
    return mapping

COMBINER_MAP = load_map_file(args.combiner_map)
COMBINER_MAP.update(parse_inline_combiners(args.combiners))
COMBINER_NAMES = sorted(COMBINER_MAP.keys())

def choose_combiner_for_client(client_id: int):
    if not COMBINER_NAMES: return None
    if args.assign == "round-robin":
        idx = (client_id - args.start_id) % len(COMBINER_NAMES)
    elif args.assign == "hash":
        idx = hash(client_id) % len(COMBINER_NAMES)
    else:
        idx = random.randrange(len(COMBINER_NAMES))
    name = COMBINER_NAMES[idx]; entry = COMBINER_MAP[name]
    return name, entry["ip"], int(entry["port"])

def ts(): return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")

def jittered(s: float) -> float:
    if args.jitter <= 0: return max(0.0, s)
    return max(0.0, s + random.uniform(-args.jitter, args.jitter))

stop_event = asyncio.Event()

async def run_client(client_id: int, sem: asyncio.Semaphore | None):
    name = f"client-{client_id}"
    selected = choose_combiner_for_client(client_id)
    env = os.environ.copy()

    base_cmd = [str(BIN), f"--discover_host={args.host}", f"--token={args.token}",
                f"--name={name}", f"--client_id={client_id}"]
    if selected:
        comb_name, ip, port = selected
        env["FEDN_PREFERRED_COMBINER"] = comb_name
        base_cmd += [f"--preferred_combiner={comb_name}", f"--node_ip={ip}", f"--node_port={port}"]
    elif args.node_ip and args.node_port:
        base_cmd += [f"--node_ip={args.node_ip}", f"--node_port={args.node_port}"]

    cycle = 0
    while not stop_event.is_set() and (args.cycles == 0 or cycle < args.cycles):
        # Limit how many are online concurrently
        ctx = sem if sem is not None else contextlib.nullcontext()
        async with ctx:
            # ONLINE: start client process (heartbeat maintained inside the proc)
            t = ts()
            stdout = stderr = asyncio.subprocess.DEVNULL
            out_f = err_f = None
            if args.log_dir:
                out_f = (args.log_dir / f"{name}.cycle{cycle}.{t}.out").open("w")
                err_f = (args.log_dir / f"{name}.cycle{cycle}.{t}.err").open("w")
                stdout, stderr = out_f, err_f

            cmd = list(base_cmd)
            print("CMD:", " ".join(cmd))
            proc = await asyncio.create_subprocess_exec(*cmd, stdout=stdout, stderr=stderr,
                                                        start_new_session=True, env=env)

            # stay online for duration unless stopped
            online = jittered(args.online_for)
            try:
                await asyncio.wait_for(proc.wait(), timeout=online)
            except asyncio.TimeoutError:
                with contextlib.suppress(ProcessLookupError):
                    proc.terminate()
                try:
                    await asyncio.wait_for(proc.wait(), timeout=3.0)
                except asyncio.TimeoutError:
                    with contextlib.suppress(ProcessLookupError):
                        proc.kill()
                    await proc.wait()

            # Close log files promptly to free FDs
            for f in (out_f, err_f):
                if f: f.close()

        # OFFLINE
        offline = jittered(args.offline_for)
        # early exit check during offline
        try:
            await asyncio.wait_for(stop_event.wait(), timeout=offline)
        except asyncio.TimeoutError:
            pass

        cycle += 1

async def main():
    # Optional: small warm tip printed once
    print("LAUNCH ARGS:", "host=", args.host, "count=", args.count,
          "log_dir=", args.log_dir, "max_concurrent=", args.max_concurrent)

    sem = None
    if args.max_concurrent and args.max_concurrent > 0:
        sem = asyncio.Semaphore(args.max_concurrent)

    tasks = []
    for i in range(args.count):
        cid = args.start_id + i
        # stagger starts
        await asyncio.sleep(args.delay if i > 0 else 0.0)
        tasks.append(asyncio.create_task(run_client(cid, sem)))

    # Handle SIGINT/SIGTERM → set stop_event
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        with contextlib.suppress(NotImplementedError):
            loop.add_signal_handler(sig, stop_event.set)

    try:
        await asyncio.gather(*tasks)
    finally:
        stop_event.set()
        await asyncio.sleep(0.1)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
