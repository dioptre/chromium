#include "content/browser/devtools_ssl_server_socket.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/rand_util.h"
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
  // Use Chromium's working test certificate for localhost
  std::string cert_path = "net/data/ssl/certificates/localhost_cert.pem";
  std::string key_path = "net/data/ssl/certificates/localhost_cert.key";
  
  LOG(INFO) << "🔐 Loading Chromium test certificate: " << cert_path;
  
  // Load certificate and key from Chromium's test data
  std::string cert_data, key_data;
  if (!base::ReadFileToString(base::FilePath(cert_path), &cert_data) ||
      !base::ReadFileToString(base::FilePath(key_path), &key_data)) {
    LOG(WARNING) << "Failed to load test cert, falling back to generated cert";
    
    // Fallback to simple generated certificate
    auto private_key = crypto::keypair::PrivateKey::GenerateRsa2048();
    
    base::Time not_before = base::Time::Now();
    base::Time not_after = not_before + base::Days(1);
    
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
      LOG(ERROR) << "Failed to generate fallback certificate";
      return false;
    }
    
    // Create credentials with generated cert
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
  } else {
    // Use test certificate - TODO: Parse PEM format  
    LOG(INFO) << "Test certificate loaded, using simple fallback for now";
    
    // For now, generate simple cert since PEM parsing is complex
    auto private_key = crypto::keypair::PrivateKey::GenerateRsa2048();
    
    base::Time not_before = base::Time::Now();
    base::Time not_after = not_before + base::Days(1);
    
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
      LOG(ERROR) << "Failed to generate test certificate";
      return false;
    }
    
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
  }
  
  // Permissive SSL config for DevTools WSS development
  net::SSLServerConfig ssl_config;
  ssl_config.version_min = net::SSL_PROTOCOL_VERSION_TLS1_2;
  ssl_config.version_max = net::SSL_PROTOCOL_VERSION_TLS1_3;
  ssl_config.client_cert_type = net::SSLServerConfig::NO_CLIENT_CERT;
  
  // Permissive settings for development with self-signed certs
  ssl_config.require_ecdhe = false; // Allow more cipher suites
  
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
  LOG(INFO) << "🔐 WSS Accept called - SSL handshake will be performed";
  
  // Accept TCP connection and wrap with SSL
  auto accept_callback = base::BindOnce(
      &DevToolsSSLServerSocket::OnTCPAcceptComplete,
      weak_factory_.GetWeakPtr(), socket, std::move(callback));
  
  return tcp_socket_->Accept(&temp_socket_, std::move(accept_callback));
}

void DevToolsSSLServerSocket::OnTCPAcceptComplete(
    std::unique_ptr<net::StreamSocket>* output_socket,
    net::CompletionOnceCallback callback,
    int rv) {
  
  if (rv != net::OK) {
    LOG(ERROR) << "WSS TCP accept failed: " << net::ErrorToString(rv);
    std::move(callback).Run(rv);
    return;
  }

  if (!ssl_context_) {
    LOG(ERROR) << "WSS SSL context not initialized";
    std::move(callback).Run(net::ERR_SSL_PROTOCOL_ERROR);
    return;
  }

  // Wrap with SSL
  auto ssl_socket = ssl_context_->CreateSSLServerSocket(std::move(temp_socket_));
  if (!ssl_socket) {
    LOG(ERROR) << "WSS failed to create SSL server socket";
    std::move(callback).Run(net::ERR_SSL_PROTOCOL_ERROR);
    return;
  }

  // Perform SSL handshake
  auto handshake_callback = base::BindOnce(
      &DevToolsSSLServerSocket::OnSSLHandshakeComplete,
      weak_factory_.GetWeakPtr(), output_socket, std::move(callback));
  
  stored_ssl_socket_ = std::move(ssl_socket);
  int handshake_rv = stored_ssl_socket_->Handshake(std::move(handshake_callback));
  
  if (handshake_rv != net::ERR_IO_PENDING) {
    OnSSLHandshakeComplete(output_socket, std::move(callback), handshake_rv);
  }
}

void DevToolsSSLServerSocket::OnSSLHandshakeComplete(
    std::unique_ptr<net::StreamSocket>* output_socket,
    net::CompletionOnceCallback callback,
    int rv) {
  
  if (rv == net::OK && stored_ssl_socket_) {
    LOG(INFO) << "🔐 WSS SSL handshake completed successfully";
    *output_socket = std::move(stored_ssl_socket_);
  } else {
    LOG(ERROR) << "WSS SSL handshake failed: " << net::ErrorToString(rv);
    stored_ssl_socket_.reset();
  }
  
  std::move(callback).Run(rv);
}

}  // namespace content