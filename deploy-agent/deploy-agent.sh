#!/bin/sh
# Command line wrapper, installed as /usr/local/bin/deploy-agent.
APP=/opt/deploy-agent/deploy_agent.py
if [ "$1" = "update" ] && [ "$(id -u)" = "0" ]; then
  # Run updates as the deploy user so file ownership stays consistent.
  exec runuser -u deploy -- python3 "$APP" "$@"
fi
exec python3 "$APP" "$@"
