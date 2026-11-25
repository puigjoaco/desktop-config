#!/usr/bin/env python3
"""Local Sunshine API helper; never puts the admin password in arguments/output."""
import argparse
import base64
import getpass
import json
from pathlib import Path
import re
import ssl
import sys
import urllib.request


def request(path, payload=None):
    home = Path.home()
    credentials = home / ".config/sunshine-admin/admin.json"
    if credentials.stat().st_mode & 0o077:
        raise RuntimeError("Admin credential file must have mode 0600")
    admin = json.loads(credentials.read_text())
    # Trust only this host's generated Sunshine certificate. Its CN is not
    # localhost, so disable name matching while retaining certificate validation.
    context = ssl.create_default_context(
        cafile=str(home / ".config/sunshine/credentials/cacert.pem")
    )
    context.check_hostname = False
    auth = base64.b64encode(
        f"{admin['username']}:{admin['password']}".encode()
    ).decode()
    headers = {"Authorization": f"Basic {auth}"}
    data = None
    if payload is not None:
        data = json.dumps(payload).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(
        "https://localhost:47990" + path, data=data, headers=headers
    )
    with urllib.request.urlopen(req, context=context, timeout=15) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["pending", "pair", "health"])
    parser.add_argument("--pairing-id", help="Required if several requests are pending")
    parser.add_argument("--name", help="Override the device name reported by Moonlight")
    parser.add_argument("--pin-file", type=Path, help="Read the PIN from a private file")
    args = parser.parse_args()
    if args.action == "health":
        config = request("/api/config")
        print(json.dumps({key: config.get(key) for key in [
            "capture", "encoder", "output_name", "origin_web_ui_allowed", "upnp"
        ]}, indent=2))
        return
    pairings = request("/api/pin")["pairings"]
    if args.action == "pending":
        print(json.dumps(pairings, indent=2))
        return
    if args.pairing_id:
        matches = [p for p in pairings if p["id"] == args.pairing_id]
    else:
        matches = pairings
    if len(matches) != 1:
        raise RuntimeError("Need exactly one matching pending client; run 'pending' first")
    if args.pin_file:
        if args.pin_file.stat().st_mode & 0o077:
            raise RuntimeError("PIN file must be private (mode 0600)")
        pin = args.pin_file.read_text().strip()
    else:
        pin = getpass.getpass("Moonlight PIN: ")
    if not re.fullmatch(r"[0-9]{4}", pin):
        raise RuntimeError("Expected four numeric PIN digits")
    result = request("/api/pin", {
        "pairing_id": matches[0]["id"], "pin": pin,
        "name": args.name or matches[0].get("name") or "Moonlight client"
    })
    if not result.get("status"):
        raise RuntimeError("Sunshine did not accept the pairing")
    print("Moonlight client paired successfully.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Sunshine API: {error}", file=sys.stderr)
        sys.exit(1)
