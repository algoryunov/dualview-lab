#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -f .env ]]; then
  set -a
  source .env
  set +a
fi
if [[ ! -x backend/build/dualview_server || ! -f "${DUALVIEW_FRONTEND_DIR:-frontend/dist}/index.html" ]]; then
  echo "Application build is missing. Run make build before make dev." >&2
  exit 1
fi
source scripts/gstreamer-env.sh
exec backend/build/dualview_server
