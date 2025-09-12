#ifndef CONTENT_BROWSER_DEVTOOLS_SSL_SERVER_SOCKET_H_
#define CONTENT_BROWSER_DEVTOOLS_SSL_SERVER_SOCKET_H_

#include <memory>
#include <map>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_once_callback.h"
#include "net/socket/server_socket.h"
#include "net/socket/ssl_server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/ssl/ssl_server_config.h"

namespace content {

// SSL-capable server socket that wraps TCP connections with SSL
class DevToolsSSLServerSocket : public net::ServerSocket {
 public:
  explicit DevToolsSSLServerSocket(std::unique_ptr<net::ServerSocket> tcp_socket);
  ~DevToolsSSLServerSocket() override;

  // ServerSocket implementation
  int Listen(const net::IPEndPoint& address,
             int backlog,
             std::optional<bool> ipv6_only) override;
  int GetLocalAddress(net::IPEndPoint* address) const override;
  int Accept(std::unique_ptr<net::StreamSocket>* socket,
             net::CompletionOnceCallback callback) override;

 private:
  void OnTCPAcceptComplete(std::unique_ptr<net::StreamSocket>* output_socket,
                           net::CompletionOnceCallback callback,
                           int rv);
  void OnSSLHandshakeComplete(std::unique_ptr<net::StreamSocket>* output_socket,
                              net::CompletionOnceCallback callback,
                              int rv);
  
  bool InitializeSSLContext();

  std::unique_ptr<net::ServerSocket> tcp_socket_;
  std::unique_ptr<net::SSLServerContext> ssl_context_;
  
  // For SSL handshake operations
  std::unique_ptr<net::StreamSocket> temp_socket_;
  std::unique_ptr<net::SSLServerSocket> stored_ssl_socket_;
  
  base::WeakPtrFactory<DevToolsSSLServerSocket> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_DEVTOOLS_SSL_SERVER_SOCKET_H_