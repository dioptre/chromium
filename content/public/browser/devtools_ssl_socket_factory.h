#ifndef CONTENT_PUBLIC_BROWSER_DEVTOOLS_SSL_SOCKET_FACTORY_H_
#define CONTENT_PUBLIC_BROWSER_DEVTOOLS_SSL_SOCKET_FACTORY_H_

#include "content/public/browser/devtools_socket_factory.h"
#include "content/common/content_export.h"
#include "base/files/file_path.h"

namespace content {

class CONTENT_EXPORT DevToolsSSLSocketFactory : public DevToolsSocketFactory {
 public:
  // Constructor that accepts optional certificate and key paths.
  // If paths are empty or files don't exist, self-signed certificates
  // will be automatically generated.
  DevToolsSSLSocketFactory(const base::FilePath& cert_path,
                           const base::FilePath& key_path,
                           int port);
  ~DevToolsSSLSocketFactory() override;

  std::unique_ptr<net::ServerSocket> CreateForHttpServer() override;
  std::unique_ptr<net::ServerSocket> CreateForTethering(
      std::string* out_name) override;

 private:
  // Attempts to load certificate files, falls back to generation if needed
  bool LoadOrGenerateSSLCertificate();
  
  // Generates a self-signed certificate for localhost
  bool GenerateSelfSignedCertificate();

  base::FilePath cert_path_;
  base::FilePath key_path_;
  int port_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_DEVTOOLS_SSL_SOCKET_FACTORY_H_