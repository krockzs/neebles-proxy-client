#!/usr/bin/env python3
"""Render a deterministic, non-privileged NEEBLES Proxy Client OS-policy candidate.

This utility never installs a policy or modifies the system. The privileged
OS authority must authenticate and publish it separately on the target host.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

MODULE = "neebles-proxy-client"
EXECUTABLE = "/usr/lib/neebles/module-operations/neebles-proxy-client-platform"
EXPECTED_PERSONAL_TAILNET = "olate.pablo@gmail.com"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--desktop-uid", required=True, type=int)
    parser.add_argument("--out", type=Path, help="Write candidate to an exclusive new file")
    opts = parser.parse_args()
    if not 1 <= opts.desktop_uid <= 60000:
        parser.error("desktop uid must be 1..60000")
    try:
        process = subprocess.run(["/usr/bin/tailscale", "status", "--json"],
                                 capture_output=True, text=True, timeout=5,
                                 check=True)
        status = json.loads(process.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as exc:
        print(f"TAILSCALE_UNAVAILABLE: {exc}", file=sys.stderr)
        return 3
    if status.get("BackendState") != "Running":
        print("TAILSCALE_NOT_RUNNING", file=sys.stderr)
        return 4
    tailnet = status.get("CurrentTailnet", {}).get("Name")
    if not isinstance(tailnet, str) or not re.fullmatch(r"[a-zA-Z0-9._@-]{1,255}", tailnet):
        print("TAILNET_IDENTITY_INVALID", file=sys.stderr)
        return 5
    if tailnet != EXPECTED_PERSONAL_TAILNET:
        print("TAILNET_NOT_PERSONAL", file=sys.stderr)
        return 7
    operations = {}
    for action in ("activate", "deactivate"):
        operations[action] = {
            "executable": EXECUTABLE,
            "argv": [f"--{action}", f"--user-uid={opts.desktop_uid}",
                     f"--tailnet={tailnet}"],
        }
    candidate = {"schema": "1", "module": MODULE, "operations": operations}
    encoded = (json.dumps(candidate, indent=2, ensure_ascii=True) + "\n").encode()
    if opts.out is None:
        sys.stdout.buffer.write(encoded)
    else:
        try:
            if not opts.out.parent.is_dir():
                raise ValueError("parent directory does not exist")
            with opts.out.open("xb") as handle:
                handle.write(encoded)
        except (ValueError, OSError) as exc:
            print(f"CANDIDATE_OUTPUT_REJECTED: {exc}", file=sys.stderr)
            return 6
        print(f"CANDIDATE={opts.out}")
    print("NOT_INSTALLED=policy requires authenticated OS deployment", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
