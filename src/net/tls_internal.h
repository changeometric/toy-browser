#pragma once

// Private TLS transport factory behind the shared HTTP/1.1 client code. This is
// the only header in the module that touches the TLS implementation, and it is
// private: no OpenSSL type appears in a public header.

#include <memory>
#include <string>

#include "stream_internal.h"
#include "toy/net/http.h"
#include "toy/net/url.h"

namespace toy::net::internal {

// Connects to `url` over TCP and performs a verified TLS handshake. Certificate
// verification against the default trust store and hostname verification are
// always on; there is no insecure mode. Returns null with `error` set on failure.
std::unique_ptr<Stream> OpenTlsStream(const Url& url, const FetchOptions& options,
                                      std::string* error);

}  // namespace toy::net::internal
