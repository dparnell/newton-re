/*
	File:		print/host/tls/HostTLS.cpp

	Contains:	TLS through the host's own library (HostTLS.h): Schannel on
				Windows, OpenSSL elsewhere (when the build found it), else
				none.

	Host only.
*/

#include "HostTLS.h"
#include <string.h>
#include <stdio.h>
#include <vector>
#include <string>

// the bytes in and out, and the plaintext decrypted and not yet read
struct HostTLSBuffers
{
	std::vector<unsigned char>	fIn;
	std::vector<unsigned char>	fOut;
	std::vector<unsigned char>	fPlain;
	std::string					fError;
	std::string					fName;
	bool						fDone = false;
	bool						fClosed = false;
};

static void
Fingerprint(const unsigned char* hash, char out[96])
{
	static const char kHex[] = "0123456789ABCDEF";
	char* p = out;
	for (int i = 0; i < 32; i++)
	{
		if (i > 0)
			*p++ = ':';
		*p++ = kHex[hash[i] >> 4];
		*p++ = kHex[hash[i] & 15];
	}
	*p = 0;
}


#if defined(_WIN32)

/*------------------------------------------------------------------------------
	S c h a n n e l
------------------------------------------------------------------------------*/

#define SECURITY_WIN32
#include <windows.h>
#include <wincrypt.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>

struct HostTLS : HostTLSBuffers
{
	CredHandle					fCred;
	CtxtHandle					fContext;
	bool						fHaveCred = false;
	bool						fHaveContext = false;
	SecPkgContext_StreamSizes	fSizes;
	int							fTrusted = 0;
	char						fFingerprint[96] = "";
};

int
HostTLSAvailable(void)
{
	return 1;
}


HostTLS*
HostTLSNew(const char* serverName)
{
	HostTLS* tls = new HostTLS;
	tls->fName = serverName != NULL ? serverName : "";
	SCHANNEL_CRED cred;
	memset(&cred, 0, sizeof(cred));
	cred.dwVersion = SCHANNEL_CRED_VERSION;
	cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
	// (the certificate is judged by the caller: HostTLSSystemTrusts and a pin)
	cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
	TimeStamp expiry;
	SECURITY_STATUS status = AcquireCredentialsHandleA(NULL, (LPSTR) UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
		NULL, &cred, NULL, NULL, &tls->fCred, &expiry);
	if (status != SEC_E_OK)
	{
		char text[64];
		snprintf(text, sizeof(text), "AcquireCredentialsHandle 0x%08lx", (unsigned long) status);
		tls->fError = text;
	}
	else
		tls->fHaveCred = true;
	return tls;
}


void
HostTLSFree(HostTLS* tls)
{
	if (tls == NULL)
		return;
	if (tls->fHaveContext)
		DeleteSecurityContext(&tls->fContext);
	if (tls->fHaveCred)
		FreeCredentialsHandle(&tls->fCred);
	delete tls;
}


// the server's certificate judged by the system's store, and hashed
static void
LookAtCertificate(HostTLS* tls)
{
	PCCERT_CONTEXT cert = NULL;
	if (QueryContextAttributesA(&tls->fContext, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) != SEC_E_OK || cert == NULL)
		return;
	BYTE hash[32];
	DWORD hashSize = sizeof(hash);
	if (CryptHashCertificate2(L"SHA256", 0, NULL, cert->pbCertEncoded, cert->cbCertEncoded, hash, &hashSize) && hashSize == 32)
		Fingerprint(hash, tls->fFingerprint);
	CERT_CHAIN_PARA chainPara;
	memset(&chainPara, 0, sizeof(chainPara));
	chainPara.cbSize = sizeof(chainPara);
	PCCERT_CHAIN_CONTEXT chain = NULL;
	if (CertGetCertificateChain(NULL, cert, NULL, cert->hCertStore, &chainPara, 0, NULL, &chain))
	{
		std::wstring name(tls->fName.begin(), tls->fName.end());
		SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl;
		memset(&ssl, 0, sizeof(ssl));
		ssl.cbSize = sizeof(ssl);
		ssl.dwAuthType = AUTHTYPE_SERVER;
		ssl.pwszServerName = (WCHAR*) name.c_str();
		CERT_CHAIN_POLICY_PARA policy;
		memset(&policy, 0, sizeof(policy));
		policy.cbSize = sizeof(policy);
		policy.pvExtraPolicyPara = &ssl;
		CERT_CHAIN_POLICY_STATUS result;
		memset(&result, 0, sizeof(result));
		result.cbSize = sizeof(result);
		if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &result) && result.dwError == 0)
			tls->fTrusted = 1;
		CertFreeCertificateChain(chain);
	}
	CertFreeCertificateContext(cert);
}


int
HostTLSHandshake(HostTLS* tls)
{
	if (tls->fDone)
		return 1;
	if (!tls->fHaveCred)
		return -1;
	if (tls->fHaveContext && tls->fIn.empty())
		return 0;
	SecBuffer inBuffers[2];
	inBuffers[0].BufferType = SECBUFFER_TOKEN;
	inBuffers[0].pvBuffer = tls->fIn.empty() ? NULL : tls->fIn.data();
	inBuffers[0].cbBuffer = (unsigned long) tls->fIn.size();
	inBuffers[1].BufferType = SECBUFFER_EMPTY;
	inBuffers[1].pvBuffer = NULL;
	inBuffers[1].cbBuffer = 0;
	SecBufferDesc inDesc = { SECBUFFER_VERSION, 2, inBuffers };
	SecBuffer outBuffers[1];
	outBuffers[0].BufferType = SECBUFFER_TOKEN;
	outBuffers[0].pvBuffer = NULL;
	outBuffers[0].cbBuffer = 0;
	SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, outBuffers };
	DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY
				| ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR | ISC_REQ_MANUAL_CRED_VALIDATION;
	DWORD outFlags = 0;
	TimeStamp expiry;
	SECURITY_STATUS status = InitializeSecurityContextA(&tls->fCred, tls->fHaveContext ? &tls->fContext : NULL,
		(SEC_CHAR*) tls->fName.c_str(), flags, 0, 0, tls->fHaveContext ? &inDesc : NULL, 0,
		&tls->fContext, &outDesc, &outFlags, &expiry);
	tls->fHaveContext = true;
	if (outBuffers[0].pvBuffer != NULL)
	{
		if (outBuffers[0].cbBuffer > 0)
		{
			const unsigned char* p = (const unsigned char*) outBuffers[0].pvBuffer;
			tls->fOut.insert(tls->fOut.end(), p, p + outBuffers[0].cbBuffer);
		}
		FreeContextBuffer(outBuffers[0].pvBuffer);
	}
	if (status == SEC_E_INCOMPLETE_MESSAGE)
		return 0;
	// what Schannel did not take is the next message's
	size_t extra = (inBuffers[1].BufferType == SECBUFFER_EXTRA) ? inBuffers[1].cbBuffer : 0;
	if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK)
	{
		if (extra > 0 && extra <= tls->fIn.size())
			tls->fIn.erase(tls->fIn.begin(), tls->fIn.end() - extra);
		else
			tls->fIn.clear();
		if (status == SEC_E_OK)
		{
			QueryContextAttributesA(&tls->fContext, SECPKG_ATTR_STREAM_SIZES, &tls->fSizes);
			LookAtCertificate(tls);
			tls->fDone = true;
			return 1;
		}
		// a continue with bytes still in hand goes on with them now
		if (!tls->fIn.empty())
			return HostTLSHandshake(tls);
		return 0;
	}
	char text[64];
	snprintf(text, sizeof(text), "InitializeSecurityContext 0x%08lx", (unsigned long) status);
	tls->fError = text;
	return -1;
}


int
HostTLSWrite(HostTLS* tls, const unsigned char* data, size_t size)
{
	if (!tls->fDone)
		return -1;
	while (size > 0)
	{
		size_t n = size < tls->fSizes.cbMaximumMessage ? size : tls->fSizes.cbMaximumMessage;
		std::vector<unsigned char> record(tls->fSizes.cbHeader + n + tls->fSizes.cbTrailer);
		memcpy(record.data() + tls->fSizes.cbHeader, data, n);
		SecBuffer buffers[4];
		buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
		buffers[0].pvBuffer = record.data();
		buffers[0].cbBuffer = tls->fSizes.cbHeader;
		buffers[1].BufferType = SECBUFFER_DATA;
		buffers[1].pvBuffer = record.data() + tls->fSizes.cbHeader;
		buffers[1].cbBuffer = (unsigned long) n;
		buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
		buffers[2].pvBuffer = record.data() + tls->fSizes.cbHeader + n;
		buffers[2].cbBuffer = tls->fSizes.cbTrailer;
		buffers[3].BufferType = SECBUFFER_EMPTY;
		buffers[3].pvBuffer = NULL;
		buffers[3].cbBuffer = 0;
		SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };
		SECURITY_STATUS status = EncryptMessage(&tls->fContext, 0, &desc, 0);
		if (status != SEC_E_OK)
		{
			char text[64];
			snprintf(text, sizeof(text), "EncryptMessage 0x%08lx", (unsigned long) status);
			tls->fError = text;
			return -1;
		}
		size_t total = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
		tls->fOut.insert(tls->fOut.end(), record.data(), record.data() + total);
		data += n;
		size -= n;
	}
	return 0;
}


// as many records as have come whole, decrypted into the plaintext
static void
DecryptWhatHasCome(HostTLS* tls)
{
	while (!tls->fIn.empty() && !tls->fClosed)
	{
		SecBuffer buffers[4];
		buffers[0].BufferType = SECBUFFER_DATA;
		buffers[0].pvBuffer = tls->fIn.data();
		buffers[0].cbBuffer = (unsigned long) tls->fIn.size();
		for (int i = 1; i < 4; i++)
		{
			buffers[i].BufferType = SECBUFFER_EMPTY;
			buffers[i].pvBuffer = NULL;
			buffers[i].cbBuffer = 0;
		}
		SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };
		SECURITY_STATUS status = DecryptMessage(&tls->fContext, &desc, 0, NULL);
		if (status == SEC_E_INCOMPLETE_MESSAGE)
			return;
		if (status == SEC_I_CONTEXT_EXPIRED)
		{
			tls->fClosed = true;
			return;
		}
		if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE)
		{
			char text[64];
			snprintf(text, sizeof(text), "DecryptMessage 0x%08lx", (unsigned long) status);
			tls->fError = text;
			tls->fClosed = true;
			return;
		}
		std::vector<unsigned char> extra;
		for (int i = 1; i < 4; i++)
		{
			if (buffers[i].BufferType == SECBUFFER_DATA)
			{
				const unsigned char* p = (const unsigned char*) buffers[i].pvBuffer;
				tls->fPlain.insert(tls->fPlain.end(), p, p + buffers[i].cbBuffer);
			}
			else if (buffers[i].BufferType == SECBUFFER_EXTRA)
			{
				const unsigned char* p = (const unsigned char*) buffers[i].pvBuffer;
				extra.assign(p, p + buffers[i].cbBuffer);
			}
		}
		tls->fIn.swap(extra);
		if (status == SEC_I_RENEGOTIATE)
		{
			// (TLS 1.2 renegotiation is not asked for; a server that
			// wants it is taken to have ended the connection)
			tls->fError = "the server asked to renegotiate";
			tls->fClosed = true;
			return;
		}
	}
}


long
HostTLSRead(HostTLS* tls, unsigned char* buffer, size_t size)
{
	if (!tls->fDone)
		return 0;
	DecryptWhatHasCome(tls);
	if (tls->fPlain.empty())
		return tls->fClosed ? -1 : 0;
	size_t n = size < tls->fPlain.size() ? size : tls->fPlain.size();
	memcpy(buffer, tls->fPlain.data(), n);
	tls->fPlain.erase(tls->fPlain.begin(), tls->fPlain.begin() + n);
	return (long) n;
}


int
HostTLSSystemTrusts(HostTLS* tls)
{
	return tls->fTrusted;
}


void
HostTLSFingerprint(HostTLS* tls, char fingerprint[96])
{
	strcpy(fingerprint, tls->fFingerprint);
}


#elif defined(HOST_TLS_OPENSSL)

/*------------------------------------------------------------------------------
	O p e n S S L
------------------------------------------------------------------------------*/

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>

struct HostTLS : HostTLSBuffers
{
	SSL_CTX*	fCtx = NULL;
	SSL*		fSSL = NULL;
	BIO*		fRead = NULL;		// what the peer sent, for OpenSSL to read
	BIO*		fWrite = NULL;		// what OpenSSL wrote, for the peer
	int			fTrusted = 0;
	char		fFingerprint[96] = "";
};

int
HostTLSAvailable(void)
{
	return 1;
}


static void
TakeOpenSSLError(HostTLS* tls, const char* what)
{
	char text[256];
	unsigned long e = ERR_get_error();
	snprintf(text, sizeof(text), "%s: %s", what, e != 0 ? ERR_error_string(e, NULL) : "failed");
	tls->fError = text;
}


HostTLS*
HostTLSNew(const char* serverName)
{
	HostTLS* tls = new HostTLS;
	tls->fName = serverName != NULL ? serverName : "";
	tls->fCtx = SSL_CTX_new(TLS_client_method());
	if (tls->fCtx == NULL)
	{
		TakeOpenSSLError(tls, "SSL_CTX_new");
		return tls;
	}
	SSL_CTX_set_min_proto_version(tls->fCtx, TLS1_2_VERSION);
	SSL_CTX_set_default_verify_paths(tls->fCtx);
	// (the certificate is judged by the caller: HostTLSSystemTrusts and a pin)
	SSL_CTX_set_verify(tls->fCtx, SSL_VERIFY_NONE, NULL);
	tls->fSSL = SSL_new(tls->fCtx);
	tls->fRead = BIO_new(BIO_s_mem());
	tls->fWrite = BIO_new(BIO_s_mem());
	SSL_set_bio(tls->fSSL, tls->fRead, tls->fWrite);
	SSL_set_connect_state(tls->fSSL);
	if (!tls->fName.empty())
	{
		SSL_set_tlsext_host_name(tls->fSSL, tls->fName.c_str());
		SSL_set1_host(tls->fSSL, tls->fName.c_str());
	}
	return tls;
}


void
HostTLSFree(HostTLS* tls)
{
	if (tls == NULL)
		return;
	if (tls->fSSL != NULL)
		SSL_free(tls->fSSL);			// (and its BIOs)
	if (tls->fCtx != NULL)
		SSL_CTX_free(tls->fCtx);
	delete tls;
}


// what OpenSSL wrote, moved to the bytes to send
static void
DrainWrite(HostTLS* tls)
{
	unsigned char buffer[4096];
	int n;
	while ((n = BIO_read(tls->fWrite, buffer, sizeof(buffer))) > 0)
		tls->fOut.insert(tls->fOut.end(), buffer, buffer + n);
}


// what the peer sent, handed to OpenSSL
static void
FeedRead(HostTLS* tls)
{
	if (!tls->fIn.empty())
	{
		BIO_write(tls->fRead, tls->fIn.data(), (int) tls->fIn.size());
		tls->fIn.clear();
	}
}


int
HostTLSHandshake(HostTLS* tls)
{
	if (tls->fDone)
		return 1;
	if (tls->fSSL == NULL)
		return -1;
	FeedRead(tls);
	int result = SSL_do_handshake(tls->fSSL);
	DrainWrite(tls);
	if (result == 1)
	{
		X509* cert = SSL_get1_peer_certificate(tls->fSSL);
		if (cert != NULL)
		{
			unsigned char hash[EVP_MAX_MD_SIZE];
			unsigned int size = 0;
			if (X509_digest(cert, EVP_sha256(), hash, &size) && size == 32)
				Fingerprint(hash, tls->fFingerprint);
			X509_free(cert);
			tls->fTrusted = (SSL_get_verify_result(tls->fSSL) == X509_V_OK);
		}
		tls->fDone = true;
		return 1;
	}
	int error = SSL_get_error(tls->fSSL, result);
	if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
		return 0;
	TakeOpenSSLError(tls, "SSL_do_handshake");
	return -1;
}


int
HostTLSWrite(HostTLS* tls, const unsigned char* data, size_t size)
{
	if (!tls->fDone)
		return -1;
	while (size > 0)
	{
		int n = SSL_write(tls->fSSL, data, (int) (size > 16384 ? 16384 : size));
		if (n <= 0)
		{
			TakeOpenSSLError(tls, "SSL_write");
			return -1;
		}
		DrainWrite(tls);
		data += n;
		size -= (size_t) n;
	}
	return 0;
}


long
HostTLSRead(HostTLS* tls, unsigned char* buffer, size_t size)
{
	if (!tls->fDone)
		return 0;
	FeedRead(tls);
	int n = SSL_read(tls->fSSL, buffer, (int) size);
	DrainWrite(tls);
	if (n > 0)
		return n;
	int error = SSL_get_error(tls->fSSL, n);
	if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
		return 0;
	if (error != SSL_ERROR_ZERO_RETURN)
		TakeOpenSSLError(tls, "SSL_read");
	return -1;
}


int
HostTLSSystemTrusts(HostTLS* tls)
{
	return tls->fTrusted;
}


void
HostTLSFingerprint(HostTLS* tls, char fingerprint[96])
{
	strcpy(fingerprint, tls->fFingerprint);
}


#else

/*------------------------------------------------------------------------------
	N o n e
------------------------------------------------------------------------------*/

struct HostTLS : HostTLSBuffers {};

int HostTLSAvailable(void) { return 0; }
HostTLS* HostTLSNew(const char*) { return NULL; }
void HostTLSFree(HostTLS* tls) { delete tls; }
int HostTLSHandshake(HostTLS*) { return -1; }
int HostTLSWrite(HostTLS*, const unsigned char*, size_t) { return -1; }
long HostTLSRead(HostTLS*, unsigned char*, size_t) { return -1; }
int HostTLSSystemTrusts(HostTLS*) { return 0; }
void HostTLSFingerprint(HostTLS*, char fingerprint[96]) { fingerprint[0] = 0; }

#endif


/*------------------------------------------------------------------------------
	T h e   b y t e s   i n   a n d   o u t   (every library)
------------------------------------------------------------------------------*/

void
HostTLSPutReceived(HostTLS* tls, const unsigned char* data, size_t size)
{
	tls->fIn.insert(tls->fIn.end(), data, data + size);
}


size_t
HostTLSPendingToSend(HostTLS* tls)
{
	return tls->fOut.size();
}


size_t
HostTLSTakeToSend(HostTLS* tls, unsigned char* buffer, size_t size)
{
	size_t n = size < tls->fOut.size() ? size : tls->fOut.size();
	memcpy(buffer, tls->fOut.data(), n);
	tls->fOut.erase(tls->fOut.begin(), tls->fOut.begin() + n);
	return n;
}


const char*
HostTLSError(HostTLS* tls)
{
	return tls->fError.c_str();
}
