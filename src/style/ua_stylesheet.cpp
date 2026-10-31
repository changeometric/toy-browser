// The deliberately minimal user-agent style sheet for the M4 subset.

#include "ua_stylesheet.h"

namespace toy::style::detail {

std::string_view UserAgentStyleSheet() {
  static constexpr std::string_view kSheet = R"css(
head, style, script, title, meta, link { display: none; }
html, body, div, p, h1, h2, h3, h4, h5, h6, ul, ol, li, blockquote, pre, hr { display: block; }
span, a, em, strong, b, i, code, br, img { display: inline; }
body { margin: 8px; }
p { margin: 16px 0; }
h1 { font-size: 32px; font-weight: 700; margin: 16px 0; }
h2 { font-size: 24px; font-weight: 700; margin: 12px 0; }
h3 { font-size: 20px; font-weight: 700; margin: 12px 0; }
a { color: #0000ee; }
b, strong { font-weight: 700; }
i, em { font-style: italic; }
code, kbd, samp, pre { font-family: monospace; }
)css";
  return kSheet;
}

}  // namespace toy::style::detail
