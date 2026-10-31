// OpenSSL-backed TLS transport behind the shared HTTP/1.1 client code.
//
// One SSL_CTX is created lazily on the first https fetch and reused for the whole
// process; it is initialised from OpenSSL's default verify paths, so the standard
// SSL_CERT_FILE/SSL_CERT_DIR overrides are honoured at creation time. Certificate
// verification is always on and there is no insecure mode.

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <poll.h>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "socket_internal.h"
#include "stream_internal.h"
#include "tls_internal.h"
#include "toy/net/http.h"
#include "toy/net/url.h"

namespace toy::net::internal {
namespace {

constexpr int kMaxIoChunk = 1 << 20;

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

// Formats the first queued OpenSSL error and clears the queue; returns an empty
// string when no error was recorded.
std::string OpenSslErrorText() {
  const unsigned long code = ERR_get_error();
  if (code == 0) {
    return std::string();
  }
  char buffer[256];
  ERR_error_string_n(code, buffer, sizeof(buffer));
  ERR_clear_error();
  return std::string(buffer);
}

// SSL_write uses write(), which can raise SIGPIPE when the peer has gone away, so
// the TLS path ignores the signal once per process. This is the smallest documented
// mechanism; library code never installs a handler of its own.
void IgnoreSigpipeOnce() {
  static std::once_flag once;
  std::call_once(once, [] { std::signal(SIGPIPE, SIG_IGN); });
}

// Creates the process-wide client context on first use and keeps it until process
// exit (the unique_ptr deleter frees it).
SSL_CTX* TlsContext(std::string* error) {
  static std::once_flag init_once;
  static std::unique_ptr<SSL_CTX, void (*)(SSL_CTX*)> context(nullptr, &SSL_CTX_free);
  static std::string init_error;
  std::call_once(init_once, [] {
    SSL_CTX* raw = SSL_CTX_new(TLS_client_method());
    if (raw == nullptr) {
      init_error = "cannot create the TLS client context: " + OpenSslErrorText();
      return;
    }
    // Always verify the chain and the peer name; no option or argument can turn
    // this off.
    SSL_CTX_set_verify(raw, SSL_VERIFY_PEER, nullptr);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    // Real servers may close the TCP connection without a TLS close_notify; such an
    // EOF is end of stream (truncation is still caught by Content-Length/chunked).
    SSL_CTX_set_options(raw, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
    if (SSL_CTX_set_default_verify_paths(raw) != 1) {
      init_error = "cannot load the default TLS trust store: " + OpenSslErrorText();
      SSL_CTX_free(raw);
      return;
    }
    context.reset(raw);
  });
  if (context == nullptr) {
    SetError(error, init_error);
    return nullptr;
  }
  return context.get();
}

bool IsIpv4Literal(const std::string& host) {
  in_addr address{};
  return ::inet_pton(AF_INET, host.c_str(), &address) == 1;
}

// Waits for the TLS object's socket after SSL_ERROR_WANT_READ/WANT_WRITE.
bool WaitForTlsOperation(SSL* ssl, int ssl_error, int timeout_ms, const char* operation,
                         std::string* error) {
  const short events = ssl_error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
  const int ready = WaitForEvents(SSL_get_fd(ssl), events, timeout_ms, error);
  if (ready == 0) {
    SetError(error, std::string(operation) + " timed out");
    return false;
  }
  return ready == 1;
}

std::string IoErrorText(int ssl_error) {
  if (ssl_error == SSL_ERROR_SYSCALL) {
    if (errno != 0) {
      return std::strerror(errno);
    }
    return "connection closed unexpectedly";
  }
  const std::string detail = OpenSslErrorText();
  return detail.empty() ? "unknown TLS error" : detail;
}

// A failed handshake is reported as an ordinary fetch error. Chain failures say
// "certificate" and name mismatches say "hostname", which keeps the API contract
// readable without exposing OpenSSL details.
std::string HandshakeErrorText(SSL* ssl, int ssl_error) {
  const long verify_result = SSL_get_verify_result(ssl);
  if (verify_result != X509_V_OK) {
    const char* reason = X509_verify_cert_error_string(verify_result);
    if (verify_result == X509_V_ERR_HOSTNAME_MISMATCH ||
        verify_result == X509_V_ERR_IP_ADDRESS_MISMATCH) {
      return std::string("TLS certificate hostname mismatch: ") + reason;
    }
    return std::string("TLS certificate verification failed: ") + reason;
  }
  return "TLS handshake failed: " + IoErrorText(ssl_error);
}

// Enables SNI for DNS names (never for IPv4 literals) and pins verification to the
// requested host.
bool ConfigurePeerVerification(SSL* ssl, const Url& url, std::string* error) {
  if (IsIpv4Literal(url.host)) {
    X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
    if (X509_VERIFY_PARAM_set1_ip_asc(param, url.host.c_str()) != 1) {
      SetError(error, "cannot configure TLS IP address verification for '" + url.host + "'");
      return false;
    }
    return true;
  }
  if (SSL_set_tlsext_host_name(ssl, url.host.c_str()) != 1) {
    SetError(error, "cannot set the TLS server name (SNI) for '" + url.host + "'");
    return false;
  }
  if (SSL_set1_host(ssl, url.host.c_str()) != 1) {
    SetError(error, "cannot configure TLS hostname verification for '" + url.host + "'");
    return false;
  }
  return true;
}

bool Handshake(SSL* ssl, int timeout_ms, std::string* error) {
  for (;;) {
    errno = 0;
    const int result = SSL_connect(ssl);
    if (result == 1) {
      return true;
    }
    const int ssl_error = SSL_get_error(ssl, result);
    if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
      if (!WaitForTlsOperation(ssl, ssl_error, timeout_ms, "TLS handshake", error)) {
        return false;
      }
      continue;
    }
    if (ssl_error == SSL_ERROR_SYSCALL && errno == EINTR) {
      continue;
    }
    SetError(error, HandshakeErrorText(ssl, ssl_error));
    return false;
  }
}

class TlsStream final : public Stream {
 public:
  TlsStream(Socket socket, SSL* ssl) : socket_(std::move(socket)), ssl_(ssl) {}
  ~TlsStream() override {
    if (ssl_ != nullptr) {
      SSL_free(ssl_);
    }
  }

  TlsStream(const TlsStream&) = delete;
  TlsStream& operator=(const TlsStream&) = delete;

  bool WriteAll(std::string_view data, int timeout_ms, std::string* error) override {
    std::size_t offset = 0;
    while (offset < data.size()) {
      const std::size_t remaining = data.size() - offset;
      const int chunk = remaining > static_cast<std::size_t>(kMaxIoChunk)
                            ? kMaxIoChunk
                            : static_cast<int>(remaining);
      errno = 0;
      const int written = SSL_write(ssl_, data.data() + offset, chunk);
      if (written > 0) {
        offset += static_cast<std::size_t>(written);
        continue;
      }
      const int ssl_error = SSL_get_error(ssl_, written);
      if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
        if (!WaitForTlsOperation(ssl_, ssl_error, timeout_ms, "send", error)) {
          return false;
        }
        continue;
      }
      if (ssl_error == SSL_ERROR_SYSCALL && errno == EINTR) {
        continue;
      }
      SetError(error, "send failed: " + IoErrorText(ssl_error));
      return false;
    }
    return true;
  }

  bool ReadSome(char* buffer, std::size_t capacity, std::size_t* read, int timeout_ms,
                std::string* error) override {
    for (;;) {
      const int chunk = capacity > static_cast<std::size_t>(kMaxIoChunk)
                            ? kMaxIoChunk
                            : static_cast<int>(capacity);
      errno = 0;
      const int received = SSL_read(ssl_, buffer, chunk);
      if (received > 0) {
        *read = static_cast<std::size_t>(received);
        return true;
      }
      const int ssl_error = SSL_get_error(ssl_, received);
      // A clean close_notify and an unclean EOF are both end of stream.
      if (ssl_error == SSL_ERROR_ZERO_RETURN) {
        *read = 0;
        return true;
      }
      if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
        if (!WaitForTlsOperation(ssl_, ssl_error, timeout_ms, "read", error)) {
          return false;
        }
        continue;
      }
      if (ssl_error == SSL_ERROR_SYSCALL) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == 0) {
          *read = 0;
          return true;
        }
        SetError(error, "receive failed: " + IoErrorText(ssl_error));
        return false;
      }
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
      if (ssl_error == SSL_ERROR_SSL &&
          ERR_GET_REASON(ERR_peek_error()) == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
        ERR_clear_error();
        *read = 0;
        return true;
      }
#endif
      SetError(error, "receive failed: " + IoErrorText(ssl_error));
      return false;
    }
  }

 private:
  Socket socket_;
  SSL* ssl_ = nullptr;
};

}  // namespace

std::unique_ptr<Stream> OpenTlsStream(const Url& url, const FetchOptions& options,
                                      std::string* error) {
  IgnoreSigpipeOnce();
  SSL_CTX* context = TlsContext(error);
  if (context == nullptr) {
    return nullptr;
  }
  Socket socket;
  if (!ConnectTcp(url, options.connect_timeout_ms, &socket, error)) {
    return nullptr;
  }
  SSL* ssl = SSL_new(context);
  if (ssl == nullptr) {
    SetError(error, "cannot create the TLS connection: " + OpenSslErrorText());
    return nullptr;
  }
  if (SSL_set_fd(ssl, socket.get()) != 1) {
    SetError(error, "cannot attach the TLS connection to its socket: " + OpenSslErrorText());
    SSL_free(ssl);
    return nullptr;
  }
  if (!ConfigurePeerVerification(ssl, url, error) ||
      !Handshake(ssl, options.read_timeout_ms, error)) {
    ERR_clear_error();
    SSL_free(ssl);
    return nullptr;
  }
  return std::make_unique<TlsStream>(std::move(socket), ssl);
}

}  // namespace toy::net::internal
