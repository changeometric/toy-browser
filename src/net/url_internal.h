#pragma once

#include <string>
#include <string_view>

namespace toy::net {

// Percent-encoding helpers shared by URL serialization and HTTP request writing.
// Each keeps the characters allowed in its RFC 3986 component and encodes the
// rest as uppercase %XX escapes.
std::string EncodePath(std::string_view decoded);
std::string EncodeQuery(std::string_view decoded);

}  // namespace toy::net
