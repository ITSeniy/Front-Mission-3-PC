#!/usr/bin/env python3
"""Capture the halted post-training scheduler state into one JSON artifact."""

from __future__ import annotations

import argparse
import json
import socket
from datetime import datetime, timezone
from pathlib import Path


def query(port: int, request: dict) -> dict:
    payload = (json.dumps(request, separators=(",", ":")) + "\n").encode()
    with socket.create_connection(("127.0.0.1", port), timeout=10.0) as sock:
        sock.settimeout(20.0)
        sock.sendall(payload)
        data = bytearray()
        while True:
            chunk = sock.recv(1 << 20)
            if not chunk:
                break
            data.extend(chunk)
            try:
                return json.loads(data.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
    raise RuntimeError(f"incomplete response for {request['cmd']!r}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=4480)
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("build-debug/post_training_scheduler_capture.json"),
    )
    args = parser.parse_args()

    initial_commands = [
        {"cmd": "ping"},
        {"cmd": "get_registers"},
        {"cmd": "sched_escape_ring", "count": 256},
    ]
    commands = [
        {"cmd": "thread_ctx_ring", "count": 256},
        {"cmd": "thread_trace", "count": 2048, "newest": 0},
        {"cmd": "fntrace_dump", "count": 4096},
        {"cmd": "freeze_check", "window": 4096},
    ]

    result = {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "port": args.port,
        "responses": {},
    }
    request_id = 0
    for command in initial_commands:
        request_id += 1
        request = {"id": request_id, **command}
        result["responses"][command["cmd"]] = query(args.port, request)

    frame = int(result["responses"]["get_registers"].get("frame", 0))
    commands.insert(
        0,
        {
            "cmd": "irqctx_ring",
            "frame_lo": max(0, frame - 2),
            "frame_hi": frame,
            "count": 4096,
        },
    )
    current_tcb = int(
        result["responses"]["sched_escape_ring"]["scheduler"]["current_tcb"],
        16,
    )
    if current_tcb:
        tcb_phys = current_tcb & 0x1FFFFFFF
        request_id += 1
        result["responses"]["current_tcb_ram"] = query(
            args.port,
            {
                "id": request_id,
                "cmd": "read_ram",
                "addr": f"0x{tcb_phys:08X}",
                "len": 0xA0,
            },
        )
        commands.insert(
            1,
            {
                "cmd": "wtrace_all_dump",
                "addr_lo": f"0x{tcb_phys:08X}",
                "addr_hi": f"0x{tcb_phys + 0xA0:08X}",
                "count": 2048,
                "newest": 1,
            },
        )
    for command in commands:
        request_id += 1
        request = {"id": request_id, **command}
        result["responses"][command["cmd"]] = query(args.port, request)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(args.out.resolve())


if __name__ == "__main__":
    main()
