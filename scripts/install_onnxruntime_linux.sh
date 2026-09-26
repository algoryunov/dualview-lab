#!/usr/bin/env bash
set -euo pipefail
readonly VERSION=1.30.0
case "${1:-$(uname -m)}" in
  amd64|x86_64) architecture=x64; checksum=a5ed5a3cac51fbb2e90da632ae43d19212faaa20e76484e62bcb7c23ddb3b3fd ;;
  arm64|aarch64) architecture=aarch64; checksum=e16a27a8ed330bbc698df7330b0cf56e722f354e3bcc92118682c74ef3c3e3da ;;
  *) echo 'Supported Linux architectures: amd64 and arm64' >&2; exit 1 ;;
esac
readonly destination="${2:-/opt/onnxruntime}"
readonly archive="$(mktemp)"
trap 'rm -f "$archive"' EXIT
curl --fail --location --retry 3 --proto '=https' --tlsv1.2 \
  "https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/onnxruntime-linux-${architecture}-${VERSION}.tgz" -o "$archive"
printf '%s  %s\n' "$checksum" "$archive" | sha256sum -c -
mkdir -p "$destination"
tar -xzf "$archive" --strip-components=1 -C "$destination"
