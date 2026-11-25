#!/usr/bin/env python3
"""Add the T3 PR-check webhook to GitHub repos and ping it.

Usage: add_repo.py OWNER/REPO [OWNER/REPO ...]

Reads the webhook address and its signing secret from ~/.config/t3-pr-checks/
(files `webhook-url` and `webhook-secret`). Neither is printed or put on a
command line. A repo that already has the webhook is only pinged again.
Exits 1 if any repo fails or T3 does not accept the ping.
"""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

REPO = re.compile(r"[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9._-]+")
PING_WAIT_SECONDS = 20


def config_dir() -> Path:
    return Path(os.environ.get("T3_PR_CHECKS_DIR") or Path.home() / ".config/t3-pr-checks")


def gh(*args, body=None):
    result = subprocess.run(["gh", "api", *args], input=None if body is None else json.dumps(body),
                            capture_output=True, text=True, timeout=60, check=True)
    return json.loads(result.stdout) if result.stdout.strip() else None


def latest_delivery(repo: str, hook_id: int):
    rows = gh(f"repos/{repo}/hooks/{hook_id}/deliveries?per_page=1")
    return rows[0] if rows else None


def add_repo(repo: str, url: str, secret: str) -> str:
    """Make sure `repo` has the webhook, ping it and return a one-line result."""
    hooks = gh(f"repos/{repo}/hooks?per_page=100")
    hook = next((h for h in hooks if h.get("config", {}).get("url") == url), None)
    state = "already there"
    if hook is None:
        hook = gh("-X", "POST", f"repos/{repo}/hooks", "--input", "-", body={
            "name": "web", "active": True, "events": ["pull_request"],
            "config": {"url": url, "content_type": "json", "insecure_ssl": "0", "secret": secret}})
        state = "added"
    before = latest_delivery(repo, hook["id"])
    gh("-X", "POST", f"repos/{repo}/hooks/{hook['id']}/pings")
    for _ in range(PING_WAIT_SECONDS):
        time.sleep(1)
        delivery = latest_delivery(repo, hook["id"])
        if delivery and delivery.get("event") == "ping" and delivery.get("id") != (before or {}).get("id"):
            code = delivery.get("status_code")
            if code == 202:
                return f"webhook {state}, T3 answered 202"
            raise RuntimeError(f"webhook {state}, but T3 answered {code} to the ping")
    raise RuntimeError(f"webhook {state}, but GitHub recorded no ping within {PING_WAIT_SECONDS} s")


def main(argv) -> int:
    repos = argv[1:]
    if not repos or not all(REPO.fullmatch(repo) for repo in repos):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    try:
        url = (config_dir() / "webhook-url").read_text().strip()
        secret = (config_dir() / "webhook-secret").read_text().strip()
    except OSError as error:
        print(f"Cannot read {error.filename}", file=sys.stderr)
        return 2
    if not url.startswith("https://") or not secret:
        print(f"{config_dir()} needs an https webhook-url and a webhook-secret", file=sys.stderr)
        return 2
    failed = False
    for repo in repos:
        try:
            print(f"{repo}: {add_repo(repo, url, secret)}")
        except subprocess.CalledProcessError as error:
            failed = True
            detail = (error.stderr or "").replace(url, "<webhook-url>").replace(secret, "<secret>").strip()
            print(f"{repo}: gh failed: {detail.splitlines()[0] if detail else error.returncode}", file=sys.stderr)
        except (RuntimeError, subprocess.TimeoutExpired, ValueError, KeyError, TypeError) as error:
            failed = True
            print(f"{repo}: {str(error).replace(url, '<webhook-url>').replace(secret, '<secret>')}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
