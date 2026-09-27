#!/usr/bin/env python3
"""Deploy agent: pulls the latest code from GitHub into website directories.

One agent serves any number of websites. Updates are triggered by the
embeddable update button (widget.js), the dashboard, or a GitHub push
webhook. Uses only the Python standard library and git.

Commands:
  deploy_agent.py serve                     run the HTTP service (default)
  deploy_agent.py add NAME REPO [options]   add a website to the config
  deploy_agent.py remove NAME               remove a website from the config
  deploy_agent.py list                      list configured websites
  deploy_agent.py update NAME               update a website from the shell
"""

import argparse
import hashlib
import hmac
import json
import os
import secrets
import shutil
import subprocess
import sys
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

CONFIG_PATH = os.environ.get("DEPLOY_AGENT_CONFIG", "/etc/deploy-agent/config.json")
BASE_DIR = Path(__file__).resolve().parent
WIDGET_FILE = BASE_DIR / "widget.js"
DASHBOARD_FILE = BASE_DIR / "dashboard.html"
BACKUP_DIR = Path(os.environ.get("DEPLOY_AGENT_BACKUP_DIR", "/var/lib/deploy-agent/backups"))
DEFAULT_WEB_ROOT = "/var/www"
HOOK_SCRIPT = ".deploy.sh"
MAX_BODY_BYTES = 5 * 1024 * 1024
COMMAND_TIMEOUT_SECONDS = 900
MAX_LOG_CHARS = 20000


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class DeployError(Exception):
    pass


class ConfigError(Exception):
    pass


# --- configuration -----------------------------------------------------------

def read_config(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        raise ConfigError(f"config file not found: {path}")
    except ValueError as exc:
        raise ConfigError(f"invalid JSON in {path}: {exc}")


def write_config(path, cfg):
    # Write in place so owner and permissions of the file are kept.
    with open(path, "r+", encoding="utf-8") as f:
        f.seek(0)
        json.dump(cfg, f, indent=2)
        f.write("\n")
        f.truncate()


def validate_config(cfg):
    token = cfg.get("admin_token", "")
    if len(token) < 16 or token == "change-me":
        raise ConfigError("admin_token must be set to a random value (>= 16 chars)")
    for name, project in cfg.get("projects", {}).items():
        if not project.get("repo"):
            raise ConfigError(f"project '{name}' has no repo")
    return cfg


# --- project / deploy logic --------------------------------------------------

class Project:
    def __init__(self, name, cfg):
        self.name = name
        self.last_run = None
        self._state_lock = threading.Lock()
        self._running = False
        self._pending = False
        self.configure(cfg)

    def configure(self, cfg):
        self.repo = cfg["repo"]
        self.branch = cfg.get("branch", "main")
        self.directory = Path(cfg.get("path") or f"{DEFAULT_WEB_ROOT}/{self.name}")
        self.domains = [d.lower() for d in cfg.get("domains", [])]
        self.webhook_secret = cfg.get("webhook_secret", "")
        self.auto_update = bool(cfg.get("auto_update", True))
        # None means: detect the build steps from the repository content.
        post_deploy = cfg.get("post_deploy")
        self.post_deploy = list(post_deploy) if post_deploy is not None else None

    def summary(self):
        return {"name": self.name, "domains": self.domains, "branch": self.branch}

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

    def deploy_blocking(self, trigger):
        with self._state_lock:
            self._running = True
        self._run_once(trigger)
        with self._state_lock:
            self._running = False
        return self.last_run

    def status(self):
        with self._state_lock:
            running, pending = self._running, self._pending
        return {
            **self.summary(),
            "auto_update": self.auto_update,
            "running": running,
            "pending": pending,
            "commit": self._commit_info(),
            "last_run": self.last_run,
        }

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
            run["from"] = self._checkout(run)
            run["to"] = self._head()

            # Skip build/restart only if nothing changed and the last run was fine,
            # so a failed build can be retried with the same commit.
            if run["from"] == run["to"] and previous_state in ("success", "up-to-date"):
                self._log(run, "Already up to date, post-deploy commands skipped.")
                run["state"] = "up-to-date"
            else:
                for command in self._post_deploy_commands(run):
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

    def _checkout(self, run):
        """Bring the directory to the latest commit. Returns the previous HEAD."""
        d = self.directory
        if (d / ".git").is_dir():
            previous = self._head()
            self._exec(run, ["git", "remote", "set-url", "origin", self.repo])
        elif d.is_dir() and any(d.iterdir()):
            # Existing website without git: back it up, then take it over.
            backup = BACKUP_DIR / f"{self.name}-{datetime.now():%Y%m%d-%H%M%S}"
            self._log(run, f"Existing files found, backup: {backup}")
            try:
                shutil.copytree(d, backup, symlinks=True)
            except OSError as exc:
                raise DeployError(f"backup failed: {exc}")
            self._exec(run, ["git", "init", "-q"])
            self._exec(run, ["git", "remote", "add", "origin", self.repo])
            previous = None
        else:
            d.parent.mkdir(parents=True, exist_ok=True)
            self._exec(run, ["git", "clone", "--branch", self.branch, "--single-branch",
                             self.repo, str(d)], cwd=d.parent)
            return None
        self._exec(run, ["git", "fetch", "--prune", "origin", self.branch])
        self._exec(run, ["git", "reset", "--hard", f"origin/{self.branch}"])
        return previous

    def _post_deploy_commands(self, run):
        if self.post_deploy is not None:
            return self.post_deploy
        d = self.directory
        if (d / HOOK_SCRIPT).is_file():
            self._log(run, f"Using {HOOK_SCRIPT} from the repository.")
            return [f"bash {HOOK_SCRIPT}"]

        commands = []
        if (d / "package.json").is_file():
            commands.append("npm ci" if (d / "package-lock.json").is_file() else "npm install")
            try:
                scripts = json.loads((d / "package.json").read_text()).get("scripts", {})
            except (OSError, ValueError):
                scripts = {}
            if "build" in scripts:
                commands.append("npm run build")
        if (d / "composer.json").is_file():
            commands.append("composer install --no-dev --optimize-autoloader --no-interaction")
        if (d / "requirements.txt").is_file() and (d / ".venv/bin/pip").is_file():
            commands.append(".venv/bin/pip install -r requirements.txt")
        self._log(run, "Detected build steps: " + (", ".join(commands) or "none (static site)"))
        return commands

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


class Registry:
    """Holds the projects and reloads the config file when it changes."""

    def __init__(self, path):
        self.path = path
        self.admin_token = ""
        self.projects = {}
        self._mtime = None
        self._lock = threading.Lock()
        self.refresh(strict=True)

    def refresh(self, strict=False):
        with self._lock:
            try:
                mtime = os.stat(self.path).st_mtime
                if mtime == self._mtime:
                    return
                cfg = validate_config(read_config(self.path))
            except (OSError, ConfigError) as exc:
                if strict:
                    raise ConfigError(str(exc))
                print(f"config reload failed, keeping old config: {exc}", flush=True)
                return
            projects = {}
            for name, project_cfg in cfg.get("projects", {}).items():
                project = self.projects.get(name)
                if project:
                    project.configure(project_cfg)
                else:
                    project = Project(name, project_cfg)
                projects[name] = project
            self.admin_token = cfg["admin_token"]
            self.projects = projects
            self.listen = (cfg.get("listen_host", "127.0.0.1"), int(cfg.get("listen_port", 9000)))
            self._mtime = mtime
            print(f"config loaded, projects: {', '.join(projects) or '-'}", flush=True)


# --- HTTP service ------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "deploy-agent"

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors_headers()
        self.send_header("Access-Control-Max-Age", "86400")
        self.end_headers()

    def do_GET(self):
        self.server.registry.refresh()
        parts = self._path_parts()
        if parts in ([], ["dashboard"]):
            return self._send_file(DASHBOARD_FILE, "text/html; charset=utf-8")
        if parts == ["widget.js"]:
            return self._send_file(WIDGET_FILE, "application/javascript; charset=utf-8")
        if parts == ["projects"]:
            if self._authorized():
                projects = self.server.registry.projects.values()
                return self._json(200, {"projects": [p.summary() for p in projects]})
            return None
        if len(parts) == 2 and parts[1] == "status":
            project = self._project(parts[0])
            if project and self._authorized():
                return self._json(200, project.status())
            return None
        return self._json(404, {"error": "not found"})

    def do_POST(self):
        self.server.registry.refresh()
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

    def _path_parts(self):
        return [p for p in urlsplit(self.path).path.split("/") if p]

    def _project(self, name):
        project = self.server.registry.projects.get(name)
        if not project:
            self._json(404, {"error": f"unknown project '{name}'"})
        return project

    def _authorized(self):
        header = self.headers.get("Authorization", "")
        token = header[7:] if header.startswith("Bearer ") else ""
        expected = self.server.registry.admin_token
        if token and hmac.compare_digest(token.encode(), expected.encode()):
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

    def _cors_headers(self):
        # Auth uses a bearer token (no cookies), so any website may call the API.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Authorization, Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")

    def _send_file(self, path, content_type):
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self._cors_headers()
        self.end_headers()
        self.wfile.write(data)

    def _json(self, code, payload):
        data = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self._cors_headers()
        self.end_headers()
        self.wfile.write(data)


# --- command line ------------------------------------------------------------

def cmd_serve(args):
    registry = Registry(args.config)
    server = ThreadingHTTPServer(registry.listen, Handler)
    server.registry = registry
    print(f"deploy-agent listening on {registry.listen[0]}:{registry.listen[1]}", flush=True)
    server.serve_forever()


def cmd_add(args):
    cfg = read_config(args.config)
    projects = cfg.setdefault("projects", {})
    if args.name in projects:
        sys.exit(f"project '{args.name}' already exists")
    project = {
        "repo": args.repo,
        "branch": args.branch,
        "path": args.path or f"{DEFAULT_WEB_ROOT}/{args.name}",
        "domains": args.domain,
        "auto_update": not args.no_auto,
        "webhook_secret": secrets.token_hex(32),
    }
    projects[args.name] = project
    write_config(args.config, cfg)

    base = cfg.get("public_url", "https://<your-deploy-domain>").rstrip("/")
    print(f"Added '{args.name}' -> {project['path']}\n")
    path = Path(project["path"])
    if path.is_dir() and any(path.iterdir()) and not (path / ".git").is_dir():
        print("Note: the folder already has files. The first update backs them up\n"
              f"      to {BACKUP_DIR}/{args.name}-<date> and then takes the folder over.\n")
    print("Embed the update button in the website:")
    print(f'  <script src="{base}/widget.js" defer></script>\n')
    print("GitHub webhook (repo Settings -> Webhooks -> Add webhook):")
    print(f"  Payload URL:  {base}/{args.name}/webhook")
    print("  Content type: application/json")
    print(f"  Secret:       {project['webhook_secret']}\n")
    print(f"Make sure the user 'deploy' can write to {project['path']}.")


def cmd_remove(args):
    cfg = read_config(args.config)
    if cfg.get("projects", {}).pop(args.name, None) is None:
        sys.exit(f"unknown project '{args.name}'")
    write_config(args.config, cfg)
    print(f"Removed '{args.name}' (files on disk are kept).")


def cmd_list(args):
    cfg = read_config(args.config)
    for name, p in cfg.get("projects", {}).items():
        domains = ", ".join(p.get("domains", [])) or "-"
        path = p.get("path") or f"{DEFAULT_WEB_ROOT}/{name}"
        print(f"{name:20} {path:30} {domains:30} {p['repo']}")


def cmd_update(args):
    cfg = read_config(args.config)
    if args.name not in cfg.get("projects", {}):
        sys.exit(f"unknown project '{args.name}'")
    run = Project(args.name, cfg["projects"][args.name]).deploy_blocking("cli")
    print(run["log"], end="")
    print(f"=> {run['state']}")
    sys.exit(0 if run["state"] != "failed" else 1)


def main():
    parser = argparse.ArgumentParser(description="Deploy websites from GitHub.")
    parser.add_argument("--config", default=CONFIG_PATH)
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("serve", help="run the HTTP service")

    add = sub.add_parser("add", help="add a website")
    add.add_argument("name", help="short name, e.g. my-site")
    add.add_argument("repo", help="GitHub URL, e.g. https://github.com/user/repo.git")
    add.add_argument("--path", help=f"target folder (default: {DEFAULT_WEB_ROOT}/NAME)")
    add.add_argument("--branch", default="main")
    add.add_argument("--domain", action="append", default=[],
                     help="domain of the website (repeatable), used by the button")
    add.add_argument("--no-auto", action="store_true", help="no auto update on push")

    remove = sub.add_parser("remove", help="remove a website")
    remove.add_argument("name")

    sub.add_parser("list", help="list websites")

    update = sub.add_parser("update", help="update a website now")
    update.add_argument("name")

    args = parser.parse_args()
    handlers = {"add": cmd_add, "remove": cmd_remove, "list": cmd_list, "update": cmd_update}
    try:
        handlers.get(args.command, cmd_serve)(args)
    except ConfigError as exc:
        sys.exit(f"config error: {exc}")


if __name__ == "__main__":
    main()
