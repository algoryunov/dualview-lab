#!/usr/bin/env bash
# Source from the repository root. Limit discovery to the native media pipeline.
set -euo pipefail
DUALVIEW_GST_LIBDIR="$(pkg-config --variable=pluginsdir gstreamer-1.0)"
DUALVIEW_GST_PLUGINS="$PWD/backend/build/gst-plugins"
mkdir -p "$DUALVIEW_GST_PLUGINS"
for plugin in coreelements app typefindfunctions playback videoconvertscale vpx rtp rtpmanager webrtc nice dtls srtp sctp; do
  extension=so
  if [[ "$(uname -s)" == Darwin ]]; then extension=dylib; fi
  library="$DUALVIEW_GST_LIBDIR/libgst${plugin}.${extension}"
  if [[ ! -f "$library" ]]; then echo "Missing required GStreamer plugin: $library" >&2; return 1; fi
  ln -sf "$library" "$DUALVIEW_GST_PLUGINS/libgst${plugin}.${extension}"
done
export GST_PLUGIN_SYSTEM_PATH=''
export GST_PLUGIN_PATH="$DUALVIEW_GST_PLUGINS"
export GST_REGISTRY="$PWD/backend/build/gst-registry.bin"
