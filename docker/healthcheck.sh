#!/bin/sh
set -eu
scheme=http
case "${DUALVIEW_TLS:-false}" in true|1|yes|on) scheme=https ;; esac
# Local liveness only; LAN clients must still trust the configured certificate.
exec curl --fail --silent --insecure --max-time 2 "$scheme://127.0.0.1:${DUALVIEW_PORT:-8443}/api/health"
