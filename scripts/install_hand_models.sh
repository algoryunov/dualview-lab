#!/usr/bin/env bash
set -euo pipefail

# Apache-2.0 model assets from the official OpenCV Zoo repository. They stay
# local and Git-ignored; the SHA-256 checks make the selected revision explicit.
readonly ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly MODELS_DIR="${ROOT_DIR}/models/onnx"

readonly PALM_FILE="palm_detection_mediapipe_2023feb.onnx"
readonly PALM_SHA256="78ff51c38496b7fc8b8ebdb6cc8c1abb02fa6c38427c6848254cdaba57fcce7c"
readonly PALM_URL="https://github.com/opencv/opencv_zoo/raw/main/models/palm_detection_mediapipe/${PALM_FILE}"
readonly HAND_FILE="handpose_estimation_mediapipe_2023feb.onnx"
readonly HAND_SHA256="db0898ae717b76b075d9bf563af315b29562e11f8df5027a1ef07b02bef6d81c"
readonly HAND_URL="https://github.com/opencv/opencv_zoo/raw/main/models/handpose_estimation_mediapipe/${HAND_FILE}"

mkdir -p "${MODELS_DIR}"
install_model() {
  local file_name="$1"
  local expected_sha="$2"
  local source_url="$3"
  local target_path="${MODELS_DIR}/${file_name}"
  local temporary_path
  temporary_path="$(mktemp -t dualview-hand-model.XXXXXX.onnx)"
  trap 'rm -f "${temporary_path}"' RETURN
  if [[ -f "${target_path}" ]] && echo "${expected_sha}  ${target_path}" | shasum -a 256 -c -; then
    echo "Verified ${target_path}"
    return
  fi
  curl --fail --location --proto '=https' --tlsv1.2 --output "${temporary_path}" "${source_url}"
  echo "${expected_sha}  ${temporary_path}" | shasum -a 256 -c -
  mv "${temporary_path}" "${target_path}"
  trap - RETURN
  echo "Installed ${target_path}"
}

install_model "${PALM_FILE}" "${PALM_SHA256}" "${PALM_URL}"
install_model "${HAND_FILE}" "${HAND_SHA256}" "${HAND_URL}"
