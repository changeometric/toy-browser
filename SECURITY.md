# Security policy

## Scope and expectations

toybrowser is an experimental learning project, not a hardened browser. It implements a small
subset of HTML/CSS and rendering, has no JavaScript engine and no sandbox, and should not be used to
browse untrusted pages or to enter sensitive information. Treat any fetched page, image, font or
external stylesheet as untrusted input.

The one deliberate security feature is HTTPS: certificate-chain and hostname verification are always
on, there is no insecure mode and no way to disable verification. HTTP responses are bounded by a
body size cap and timeouts, including after gzip decoding (M13); keep-alive connections
do not change the trust model.

## Supported versions

The `main` branch is the only supported version. There are no release branches or backports.

## Reporting a vulnerability

Please do not open a public issue for a suspected vulnerability. Use the repository's private
security reporting channel (for example, GitHub's "Report a vulnerability" / private security
advisory feature) and include:

- the affected command or page,
- a minimal reproduction,
- the observed impact and the expected behavior,
- the commit or version you tested.

You will get a best-effort acknowledgement. This is a hobby project, so no response-time or fix-time
guarantee is provided.

## Known non-issues

- `tests/fixtures/tls/` contains private keys and certificates. They are generated exclusively for
  the in-process loopback TLS tests, chain to nothing public and are explicitly documented as
  test-only. Their presence in the repository is intentional, not a leaked secret.
- Rendering differences between installed fonts, window managers or distributions are not security
  issues.
- A page being able to consume CPU or memory while rendering is expected for a non-hardened toy
  browser; report it only if it bypasses the documented limits or crashes the process in a way that
  looks memory-unsafe.
