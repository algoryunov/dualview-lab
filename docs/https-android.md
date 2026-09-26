# Local HTTPS and phone access

Mobile browsers require a secure context for camera access. Use a LAN hostname or IP covered by the development certificate, and set `DUALVIEW_PUBLIC_ORIGIN` to that same HTTPS origin.

1. Run `mkcert -install` on the host.
2. Create the server certificate as shown in the README.
3. Locate the CA directory with `mkcert -CAROOT`.
4. Transfer only `rootCA.pem` to your Android phone and install it as a CA certificate in system security settings. Never transfer the CA private key or the server private key.
5. Connect both devices to the same Wi-Fi network, open the HTTPS address in Chrome, and confirm it loads without certificate errors.
6. Open **Connect phone** on the dashboard and follow the link on the phone.

Localhost on a phone refers to the phone itself. A pairing link must use the host's reachable LAN address. Guest Wi-Fi and client isolation can block WebRTC even when the page loads. No TURN relay is configured.

Use `DUALVIEW_SHOW_QR_CODE=false` to hide the QR while retaining the link. Pairing authorization is controlled independently by `DUALVIEW_PAIRING_REQUIRED`.
