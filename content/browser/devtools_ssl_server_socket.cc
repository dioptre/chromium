#include "content/browser/devtools_ssl_server_socket.h"

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "crypto/keypair.h"
#include "net/base/net_errors.h"
#include "net/cert/x509_util.h"
#include "net/socket/ssl_server_socket.h"
#include "third_party/boringssl/src/include/openssl/crypto.h"

namespace content {

DevToolsSSLServerSocket::DevToolsSSLServerSocket(
    std::unique_ptr<net::ServerSocket> tcp_socket)
    : tcp_socket_(std::move(tcp_socket)) {
  InitializeSSLContext();
}

DevToolsSSLServerSocket::~DevToolsSSLServerSocket() = default;

bool DevToolsSSLServerSocket::InitializeSSLContext() {
  // Generate self-signed certificate
  auto private_key = crypto::keypair::PrivateKey::GenerateRsa2048();
  
  base::Time not_before = base::Time::Now();
  base::Time not_after = not_before + base::Days(365);
  
  std::string der_cert;
  bool success = net::x509_util::CreateSelfSignedCert(
      private_key.key(),
      net::x509_util::DIGEST_SHA256,
      "CN=localhost",
      123456,
      not_before,
      not_after,
      {},
      &der_cert);
      
  if (!success) {
    LOG(ERROR) << "Failed to generate SSL certificate for WSS";
    return false;
  }

  // Create SSL credentials
  std::vector<net::SSLServerCredential> credentials;
  net::SSLServerCredential credential;
  
  credential.cert_chain.push_back(
      bssl::UniquePtr<CRYPTO_BUFFER>(CRYPTO_BUFFER_new(
          reinterpret_cast<const uint8_t*>(der_cert.data()),
          der_cert.size(),
          nullptr)));
  
  EVP_PKEY* key_copy = private_key.key();
  EVP_PKEY_up_ref(key_copy);
  credential.pkey = bssl::UniquePtr<EVP_PKEY>(key_copy);
  
  credentials.push_back(std::move(credential));
  
  // SSL config
  net::SSLServerConfig ssl_config;
  ssl_config.version_min = net::SSL_PROTOCOL_VERSION_TLS1_2;
  ssl_config.version_max = net::SSL_PROTOCOL_VERSION_TLS1_3;
  
  ssl_context_ = net::CreateSSLServerContext(
      std::move(credentials), ssl_config);
      
  LOG(INFO) << "WSS SSL context initialized";
  return true;
}

int DevToolsSSLServerSocket::Listen(const net::IPEndPoint& address,
                                    int backlog,
                                    std::optional<bool> ipv6_only) {
  return tcp_socket_->Listen(address, backlog, ipv6_only);
}

int DevToolsSSLServerSocket::GetLocalAddress(net::IPEndPoint* address) const {
  return tcp_socket_->GetLocalAddress(address);
}

int DevToolsSSLServerSocket::Accept(std::unique_ptr<net::StreamSocket>* socket,
                                    net::CompletionOnceCallback callback) {
  // For now, just pass through to TCP socket to avoid callback complexity
  LOG(INFO) << "WSS Accept called - using TCP passthrough (SSL TODO)";
  return tcp_socket_->Accept(socket, std::move(callback));
}

void DevToolsSSLServerSocket::OnAcceptComplete(int rv) {
  if (!pending_callback_) {
    LOG(ERROR) << "WSS OnAcceptComplete called with null callback";
    return;
  }

  if (rv != net::OK) {
    std::move(pending_callback_).Run(rv);
    return;
  }

  if (!ssl_context_) {
    LOG(ERROR) << "WSS SSL context is null";
    std::move(pending_callback_).Run(net::ERR_SSL_PROTOCOL_ERROR);
    return;
  }

  if (!accepted_socket_) {
    LOG(ERROR) << "WSS accepted socket is null";
    std::move(pending_callback_).Run(net::ERR_CONNECTION_FAILED);
    return;
  }

  // Wrap with SSL
  auto ssl_socket = ssl_context_->CreateSSLServerSocket(std::move(accepted_socket_));
  if (!ssl_socket) {
    LOG(ERROR) << "Failed to create SSL server socket";
    std::move(pending_callback_).Run(net::ERR_SSL_PROTOCOL_ERROR);
    return;
  }

  // Get pointer before moving
  net::SSLServerSocket* ssl_socket_ptr = ssl_socket.get();
  
  // Perform SSL handshake
  auto handshake_callback = base::BindOnce(
      &DevToolsSSLServerSocket::OnSSLHandshakeComplete,
      weak_factory_.GetWeakPtr(),
      std::move(ssl_socket));
  
  int handshake_rv = ssl_socket_ptr->Handshake(std::move(handshake_callback));
  
  if (handshake_rv != net::ERR_IO_PENDING) {
    // Note: ssl_socket was moved, so we can't use it here
    OnSSLHandshakeComplete(nullptr, handshake_rv);
  }
}

void DevToolsSSLServerSocket::OnSSLHandshakeComplete(
    std::unique_ptr<net::SSLServerSocket> ssl_socket,
    int rv) {
  
  if (!pending_callback_) {
    LOG(ERROR) << "WSS SSL handshake complete called with null callback";
    return;
  }
  
  if (rv == net::OK && ssl_socket) {
    LOG(INFO) << "WSS SSL handshake completed successfully";
    if (pending_socket_) {
      *pending_socket_ = std::move(ssl_socket);
    }
  } else {
    LOG(ERROR) << "WSS SSL handshake failed: " << net::ErrorToString(rv);
  }
  
  std::move(pending_callback_).Run(rv);
  pending_socket_ = nullptr;
}

}  // namespace content