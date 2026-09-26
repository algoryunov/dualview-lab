# syntax=docker/dockerfile:1
FROM node:24-trixie-slim AS frontend
WORKDIR /src/frontend
COPY frontend/package*.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build \
    && mkdir -p /notices/npm \
    && find node_modules -type f \( -iname "LICENSE*" -o -iname "NOTICE*" -o -iname "COPYING*" \) -exec cp --parents {} /notices/npm \;

FROM debian:trixie-slim AS native
ARG TARGETARCH
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl perl cmake ninja-build g++ pkg-config \
    libopencv-dev nlohmann-json3-dev libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev libsoup-3.0-dev \
    && rm -rf /var/lib/apt/lists/*
COPY scripts/install_onnxruntime_linux.sh /tmp/install-ort.sh
RUN bash /tmp/install-ort.sh "$TARGETARCH" /opt/onnxruntime
WORKDIR /src
COPY scripts/install_hand_models.sh scripts/install_hand_models.sh
RUN bash scripts/install_hand_models.sh
COPY backend/ backend/
RUN cmake -S backend -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DDUALVIEW_ONNXRUNTIME_ROOT=/opt/onnxruntime \
    && cmake --build /build --parallel 2 \
    && ctest --test-dir /build --output-on-failure

FROM debian:trixie-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libopencv-contrib410 libopencv-videoio410 \
    libsoup-3.0-0 gstreamer1.0-tools gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-nice \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 dualview \
    && useradd --uid 10001 --gid dualview --no-create-home dualview \
    && mkdir -p /data/calibration /data/logs /app/certs \
    && chown -R dualview:dualview /data
WORKDIR /app
COPY --from=native /opt/onnxruntime/lib/ /opt/onnxruntime/lib/
RUN echo /opt/onnxruntime/lib > /etc/ld.so.conf.d/onnxruntime.conf && ldconfig
COPY --from=native /build/dualview_server /usr/local/bin/dualview_server
COPY --from=native /build/dualview_charuco /usr/local/bin/dualview_charuco
COPY --from=native /src/models/onnx/ /app/models/onnx/
COPY --from=native /opt/onnxruntime/ThirdPartyNotices.txt /app/notices/onnxruntime.txt
COPY --from=native /opt/onnxruntime/LICENSE /app/notices/onnxruntime-LICENSE
COPY --from=native /usr/share/common-licenses/Apache-2.0 /app/notices/models-Apache-2.0
COPY --from=frontend /notices/npm/ /app/notices/npm/
COPY --from=frontend /src/frontend/dist/ /app/frontend/dist/
COPY THIRD_PARTY_NOTICES.md /app/THIRD_PARTY_NOTICES.md
COPY docker/healthcheck.sh /usr/local/bin/dualview-healthcheck
RUN chmod a+r /app/models/onnx/*.onnx \
    && chmod +x /usr/local/bin/dualview-healthcheck \
    && for element in webrtcbin vp8enc vp8dec nicesrc dtlsenc srtpenc sctpenc; do gst-inspect-1.0 "$element" >/dev/null; done
ENV DUALVIEW_HOST=0.0.0.0 DUALVIEW_PORT=8443 \
    DUALVIEW_TLS=false DUALVIEW_PUBLIC_ORIGIN=http://localhost:8443 \
    DUALVIEW_INFERENCE_PROVIDER=cpu DUALVIEW_CAMERA_ENABLED=false \
    DUALVIEW_MODELS_DIR=/app/models/onnx DUALVIEW_FRONTEND_DIR=/app/frontend/dist \
    DUALVIEW_CALIBRATION_FILE=/data/calibration/native.yml \
    DUALVIEW_TRACKING_LOG=- \
    DUALVIEW_METAL_LOG=/data/logs/dualview-metal.jsonl \
    XDG_CACHE_HOME=/tmp/dualview-cache
USER dualview
EXPOSE 8443
HEALTHCHECK --interval=15s --timeout=3s --start-period=30s --retries=3 CMD ["dualview-healthcheck"]
ENTRYPOINT ["dualview_server"]
