/*
	File:		print/host/tls/HostTLS.h

	Contains:	TLS as the host's own library does it, for ipps:// printers
				(print/host/HostIPPSocket.h): Schannel (SSPI) on Windows,
				OpenSSL elsewhere.  Never cryptography of our own.

				The connection is the caller's: the bytes the peer sends are
				handed in (HostTLSPutReceived) and the bytes to send are taken
				out (HostTLSTakeToSend), so the same code serves a polled
				comm tool and a task waiting in PrReleaseControl.

				The library does not decide whether to trust the printer: it
				says whether the system's certificate store vouches for the
				certificate and the name (HostTLSSystemTrusts), and gives its
				SHA-256 fingerprint (HostTLSFingerprint) for the caller's own
				pin.

				Built without the Newton include paths (the platforms'
				headers and the DDK's names do not mix): a plain C interface.

	Host only.
*/

#ifndef __HOSTTLS_H
#define __HOSTTLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HostTLS HostTLS;

// Whether this host build has TLS at all.
int			HostTLSAvailable(void);

// A client session for a server of that name (the name its certificate
// must carry for the system to vouch for it).  ==> NULL without TLS.
HostTLS*	HostTLSNew(const char* serverName);
void		HostTLSFree(HostTLS* tls);

// The peer's bytes, handed in; bytes for the peer, taken out.
void		HostTLSPutReceived(HostTLS* tls, const unsigned char* data, size_t size);
size_t		HostTLSPendingToSend(HostTLS* tls);
size_t		HostTLSTakeToSend(HostTLS* tls, unsigned char* buffer, size_t size);

// The handshake carried on with what has been received: 1 done, 0 more to
// come, -1 failed (HostTLSError says why).
int			HostTLSHandshake(HostTLS* tls);

// After the handshake: data encrypted into the bytes to send (==> 0, or -1
// failed); data decrypted out of the bytes received (==> how much, 0 none
// yet, -1 the peer closed or failed).
int			HostTLSWrite(HostTLS* tls, const unsigned char* data, size_t size);
long		HostTLSRead(HostTLS* tls, unsigned char* buffer, size_t size);

// After the handshake: whether the system's certificate store vouches for
// the server's certificate and name; its SHA-256 fingerprint, as 32 hex
// pairs separated by colons (95 characters and the nul).
int			HostTLSSystemTrusts(HostTLS* tls);
void		HostTLSFingerprint(HostTLS* tls, char fingerprint[96]);

// What went wrong, for the log.
const char*	HostTLSError(HostTLS* tls);

#ifdef __cplusplus
}
#endif

#endif	/* __HOSTTLS_H */
