# TLS test certificates

Test-only certificates for the deterministic `https://` tests. They were generated once for this
repository with the OpenSSL command-line tool and are committed as fixtures so no test needs the
public network, a public CA or an installed certificate store.

| File | Purpose |
| --- | --- |
| `test_ca.pem` | Self-signed test CA certificate (not a public CA). Tests trust it by pointing `SSL_CERT_FILE` at it. |
| `localhost_cert.pem` | Leaf certificate for `localhost` (SAN `DNS:localhost`), signed by `test_ca.pem`. |
| `localhost_key.pem` | Private key for `localhost_cert.pem`. Test-only: never used outside the tests. |
| `untrusted_cert.pem` | Self-signed leaf certificate for `localhost`, deliberately **not** signed by `test_ca.pem`. Used to assert that certificate verification fails. |
| `untrusted_key.pem` | Private key for `untrusted_cert.pem`. Test-only. |

All certificates are valid for 20 years: the CA and the `localhost` leaf expire on 2046-10-26 and
the untrusted leaf on 2046-10-26. None of them is a secret: they chain to nothing public and only
exist for the loopback tests.

## How the tests use them

- The in-process TLS loopback server lives in `tests/support/tls_server.h`; tests pass it the
  certificate/key file pair they need.
- Trusted tests set `SSL_CERT_FILE` to `test_ca.pem` through the CTest `ENVIRONMENT` property, so
  the transport's normal OpenSSL default trust paths see exactly this CA. No test overrides the
  trust store in code and no insecure mode exists.
- Hostname verification is exercised with the same `localhost_cert.pem`: a client that connects to
  `https://127.0.0.1:<port>/` must fail, because the certificate names `localhost`, not the IP.

## Regenerating

Only needed if a certificate is ever lost or the fixtures must move. The CA private key is *not*
committed; these commands regenerate a consistent set (run from this directory, with a scratch
directory for the intermediate CA key and serial file):

```sh
openssl req -x509 -newkey rsa:3072 -nodes -keyout /tmp/test_ca.key -out test_ca.pem -days 7300 \
    -subj "/CN=Toybrowser Test CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl req -new -newkey rsa:2048 -nodes -keyout localhost_key.pem -out /tmp/localhost.csr \
    -subj "/CN=localhost"
printf '%s\n' \
    "basicConstraints=critical,CA:FALSE" \
    "keyUsage=critical,digitalSignature,keyEncipherment" \
    "extendedKeyUsage=serverAuth" \
    "subjectAltName=DNS:localhost" > /tmp/localhost.cnf
openssl x509 -req -in /tmp/localhost.csr -CA test_ca.pem -CAkey /tmp/test_ca.key \
    -CAcreateserial -out localhost_cert.pem -days 7300 -extfile /tmp/localhost.cnf
openssl req -x509 -newkey rsa:2048 -nodes -keyout untrusted_key.pem -out untrusted_cert.pem \
    -days 7300 -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
    -addext "extendedKeyUsage=serverAuth"
```

If `localhost_cert.pem` or the CA ever changes, keep the certificate/key pairs in sync: the leaf
must stay valid for `localhost` and the untrusted leaf must stay unrelated to the test CA.
