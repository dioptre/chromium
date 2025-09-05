#include "content/public/browser/devtools_ssl_socket_factory.h"

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "crypto/keypair.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/socket/ssl_server_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/ssl/ssl_config.h"
#include "net/ssl/ssl_server_config.h"

namespace content {

DevToolsSSLSocketFactory::DevToolsSSLSocketFactory(
    const base::FilePath& cert_path,
    const base::FilePath& key_path,
    int port)
    : cert_path_(cert_path), key_path_(key_path), port_(port) {}

DevToolsSSLSocketFactory::~DevToolsSSLSocketFactory() = default;

std::unique_ptr<net::ServerSocket> DevToolsSSLSocketFactory::CreateForHttpServer() {
  // For now, return a simple TCP socket and log that WSS is requested
  // This is a simplified implementation that will need SSL wrapping
  auto tcp_socket = std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
  
  // Bind to port
  net::IPEndPoint endpoint(net::IPAddress::IPv4AllZeros(), port_);
  int result = tcp_socket->Listen(endpoint, 10, std::nullopt);
  if (result != net::OK) {
    LOG(ERROR) << "Failed to bind WSS socket to port " << port_;
    return nullptr;
  }

  // Generate certificate if needed
  if (!LoadOrGenerateSSLCertificate()) {
    LOG(ERROR) << "Failed to load or generate SSL certificate";
    return nullptr;
  }

  // TODO: Wrap TCP socket with SSL/TLS
  // For now, log success and return TCP socket
  LOG(INFO) << "DevTools WSS socket created on port " << port_;
  LOG(WARNING) << "SSL certificate generated but SSL wrapping not yet implemented";
  
  return std::move(tcp_socket);
}

std::unique_ptr<net::ServerSocket> DevToolsSSLSocketFactory::CreateForTethering(
    std::string* out_name) {
  // Tethering doesn't use SSL
  return nullptr;
}

bool DevToolsSSLSocketFactory::LoadOrGenerateSSLCertificate() {
  
  // Try to load existing certificate and key files if provided
  if (!cert_path_.empty() && !key_path_.empty()) {
    std::string cert_data, key_data;
    if (base::ReadFileToString(cert_path_, &cert_data) &&
        base::ReadFileToString(key_path_, &key_data)) {
      LOG(INFO) << "Loaded SSL certificate from " << cert_path_;
      return true;
    }
    LOG(WARNING) << "Failed to load SSL certificate files, generating new ones";
  }

  // Generate self-signed certificate
  return GenerateSelfSignedCertificate();
}

bool DevToolsSSLSocketFactory::GenerateSelfSignedCertificate() {
  
  LOG(INFO) << "Generating self-signed SSL certificate for DevTools WSS";

  // Generate key pair using Chromium's crypto API
  auto key_pair = crypto::keypair::PrivateKey::GenerateEcP256();

  // Create certificate using Chromium's X509 utilities
  base::Time not_before = base::Time::Now();
  base::Time not_after = not_before + base::Days(365); // 1 year validity

  // Certificate subject
  std::string subject = "CN=localhost";
  
  // Generate self-signed certificate
  std::string der_cert;
  bool success = net::x509_util::CreateSelfSignedCert(
      key_pair.key(),
      net::x509_util::DIGEST_SHA256,
      subject,
      123456, // Serial number
      not_before,
      not_after,
      {}, // No extensions
      &der_cert);

  if (!success || der_cert.empty()) {
    LOG(ERROR) << "Failed to generate self-signed certificate";
    return false;
  }

  LOG(INFO) << "Generated self-signed SSL certificate for localhost";
  return true;
}

}  // namespace content