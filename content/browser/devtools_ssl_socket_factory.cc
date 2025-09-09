#include "content/public/browser/devtools_ssl_socket_factory.h"

#include "base/logging.h"
#include "net/base/net_errors.h"
#include "net/socket/tcp_server_socket.h"

namespace content {

DevToolsSSLSocketFactory::DevToolsSSLSocketFactory(
    const base::FilePath& cert_path,
    const base::FilePath& key_path,
    int port)
    : cert_path_(cert_path), key_path_(key_path), port_(port) {}

DevToolsSSLSocketFactory::~DevToolsSSLSocketFactory() = default;

std::unique_ptr<net::ServerSocket> DevToolsSSLSocketFactory::CreateForHttpServer() {
  // Create TCP socket
  auto tcp_socket = std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
  
  // Bind to port
  net::IPEndPoint endpoint(net::IPAddress::IPv4AllZeros(), port_);
  int result = tcp_socket->Listen(endpoint, 10, std::nullopt);
  if (result != net::OK) {
    LOG(ERROR) << "Failed to bind TCP socket to WSS port " << port_;
    return nullptr;
  }

  LOG(INFO) << "WSS ServerSocket created on port " << port_ << " (TCP mode for testing)";
  return std::move(tcp_socket);
}

std::unique_ptr<net::ServerSocket> DevToolsSSLSocketFactory::CreateForTethering(
    std::string* out_name) {
  // Tethering doesn't use SSL
  return nullptr;
}

}  // namespace content