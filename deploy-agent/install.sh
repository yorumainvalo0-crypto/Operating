#!/usr/bin/env bash
# Installs the deploy agent as a systemd service (Debian/Ubuntu, run as root).
set -euo pipefail

INSTALL_DIR=/opt/deploy-agent
CONFIG_DIR=/etc/deploy-agent
CONFIG_FILE="$CONFIG_DIR/config.json"
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ $EUID -ne 0 ]]; then
  echo "Please run as root: sudo $0" >&2
  exit 1
fi

apt-get update -qq
apt-get install -y -qq git python3 >/dev/null

if ! id deploy >/dev/null 2>&1; then
  useradd --system --create-home --home-dir /var/lib/deploy-agent --shell /usr/sbin/nologin deploy
fi

install -d -m 755 "$INSTALL_DIR"
install -m 755 "$SRC_DIR/deploy_agent.py" "$INSTALL_DIR/deploy_agent.py"
install -m 644 "$SRC_DIR/widget.js" "$INSTALL_DIR/widget.js"

install -d -m 750 -o root -g deploy "$CONFIG_DIR"
if [[ ! -f "$CONFIG_FILE" ]]; then
  install -m 640 -o root -g deploy "$SRC_DIR/config.example.json" "$CONFIG_FILE"
  # Replace placeholder secrets with random values.
  python3 - "$CONFIG_FILE" <<'PY'
import json, secrets, sys
path = sys.argv[1]
with open(path) as f:
    cfg = json.load(f)
cfg["admin_token"] = secrets.token_urlsafe(32)
for project in cfg["projects"].values():
    project["webhook_secret"] = secrets.token_hex(32)
with open(path, "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PY
  echo "Created $CONFIG_FILE with random secrets - edit the projects section."
fi

install -m 644 "$SRC_DIR/deploy-agent.service" /etc/systemd/system/deploy-agent.service
systemctl daemon-reload
systemctl enable deploy-agent >/dev/null
systemctl restart deploy-agent

echo
echo "Deploy agent installed and running."
echo "Admin token: $(python3 -c "import json;print(json.load(open('$CONFIG_FILE'))['admin_token'])")"
echo "Next steps: edit $CONFIG_FILE, then: systemctl restart deploy-agent"
