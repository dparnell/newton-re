# tools/print/testcerts - certificates for tests only

Two self-signed X.509 certificates and their private keys, **for tests
only**: never trust them anywhere else, and never use them for anything but
the fake printer.  The keys are public - they are in this repository.

| file | what |
| --- | --- |
| `printer-a.pem`, `printer-a.key` | "Test printer a - tests only", SHA-256 `8D:85:C4:67:83:4A:B8:F0:41:34:79:B6:12:DC:EF:96:8D:2C:C6:FC:4D:C0:F4:31:F8:50:17:CA:95:7B:D1:56` |
| `printer-b.pem`, `printer-b.key` | "Test printer b - tests only", SHA-256 `DD:DF:91:A5:E4:DA:53:BD:37:9B:1D:24:D0:BC:93:65:A4:07:1F:D0:D7:8D:5E:D1:B3:FA:B7:98:E1:50:CA:D4` |

`tools/print/ippprinter.py --tls tools/print/testcerts/printer-a.pem` runs the
fake printer over TLS (ipps://) with one of them; the second is the same
printer after its certificate has changed.  They are used by ctests
`print.HostTLS` (the host's TLS, `src/print/host/tls/HostTLS.h`, which checks
the fingerprint above), `host.NewtonIPPS` and its runs, and
`host.NewtonIPPSPCL` (`src/host/demo/print-ipps.ns`: the Newton asking
whether to trust a certificate the system does not vouch for).

They are committed rather than made at test time because no certificate
generator is guaranteed on a test machine (Python's `ssl` module cannot make
one).  They were made with OpenSSL 3, valid for a hundred years, for
`localhost` and `127.0.0.1`:

    openssl req -x509 -newkey rsa:2048 -nodes -keyout printer-a.key -out printer-a.pem \
        -days 36500 -subj "/CN=Test printer a - tests only" \
        -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

(and the same for `b`).  Remaking them changes their fingerprints, which the
ctests above name: update `src/print/tests/CMakeLists.txt` and
`src/host/CMakeLists.txt` with the new ones (`openssl x509 -in printer-a.pem
-noout -fingerprint -sha256`).
