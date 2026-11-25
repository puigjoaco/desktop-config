"""add_repo adds the PR-check webhook once, pings it and keeps its address and secret private."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "add_repo.py"
spec = importlib.util.spec_from_file_location("add_repo", SCRIPT)
add_repo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(add_repo)

URL = "https://relay.example/v1/hooks/abc/url-token"
SECRET = "signing-secret-must-not-print"


class FakeGitHub:
    """Answers the `gh api` calls add_repo makes for one repo."""

    def __init__(self, hooks=(), ping_code=202, record_ping=True):
        self.hooks, self.ping_code, self.record_ping = list(hooks), ping_code, record_ping
        self.calls, self.deliveries = [], [{"id": 1, "event": "pull_request", "status_code": 202}]

    def __call__(self, *args, body=None):
        self.calls.append((args, body))
        path = args[-1] if args[0] != "-X" else args[2]
        if args[0] == "-X" and path.endswith("/hooks"):
            self.hooks.append({"id": 7, "config": {"url": body["config"]["url"]}})
            return self.hooks[-1]
        if args[0] == "-X" and path.endswith("/pings"):
            if self.record_ping:
                self.deliveries.insert(0, {"id": 2, "event": "ping", "status_code": self.ping_code})
            return None
        if "/deliveries" in path:
            return self.deliveries[:1]
        return self.hooks


class AddRepoTest(unittest.TestCase):
    def run_add(self, fake):
        with mock.patch.object(add_repo, "gh", fake), mock.patch.object(add_repo.time, "sleep", lambda seconds: None):
            return add_repo.add_repo("me/repo", URL, SECRET)

    def test_adds_a_pull_request_webhook_and_pings_it(self):
        fake = FakeGitHub()
        self.assertEqual(self.run_add(fake), "webhook added, T3 answered 202")
        created = [body for args, body in fake.calls if body]
        self.assertEqual(created, [{"name": "web", "active": True, "events": ["pull_request"],
                                    "config": {"url": URL, "content_type": "json", "insecure_ssl": "0", "secret": SECRET}}])
        for args, _ in fake.calls:
            self.assertNotIn(URL, " ".join(args))
            self.assertNotIn(SECRET, " ".join(args))

    def test_an_existing_webhook_is_only_pinged(self):
        fake = FakeGitHub(hooks=[{"id": 3, "config": {"url": "https://other.example"}}, {"id": 5, "config": {"url": URL}}])
        self.assertEqual(self.run_add(fake), "webhook already there, T3 answered 202")
        self.assertFalse([body for _, body in fake.calls if body])
        self.assertIn(("-X", "POST", "repos/me/repo/hooks/5/pings"), [args for args, _ in fake.calls])

    def test_a_refused_or_missing_ping_is_an_error(self):
        with self.assertRaisesRegex(RuntimeError, "answered 401"):
            self.run_add(FakeGitHub(ping_code=401))
        with self.assertRaisesRegex(RuntimeError, "no ping"):
            self.run_add(FakeGitHub(record_ping=False))

    def run_main(self, *repos, files=True, gh=None):
        with tempfile.TemporaryDirectory() as temp:
            if files:
                (Path(temp) / "webhook-url").write_text(URL + "\n")
                (Path(temp) / "webhook-secret").write_text(SECRET + "\n")
            out, err = io.StringIO(), io.StringIO()
            with mock.patch.dict(os.environ, {"T3_PR_CHECKS_DIR": temp}), \
                    mock.patch.object(add_repo, "gh", gh or FakeGitHub()), mock.patch.object(add_repo.time, "sleep", lambda seconds: None), \
                    contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = add_repo.main(["add_repo.py", *repos])
            return code, out.getvalue(), err.getvalue()

    def test_main_reports_each_repo(self):
        self.assertEqual(self.run_main("me/repo"), (0, "me/repo: webhook added, T3 answered 202\n", ""))

    def test_main_rejects_bad_arguments_and_missing_files(self):
        self.assertEqual(self.run_main()[0], 2)
        self.assertEqual(self.run_main("not-a-repo")[0], 2)
        self.assertEqual(self.run_main("me/repo/../other")[0], 2)
        code, out, err = self.run_main("me/repo", files=False)
        self.assertEqual((code, out), (2, ""))
        self.assertIn("webhook-url", err)

    def test_main_never_prints_the_address_or_the_secret(self):
        def failing(*args, body=None):
            raise subprocess.CalledProcessError(1, "gh", stderr=f"gh: could not reach {URL} with {SECRET}\nmore")
        code, out, err = self.run_main("me/repo", gh=failing)
        self.assertEqual((code, out), (1, ""))
        self.assertEqual(err, "me/repo: gh failed: gh: could not reach <webhook-url> with <secret>\n")


if __name__ == "__main__":
    unittest.main()
