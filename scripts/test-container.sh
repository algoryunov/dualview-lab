#!/usr/bin/env bash
# Run on native Linux: the browser and container share routable host ICE addresses.
set -euo pipefail
readonly image="${DUALVIEW_TEST_IMAGE:-dualview-lab:local}"
readonly port="${DUALVIEW_TEST_PORT:-18447}"
readonly origin="http://localhost:${port}"
container_id=''
cleanup() {
  if [[ -n "$container_id" ]]; then docker rm -f "$container_id" >/dev/null; fi
}
trap cleanup EXIT
for qr in true false; do
  container_id="$(docker run -d --network host \
    -e DUALVIEW_PORT="$port" -e DUALVIEW_PUBLIC_ORIGIN="$origin" \
    -e DUALVIEW_TLS=false -e DUALVIEW_CAMERA_ENABLED=false \
    -e DUALVIEW_PAIRING_REQUIRED=true -e DUALVIEW_SHOW_QR_CODE="$qr" "$image")"
  ready=false
  for ((attempt=0; attempt<60; attempt++)); do
    if curl -fsS "$origin/api/health" >/dev/null 2>&1; then ready=true; break; fi
    sleep .5
  done
  if [[ "$ready" != true ]]; then docker logs "$container_id"; exit 1; fi
  TEST_ORIGIN="$origin" EXPECT_QR="$qr" EXPECT_PROVIDER=cpu node frontend/tests/browser-smoke.mjs
  TEST_ORIGIN="$origin" node frontend/tests/pairing-security.mjs
  cleanup
  container_id=''
done
