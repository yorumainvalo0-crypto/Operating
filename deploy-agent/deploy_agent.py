#!/usr/bin/env python3
"""Deploy agent: pulls the latest code from GitHub into a website directory.

Triggered either manually (update button on the website) or automatically
(GitHub push webhook). Uses only the Python standard library and git.
"""

import hashlib
import hmac
import json
import os
import subprocess
import sys
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

CONFIG_PATH = os.environ.get("DEPLOY_AGENT_CONFIG", "/etc/deploy-agent/config.json")
WIDGET_FILE = Path(__file__).resolve().parent / "widget.js"
MAX_BODY_BYTES = 5 * 1024 * 1024
COMMAND_TIMEOUT_SECONDS = 900
MAX_LOG_CHARS = 20000


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class DeployError(Exception):
    pass


class Project:
    def __init__(self, name, cfg):
        self.name = name
        self.repo = cfg["repo"]
        self.branch = cfg.get("branch", "main")
        self.directory = Path(cfg["path"])
        self.webhook_secret = cfg.get("webhook_secret", "")
        self.auto_update = bool(cfg.get("auto_update", False))
        self.post_deploy = list(cfg.get("post_deploy", []))
        self.last_run = None
        self._state_lock = threading.Lock()
        self._running = False
        self._pending = False

    # --- public API -------------------------------------------------------

    def deploy(self, trigger):
        """Start a deploy in the background. Returns 'started' or 'queued'."""
        with self._state_lock:
            if self._running:
                # Remember the request so a push during a deploy is not lost.
                self._pending = True
                return "queued"
            self._running = True
        threading.Thread(target=self._worker, args=(trigger,), daemon=True).start()
        return "started"

    def status(self):
        with self._state_lock:
            running, pending = self._running, self._pending
        return {
            "project": self.name,
            "branch": self.branch,
            "auto_update": self.auto_update,
            "running": running,
            "pending": pending,
            "commit": self._commit_info(),
            "last_run": self.last_run,
        }

    # --- deploy logic -----------------------------------------------------

    def _worker(self, trigger):
        while True:
            self._run_once(trigger)
            with self._state_lock:
                if not self._pending:
                    self._running = False
                    return
                self._pending = False
            trigger = "queued"

    def _run_once(self, trigger):
        previous_state = self.last_run["state"] if self.last_run else None
        run = {
            "trigger": trigger,
            "state": "running",
            "started_at": now_iso(),
            "finished_at": None,
            "from": None,
            "to": None,
            "log": "",
        }
        self.last_run = run
        try:
            if (self.directory / ".git").is_dir():
                run["from"] = self._head()
                self._exec(run, ["git", "remote", "set-url", "origin", self.repo])
                self._exec(run, ["git", "fetch", "--prune", "origin", self.branch])
                self._exec(run, ["git", "reset", "--hard", f"origin/{self.branch}"])
            else:
                if self.directory.exists() and any(self.directory.iterdir()):
                    raise DeployError(
                        f"{self.directory} is not empty and not a git checkout"
                    )
                self.directory.parent.mkdir(parents=True, exist_ok=True)
                self._exec(
                    run,
                    ["git", "clone", "--branch", self.branch, "--single-branch",
                     self.repo, str(self.directory)],
                    cwd=self.directory.parent,
                )
            run["to"] = self._head()

            # Skip build/restart only if nothing changed and the last run was fine,
            # so a failed build can be retried with the same commit.
            if run["from"] == run["to"] and previous_state in ("success", "up-to-date"):
                self._log(run, "Already up to date, post-deploy commands skipped.")
                run["state"] = "up-to-date"
            else:
                for command in self.post_deploy:
                    self._exec(run, command, shell=True)
                run["state"] = "success"
        except DeployError as exc:
            self._log(run, f"ERROR: {exc}")
            run["state"] = "failed"
        except Exception as exc:  # keep the agent alive on unexpected errors
            self._log(run, f"UNEXPECTED ERROR: {exc!r}")
            run["state"] = "failed"
        finally:
            run["finished_at"] = now_iso()
            print(f"[{self.name}] deploy {run['state']} "
                  f"({run['from']} -> {run['to']}, trigger={trigger})", flush=True)

    # --- helpers ----------------------------------------------------------

    def _exec(self, run, command, cwd=None, shell=False):
        shown = command if shell else " ".join(command)
        self._log(run, f"$ {shown}")
        env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
        try:
            result = subprocess.run(
                command,
                cwd=str(cwd or self.directory),
                shell=shell,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=COMMAND_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired:
            raise DeployError(f"timeout after {COMMAND_TIMEOUT_SECONDS}s: {shown}")
        if result.stdout:
            self._log(run, result.stdout.rstrip())
        if result.returncode != 0:
            raise DeployError(f"exit code {result.returncode}: {shown}")

    @staticmethod
    def _log(run, text):
        run["log"] = (run["log"] + text + "\n")[-MAX_LOG_CHARS:]

    def _git_output(self, *args):
        if not (self.directory / ".git").is_dir():
            return None
        result = subprocess.run(
            ["git", *args], cwd=str(self.directory),
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
        return result.stdout.strip() if result.returncode == 0 else None

    def _head(self):
        return self._git_output("rev-parse", "HEAD")

    def _commit_info(self):
        output = self._git_output("log", "-1", "--format=%H%n%cI%n%s")
        if not output:
            return None
        sha, date, subject = (output.split("\n", 2) + ["", ""])[:3]
        return {"sha": sha, "date": date, "message": subject}


class Handler(BaseHTTPRequestHandler):
    server_version = "deploy-agent"

    # --- routing ----------------------------------------------------------

    def do_GET(self):
        parts = self._path_parts()
        if parts == ["widget.js"]:
            return self._send_widget()
        if parts == ["projects"]:
            if self._authorized():
                return self._json(200, {"projects": sorted(self.server.projects)})
            return None
        if len(parts) == 2 and parts[1] == "status":
            project = self._project(parts[0])
            if project and self._authorized():
                return self._json(200, project.status())
            return None
        return self._json(404, {"error": "not found"})

    def do_POST(self):
        parts = self._path_parts()
        if len(parts) != 2:
            return self._json(404, {"error": "not found"})
        project = self._project(parts[0])
        if not project:
            return None
        if parts[1] == "update":
            if self._authorized():
                return self._json(202, {"result": project.deploy("manual")})
            return None
        if parts[1] == "webhook":
            return self._handle_webhook(project)
        return self._json(404, {"error": "not found"})

    # --- webhook ----------------------------------------------------------

    def _handle_webhook(self, project):
        body = self._read_body()
        if body is None:
            return None
        if not project.webhook_secret:
            return self._json(403, {"error": "webhook_secret not configured"})
        expected = "sha256=" + hmac.new(
            project.webhook_secret.encode(), body, hashlib.sha256
        ).hexdigest()
        signature = self.headers.get("X-Hub-Signature-256", "")
        if not hmac.compare_digest(expected, signature):
            return self._json(401, {"error": "invalid signature"})

        event = self.headers.get("X-GitHub-Event", "")
        if event == "ping":
            return self._json(200, {"result": "pong"})
        if event != "push":
            return self._json(200, {"result": f"ignored event '{event}'"})
        try:
            ref = json.loads(body).get("ref", "")
        except ValueError:
            return self._json(400, {"error": "invalid JSON"})
        if ref != f"refs/heads/{project.branch}":
            return self._json(200, {"result": f"ignored ref '{ref}'"})
        if not project.auto_update:
            return self._json(200, {"result": "auto_update disabled"})
        return self._json(202, {"result": project.deploy("webhook")})

    # --- helpers ----------------------------------------------------------

    def _path_parts(self):
        return [p for p in urlsplit(self.path).path.split("/") if p]

    def _project(self, name):
        project = self.server.projects.get(name)
        if not project:
            self._json(404, {"error": f"unknown project '{name}'"})
        return project

    def _authorized(self):
        header = self.headers.get("Authorization", "")
        token = header[7:] if header.startswith("Bearer ") else ""
        if token and hmac.compare_digest(token.encode(), self.server.admin_token.encode()):
            return True
        self._json(401, {"error": "invalid or missing token"})
        return False

    def _read_body(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if length < 0 or length > MAX_BODY_BYTES:
            self._json(413, {"error": "invalid body size"})
            return None
        return self.rfile.read(length)

    def _send_widget(self):
        data = WIDGET_FILE.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "application/javascript; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, code, payload):
        data = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)


def load_config(path):
    with open(path, encoding="utf-8") as f:
        cfg = json.load(f)
    token = cfg.get("admin_token", "")
    if len(token) < 16 or token == "change-me":
        sys.exit("config error: admin_token must be set to a random value (>= 16 chars)")
    if not cfg.get("projects"):
        sys.exit("config error: no projects configured")
    return cfg


def main():
    cfg = load_config(CONFIG_PATH)
    server = ThreadingHTTPServer(
        (cfg.get("listen_host", "127.0.0.1"), int(cfg.get("listen_port", 9000))), Handler
    )
    server.admin_token = cfg["admin_token"]
    server.projects = {name: Project(name, p) for name, p in cfg["projects"].items()}
    host, port = server.server_address[:2]
    print(f"deploy-agent listening on {host}:{port}, "
          f"projects: {', '.join(server.projects)}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
