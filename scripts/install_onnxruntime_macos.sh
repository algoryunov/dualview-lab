#!/usr/bin/env bash
set -euo pipefail

# Official macOS arm64 release package, intentionally local and Git-ignored.
# This binary must expose CoreMLExecutionProvider; CTest verifies that claim.
readonly ORT_VERSION="1.30.0"
readonly ORT_ARCHIVE="onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
readonly ORT_SHA256="6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012"
readonly ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_ARCHIVE}"

readonly ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DESTINATION="${ROOT_DIR}/backend/deps/onnxruntime-${ORT_VERSION}"
readonly ARCHIVE_PATH="$(mktemp -t dualview-onnxruntime.XXXXXX.tgz)"
trap 'rm -f "${ARCHIVE_PATH}"' EXIT

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
  echo "This installer is only for native macOS arm64 development." >&2
  exit 1
fi

if [[ -d "${DESTINATION}/lib/cmake/onnxruntime" ]]; then
  echo "ONNX Runtime ${ORT_VERSION} is already installed at ${DESTINATION}"
  exit 0
fi
if [[ -e "${DESTINATION}" ]]; then
  echo "${DESTINATION} exists but is not a valid ONNX Runtime ${ORT_VERSION} installation." >&2
  exit 1
fi

curl --fail --location --proto '=https' --tlsv1.2 --output "${ARCHIVE_PATH}" "${ORT_URL}"
if [[ -z "${ORT_SHA256}" ]]; then
  echo "Installer checksum has not been pinned yet; refusing to install." >&2
  exit 1
fi
echo "${ORT_SHA256}  ${ARCHIVE_PATH}" | shasum -a 256 -c -

mkdir -p "${DESTINATION}"
tar -xzf "${ARCHIVE_PATH}" --strip-components=2 -C "${DESTINATION}"
echo "Installed ONNX Runtime ${ORT_VERSION} at ${DESTINATION}"
