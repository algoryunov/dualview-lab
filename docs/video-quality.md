# Video quality

The phone defaults to 1280 × 720 at a preferred 15 fps. The Capture quality selector also offers 1920 × 1080. Camera and quality selections are remembered in that browser. If the camera rejects the minimum HD constraint, capture falls back to a preferred 1280 × 720 request. The actual camera mode is shown below the local preview. Full HD requires more encoding power and bandwidth and does not eliminate sensor noise. The sender caps VP8 at 6 Mbit/s and 15 fps and prefers retaining resolution under load. A bitrate cap is not guaranteed throughput; congestion control can still reduce the sending rate. See the [sender parameters](https://developer.mozilla.org/en-US/docs/Web/API/RTCRtpSender/setParameters).

The backend decodes the original phone frames for tracking and calibration. The input pipeline only drops complete decoded frames, never arbitrary RTP fragments before VP8 depayloading. Dashboard previews are encoded again at the original frame dimensions, using the VP8 realtime deadline with `cpu-used=4`, two encoding threads, and no lookahead. The preview target is 6 Mbit/s per camera, configurable with `DUALVIEW_PREVIEW_BITRATE` (500000–20000000 bits/s). These are lossy previews, not lossless copies. See [GStreamer VP8 controls](https://gstreamer.freedesktop.org/documentation/vpx/vp8enc.html).

The phone shows both **Capture** and **Sending** resolution, plus the actual outbound frame rate, bitrate, and browser quality-limitation reason. Setup shows the dimensions received by the server and decoded by the dashboard. Compare these values to locate downscaling; they do not measure perceptual quality.

VP8 negotiation retains RTX, and the server enables NACK retransmission on its WebRTC transceivers. When the phone depayloader detects packet loss, it requests a new keyframe and waits for that frame before resuming decoding. This limits corruption from missing reference frames; it cannot remove camera sensor noise or guarantee recovery during sustained congestion. See [GStreamer VP8 depayloading](https://gstreamer.freedesktop.org/documentation/rtp/rtpvp8depay.html).

- If the local phone preview is already noisy, compare the rear camera and brighter lighting. Transport bitrate cannot recover detail absent from the camera input.
- If the local preview is clear but Sending is smaller or reports a bandwidth/CPU limitation, investigate the phone's encoding or LAN connection.
- If Sending and server dimensions match but the dashboard is worse, the second encoder or dashboard connection is the next place to investigate. Increasing `DUALVIEW_PREVIEW_BITRATE` consumes more bandwidth; restart the server after changing it.

The browser regression checks synthetic HD capture, actual outbound resolution, and matching returned preview dimensions, as well as decoded pixels. It does not certify image quality on a physical phone or a congested Wi-Fi network.
