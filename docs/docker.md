# Docker

The multi-stage image compiles the C++ server and React frontend, runs native tests, and ships a non-root Linux runtime with ONNX Runtime CPU and hand models. No local `.env`, certificates, calibration, build directories, or downloaded native dependencies enter the build context. Downloads of ONNX Runtime and model weights are checksum-verified. Both Linux arm64 and amd64 are supported by the installer.

## Linux with a camera

Create `.env` from `.env.example` if needed, set `DUALVIEW_PUBLIC_ORIGIN` to the host's LAN HTTPS address, and create trusted certificates as described in the README. Place the certificate and key at `certs/dualview-lab.pem` and `certs/dualview-lab-key.pem`; the container user must have read access to them. Keep the private key out of the image and source control.

```bash
export DUALVIEW_VIDEO_DEVICE=/dev/video0
export DUALVIEW_VIDEO_GID="$(stat -c '%g' "$DUALVIEW_VIDEO_DEVICE")"
docker compose -f compose.yaml -f compose.camera.yaml up --build
```

The selected device is mapped to `/dev/video0` inside the container. The additional group grants camera access without privileged mode. Open the configured HTTPS origin and connect the phone normally. Camera compatibility depends on the host's V4L2 driver.

Compose uses **host networking** because WebRTC advertises dynamically allocated UDP endpoints. Publishing only the HTTPS TCP port does not make the media connection reachable. Native Linux on a LAN that allows peer traffic is the intended full two-camera deployment. There is no TURN/NAT traversal configuration in this project.

## CPU server without a local camera

```bash
make docker-up
# Stop it with Ctrl+C, or from another terminal:
make docker-down
```

The base Compose service deliberately disables local camera capture and selects CPU regardless of the native macOS provider in `.env`. This supports server/UI checks and Auto demo. Real two-view interaction still needs a local camera and a calibrated pair. Other settings, including QR visibility, pairing, TLS, and inference cadence, come from `.env`.

The image includes the built frontend and model weights. Rebuild after source changes. Calibration is stored in the separate Compose `calibration` volume and survives rebuilds and `docker compose down`; `down --volumes` deletes it. This volume starts empty, so the native macOS calibration file is not imported automatically. Certificates are mounted read-only.

## macOS and Docker Desktop

A Linux container cannot use the macOS Core ML provider or automatically access the built-in Mac camera. Use `make dev` for the full local Apple camera/Core ML experience.

[Docker Desktop host networking](https://docs.docker.com/engine/network/drivers/host/) requires Desktop 4.34+ and explicit enablement in Settings. It operates at TCP/UDP level; this is not equivalent to native Linux interface access, so do not assume container ICE addresses are reachable from a LAN phone. Full WebRTC on Desktop is not a supported deployment here.

For a local UI/health check without host networking or certificates:

```bash
make docker-build
docker run --rm --init -p 127.0.0.1:8443:8443 dualview-lab:local
```

Open `http://localhost:8443`. This bridge-network example is for UI/Auto demo and health checks, not a working two-camera WebRTC setup.

## Verification

`docker build` runs the Linux native tests, including real CPU model inference, and checks that the required WebRTC media plugins are installed. To test the built container's WebRTC flow on native Linux:

```bash
npm --prefix frontend ci
cd frontend && npx playwright install chromium && cd ..
./scripts/test-container.sh
```

The script tests QR enabled/disabled, synthetic phone video, returned pixels, commands, reconnect, and pairing security, then removes its test containers. It does not access a real camera or the persistent Compose calibration volume. GitHub Actions includes this Linux container check.

The runtime is based on Debian 13, whose [OpenCV package](https://packages.debian.org/trixie/libopencv-dev) meets the project's OpenCV 4.8+ requirement. The build also requires GStreamer 1.24+.

Local verification: the Linux arm64 image built successfully on Docker Desktop, all seven Linux native tests passed during the build, and the final non-root container loaded CPU models, served the frontend/configuration, and passed its HTTP healthcheck. Both Compose configurations passed validation. Linux amd64 and the container WebRTC browser job still need their first GitHub Actions run; physical Linux camera access has not been tested locally.
