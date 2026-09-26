#include <glib-unix.h>
#include <libsoup/soup.h>

#include <fstream>
#include <iostream>

#include "dualview/transport.hpp"
#include "peer.hpp"
namespace dualview {
namespace {
using transport::Peer;
struct Server {
  const Config& config;
  Runtime& runtime;
  std::vector<std::shared_ptr<Peer>> peers;
  std::string token;
  Clock::time_point expires{};
  bool valid(const std::string& supplied) const {
    return !config.pairing_required ||
           (!token.empty() && token == supplied && Clock::now() < expires);
  }
};
bool trusted_origin(SoupServerMessage* message, const Config& config) {
  const char* origin =
      soup_message_headers_get_one(soup_server_message_get_request_headers(message), "Origin");
  if (!origin) return true;
  if (origin == config.public_origin) return true;
  auto* uri = soup_server_message_get_uri(message);
  auto* expected = g_uri_join(G_URI_FLAGS_NONE, g_uri_get_scheme(uri), nullptr, g_uri_get_host(uri),
                              g_uri_get_port(uri), "", nullptr, nullptr);
  const bool valid = std::string(origin) == expected;
  g_free(expected);
  return valid;
}
void response(SoupServerMessage* message, int status, const Json& json) {
  const auto body = json.dump();
  soup_server_message_set_status(message, status, nullptr);
  soup_server_message_set_response(message, "application/json", SOUP_MEMORY_COPY, body.data(),
                                   body.size());
}
}  // namespace
int serve(const Config& config, Runtime& runtime) {
  Server state{config, runtime, {}, "", {}};
  auto* server = soup_server_new("server-header", "DualView", nullptr);
  GError* error = nullptr;
  if (config.tls) {
    auto* certificate =
        g_tls_certificate_new_from_files(config.certificate.c_str(), config.key.c_str(), &error);
    if (!certificate) {
      std::string message = error->message;
      g_error_free(error);
      g_object_unref(server);
      throw std::runtime_error(message);
    }
    soup_server_set_tls_certificate(server, certificate);
    g_object_unref(certificate);
  }
  soup_server_add_handler(
      server, nullptr,
      +[](SoupServer*, SoupServerMessage* message, const char* path, GHashTable*, gpointer ptr) {
        auto& state = *static_cast<Server*>(ptr);
        const auto method = std::string(soup_server_message_get_method(message));
        soup_message_headers_replace(soup_server_message_get_response_headers(message),
                                     "Cache-Control", "no-store");
        try {
          if (method != "GET" && !trusted_origin(message, state.config)) {
            response(message, 403, {{"error", "Untrusted origin"}});
            return;
          }
          if (std::string(path) == "/api/health" && method == "GET") {
            response(message, 200, {{"status", "ok"}, {"backend", "cpp"}, {"transport", "webrtc"}});
            return;
          }
          if (std::string(path) == "/api/config" && method == "GET") {
            response(message, 200,
                     {{"show_qr_code", state.config.show_qr},
                      {"pairing_required", state.config.pairing_required},
                      {"public_origin", state.config.public_origin}});
            return;
          }
          if (std::string(path) == "/api/session" && method == "POST") {
            if (state.config.pairing_required) {
              gchar* first = g_uuid_string_random();
              gchar* second = g_uuid_string_random();
              state.token = std::string(first) + second;
              g_free(first);
              g_free(second);
              state.expires = Clock::now() + std::chrono::minutes(5);
            }
            response(message, 200,
                     {{"phone_url",
                       state.config.public_origin + "/phone" +
                           (state.config.pairing_required ? "?session=" + state.token : "")},
                      {"expires_in_seconds", state.config.pairing_required ? 300 : 0}});
            return;
          }
          if (std::string(path).starts_with("/api/") || method != "GET") {
            response(message, 404, {{"error", "Not found"}});
            return;
          }
          const auto root = std::filesystem::weakly_canonical(state.config.frontend);
          auto relative = std::string(path) == "/" || std::string(path) == "/phone"
                              ? "index.html"
                              : std::string(path).substr(1);
          auto file = std::filesystem::weakly_canonical(root / relative);
          auto within = file.lexically_relative(root);
          if (within.empty() || *within.begin() == ".." ||
              !std::filesystem::is_regular_file(file)) {
            response(message, 404, {{"error", "Not found"}});
            return;
          }
          std::ifstream stream(file, std::ios::binary);
          std::string contents((std::istreambuf_iterator<char>(stream)), {});
          auto extension = file.extension().string();
          const char* type = extension == ".html"  ? "text/html"
                             : extension == ".js"  ? "text/javascript"
                             : extension == ".css" ? "text/css"
                             : extension == ".svg" ? "image/svg+xml"
                             : extension == ".glb" ? "model/gltf-binary"
                                                   : "application/octet-stream";
          soup_server_message_set_status(message, 200, nullptr);
          soup_server_message_set_response(message, type, SOUP_MEMORY_COPY, contents.data(),
                                           contents.size());
        } catch (const std::exception& e) {
          response(message, 500, {{"error", e.what()}});
        }
      },
      &state, nullptr);
  soup_server_add_websocket_handler(
      server, "/ws/rtc", nullptr, nullptr,
      +[](SoupServer*, SoupServerMessage* message, const char*, SoupWebsocketConnection* connection,
          gpointer ptr) {
        auto& state = *static_cast<Server*>(ptr);
        auto* uri = soup_server_message_get_uri(message);
        const char* raw = g_uri_get_query(uri);
        GHashTable* query = raw ? soup_form_decode(raw) : nullptr;
        auto parameter = [&](const char* key) {
          const auto* value =
              query ? static_cast<const char*>(g_hash_table_lookup(query, key)) : nullptr;
          return std::string(value ? value : "");
        };
        const auto role = parameter("role");
        bool phone = role == "phone";
        bool valid = (role == "dashboard" || phone) && trusted_origin(message, state.config) &&
                     (!phone || state.valid(parameter("session")));
        if (query) g_hash_table_unref(query);
        if (!valid || state.peers.size() >= 8) {
          soup_websocket_connection_close(connection, 1008,
                                          "Invalid pairing session or peer limit");
          return;
        }
        // Fully stop an old camera before the replacement can deliver any frames.
        if (phone)
          std::erase_if(state.peers, [](const auto& peer) {
            if (!peer->is_phone()) return false;
            peer->close();
            return true;
          });
        auto peer =
            std::make_shared<Peer>(state.runtime, connection, phone, state.config.preview_bitrate);
        state.peers.push_back(std::move(peer));
      },
      &state, nullptr);
  auto* address = g_inet_address_new_from_string(config.host.c_str());
  if (!address) {
    g_object_unref(server);
    throw std::runtime_error("DUALVIEW_HOST must be an IP address");
  }
  auto* socket_address = g_inet_socket_address_new(address, config.port);
  g_object_unref(address);
  bool listening = soup_server_listen(
      server, socket_address,
      config.tls ? SOUP_SERVER_LISTEN_HTTPS : static_cast<SoupServerListenOptions>(0), &error);
  g_object_unref(socket_address);
  if (!listening) {
    std::string message = error->message;
    g_error_free(error);
    g_object_unref(server);
    throw std::runtime_error(message);
  }
  auto* loop = g_main_loop_new(nullptr, false);
  const auto timer = g_timeout_add(
      67,
      +[](gpointer ptr) -> gboolean {
        auto& state = *static_cast<Server*>(ptr);
        std::erase_if(state.peers, [](const auto& peer) {
          if (!peer->is_closed()) return false;
          peer->close();
          return true;
        });
        for (auto& peer : state.peers) peer->tick();
        return G_SOURCE_CONTINUE;
      },
      &state);
  auto stop = +[](gpointer data) -> gboolean {
    g_main_loop_quit(static_cast<GMainLoop*>(data));
    return G_SOURCE_CONTINUE;
  };
  auto sigint = g_unix_signal_add(SIGINT, stop, loop);
  auto sigterm = g_unix_signal_add(SIGTERM, stop, loop);
  std::cout << "DualView C++ server listening on " << (config.tls ? "https://" : "http://")
            << config.host << ":" << config.port << std::endl;
  g_main_loop_run(loop);
  g_source_remove(timer);
  g_source_remove(sigint);
  g_source_remove(sigterm);
  for (auto& peer : state.peers) peer->close();
  state.peers.clear();
  soup_server_disconnect(server);
  g_object_unref(server);
  g_main_loop_unref(loop);
  return 0;
}
}  // namespace dualview
