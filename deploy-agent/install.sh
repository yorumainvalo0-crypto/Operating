#!/usr/bin/env bash
# Installs the deploy agent as a systemd service (Debian/Ubuntu, run as root).
#
#   sudo ./install.sh                      agent only
#   sudo ./install.sh deploy.example.com   agent + nginx site (+ HTTPS via certbot)
set -euo pipefail

DOMAIN="${1:-}"
INSTALL_DIR=/opt/deploy-agent
CONFIG_DIR=/etc/deploy-agent
CONFIG_FILE="$CONFIG_DIR/config.json"
DATA_DIR=/var/lib/deploy-agent
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ $EUID -ne 0 ]]; then
  echo "Please run as root: sudo $0 $*" >&2
  exit 1
fi

apt-get update -qq
apt-get install -y -qq git python3 >/dev/null

if ! id deploy >/dev/null 2>&1; then
  useradd --system --create-home --home-dir "$DATA_DIR" --shell /usr/sbin/nologin deploy
fi
install -d -m 750 -o deploy -g deploy "$DATA_DIR" "$DATA_DIR/backups"

install -d -m 755 "$INSTALL_DIR"
install -m 755 "$SRC_DIR/deploy_agent.py" "$INSTALL_DIR/deploy_agent.py"
install -m 644 "$SRC_DIR/widget.js" "$SRC_DIR/dashboard.html" "$INSTALL_DIR/"
install -m 755 "$SRC_DIR/deploy-agent.sh" /usr/local/bin/deploy-agent

install -d -m 750 -o root -g deploy "$CONFIG_DIR"
if [[ ! -f "$CONFIG_FILE" ]]; then
  install -m 640 -o root -g deploy "$SRC_DIR/config.example.json" "$CONFIG_FILE"
  python3 - "$CONFIG_FILE" "$DOMAIN" <<'PY'
import json, secrets, sys
path, domain = sys.argv[1], sys.argv[2]
with open(path) as f:
    cfg = json.load(f)
cfg["admin_token"] = secrets.token_urlsafe(32)
if domain:
    cfg["public_url"] = f"https://{domain}"
with open(path, "r+") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
    f.truncate()
PY
fi

install -m 644 "$SRC_DIR/deploy-agent.service" /etc/systemd/system/deploy-agent.service
systemctl daemon-reload
systemctl enable deploy-agent >/dev/null
systemctl restart deploy-agent

if [[ -n "$DOMAIN" ]]; then
  if command -v nginx >/dev/null; then
    sed "s/deploy.example.com/$DOMAIN/" "$SRC_DIR/nginx.example.conf" \
      > /etc/nginx/sites-available/deploy-agent
    ln -sf /etc/nginx/sites-available/deploy-agent /etc/nginx/sites-enabled/deploy-agent
    nginx -t && systemctl reload nginx
    if command -v certbot >/dev/null; then
      certbot --nginx -d "$DOMAIN" --non-interactive --agree-tos --register-unsafely-without-email \
        || echo "certbot failed - run it manually: certbot --nginx -d $DOMAIN"
    else
      echo "For HTTPS: apt install certbot python3-certbot-nginx && certbot --nginx -d $DOMAIN"
    fi
  else
    echo "nginx not found - configure your web server with nginx.example.conf as template."
  fi
fi

TOKEN=$(python3 -c "import json;print(json.load(open('$CONFIG_FILE'))['admin_token'])")
URL=$(python3 -c "import json;print(json.load(open('$CONFIG_FILE')).get('public_url',''))")
cat <<EOF

Deploy agent installed and running.

  Admin token: $TOKEN
  Dashboard:   $URL/

Add a website:
  sudo deploy-agent add my-site https://github.com/USER/REPO.git --domain my-site.com

EOF
