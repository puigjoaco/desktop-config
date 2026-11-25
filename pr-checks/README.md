# Automatic PR checks in T3 Code

When someone else opens or updates a pull request on one of my public GitHub repos, GitHub tells T3 Code, and T3 starts a thread that reads the PR and writes me a short report in Spanish with a recommendation. Nothing is posted, merged or run on my computer until I say so in that thread.

This is Matthew Stanton's "T3 automatic PR checks" setup with one change: a single T3 task and a single secret serve every repo, and all reports land in one T3 project, instead of one task per repo. GitHub personal accounts have no account-wide webhooks, so each repo still gets its own webhook.

- [`prompt.md`](prompt.md): the instructions each run follows. It names my GitHub login, my repos and paths on my machine; adapt them.
- [`add_repo.py`](add_repo.py): adds the webhook to a repo and pings it.

Two private files stay outside every repo, readable only by me: `~/.config/t3-pr-checks/webhook-secret` and `~/.config/t3-pr-checks/webhook-url`. Never commit or paste them.

## How it works

1. GitHub sends a signed `pull_request` event to the task's webhook address, which T3 Connect makes reachable from the internet.
2. T3 drops events with a wrong signature, events about my own PRs and bot PRs, then waits 10 minutes so a burst of pushes counts as one update.
3. A new thread starts in the task's project. The run does not see the event, so it lists the open PRs of all my public repos and picks those not reported yet.
4. It reads the PR in a temporary clone without running its code, has a second agent review it, and writes the report. It renames itself after the PR, for example `PR pneu #42 · Guarda imágenes en Imágenes (author) · listo`.
5. When I answer **publícalo** it posts the review and keeps watching the PR for replies.

## Set up from scratch

Needs T3 Code, the `gh` CLI signed in with the `repo` scope, and `python3`.

1. Turn on T3 Connect: run `t3 connect link`, approve it in the browser, then restart T3 Code. `t3 connect status` must end with "Other devices can reach this machine".
2. Make the secret:

   ```sh
   mkdir -p ~/.config/t3-pr-checks && chmod 700 ~/.config/t3-pr-checks
   (umask 077; openssl rand -hex 32 > ~/.config/t3-pr-checks/webhook-secret)
   ```

3. Create the task. In a thread of the T3 project where reports should land, ask the agent to call `schedule_task` with the text of `prompt.md` as `prompt`, the secret file's content as `SECRET`, and these settings:

   ```json
   {
     "title": "PR check · puigjoaco",
     "bindToCurrentThread": false,
     "schedule": {
       "type": "webhook",
       "filters": [
         { "path": "headers.x-github-event", "op": "equals", "value": "pull_request" },
         { "path": "body.action", "op": "in",
           "value": ["opened", "reopened", "ready_for_review", "synchronize"] },
         { "path": "body.repository.owner.login", "op": "equals", "value": "puigjoaco" },
         { "path": "body.pull_request.user.login", "op": "notEquals", "value": "puigjoaco" },
         { "path": "body.pull_request.user.type", "op": "notEquals", "value": "Bot" }
       ],
       "signature": { "header": "x-hub-signature-256", "encoding": "hex",
                      "prefix": "sha256=", "secret": "SECRET" },
       "dedupe": { "replayKey": "{{headers.x-github-delivery}}",
                   "coalesceKey": "{{body.repository.full_name}}#{{body.pull_request.number}}",
                   "coalesceWindowSeconds": 600 },
       "maxDeliveryAgeMinutes": 1440
     }
   }
   ```

   It returns a `webhookUrl`, only this once. Save it as `~/.config/t3-pr-checks/webhook-url` with mode 600.
4. Add the webhook to each public repo. A line ending in `T3 answered 202` means T3 received GitHub's ping:

   ```sh
   ./add_repo.py puigjoaco/pneu puigjoaco/desktop-config
   ```

5. Test it with a signed fake event. It prints `right signature 202` and `wrong signature 401`. About 10 minutes later a thread appears and, with no outside PRs open, renames itself `PR check · nada nuevo`.

   The snippet reads both private files itself, so neither appears in a command line:

   ```sh
   python3 - <<'PY'
   import hashlib, hmac, json, pathlib, time, urllib.error, urllib.request
   d = pathlib.Path.home() / ".config/t3-pr-checks"
   url = (d / "webhook-url").read_text().strip()
   secret = (d / "webhook-secret").read_text().strip().encode()
   body = json.dumps({"action": "synchronize", "number": 1,
                      "pull_request": {"number": 1, "user": {"login": "someone", "type": "User"}},
                      "repository": {"full_name": "puigjoaco/desktop-config", "owner": {"login": "puigjoaco"}}}).encode()
   for name, key in (("right signature", secret), ("wrong signature", b"wrong")):
       headers = {"Content-Type": "application/json", "X-GitHub-Event": "pull_request", "User-Agent": "pr-check-test",
                  "X-GitHub-Delivery": f"test-{time.time_ns()}",
                  "X-Hub-Signature-256": "sha256=" + hmac.new(key, body, hashlib.sha256).hexdigest()}
       try:
           with urllib.request.urlopen(urllib.request.Request(url, body, headers), timeout=30) as reply:
               print(name, reply.status)
       except urllib.error.HTTPError as error:
           print(name, error.code)
   PY
   ```

## A new public repo

Run `./add_repo.py puigjoaco/<repo>`. The task already covers every public repo of the account; optionally add a line about the repo's risky areas to the task's prompt and to `prompt.md`.

## Turning it off

Pause or delete the T3 task `PR check · puigjoaco` (Settings in T3 Code, or ask an agent), and delete the webhook under each repo's **Settings → Webhooks**.

## Limits

- The computer must be on with T3 Code running. T3's relay holds events while it is off, and the task skips any older than a day.
- Each run costs an agent session even when it finds nothing new.
- A PR opened while the webhook is missing from a repo is never reported.

Tests: `python3 -m unittest pr-checks/tests/test_add_repo.py` from the repo root.

Set up on Kubuntu 26.04 with T3 Code Nightly on 25 November 2026.
