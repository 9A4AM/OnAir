// See tls_conn.h. OpenSSL is a system library (Apache-2.0 from 3.0), linked only when DECT2_TLS is set; no OpenSSL code is copied.
#include "tls_conn.h"
#ifdef DECT2_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace dect2::tls {
struct Ctx { SSL_CTX* c = nullptr; };
struct Conn { SSL* s = nullptr; };

static std::string sslErr() {
    char b[256];
    const unsigned long e = ERR_get_error();
    if (!e) return "unknown error";
    ERR_error_string_n(e, b, sizeof b);
    return b;
}
bool available() { return true; }
Ctx* makeCtx(const std::string& cert, const std::string& key, std::string& err) {
    SSL_CTX* c = SSL_CTX_new(TLS_server_method());
    if (!c) { err = "TLS: " + sslErr(); return nullptr; }
    SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_chain_file(c, cert.c_str()) != 1) { err = "TLS certificate \"" + cert + "\" cannot be read: " + sslErr(); SSL_CTX_free(c); return nullptr; }
    if (SSL_CTX_use_PrivateKey_file(c, key.c_str(), SSL_FILETYPE_PEM) != 1) { err = "TLS key \"" + key + "\" cannot be read: " + sslErr(); SSL_CTX_free(c); return nullptr; }
    if (SSL_CTX_check_private_key(c) != 1) { err = "TLS key does not belong to the certificate"; SSL_CTX_free(c); return nullptr; }
    return new Ctx{c};
}
void freeCtx(Ctx* c) { if (c) { SSL_CTX_free(c->c); delete c; } }
Conn* accept(Ctx* c, sock_t s, std::string& err) {
    SSL* ssl = SSL_new(c->c);
    if (!ssl) { err = sslErr(); return nullptr; }
    SSL_set_fd(ssl, (int)s);
    if (SSL_accept(ssl) != 1) { err = sslErr(); SSL_free(ssl); return nullptr; }
    return new Conn{ssl};
}
long read(Conn* c, void* d, size_t n) { return SSL_read(c->s, d, (int)n); }
long write(Conn* c, const void* d, size_t n) { return SSL_write(c->s, d, (int)n); }
bool pending(Conn* c) { return SSL_pending(c->s) > 0; }
void close(Conn* c) { if (!c) return; SSL_shutdown(c->s); SSL_free(c->s); delete c; }
}
#else
namespace dect2::tls {
bool available() { return false; }
Ctx* makeCtx(const std::string&, const std::string&, std::string& err) { err = "this build has no TLS support (built without OpenSSL): use a reverse proxy such as Caddy or nginx in front of the server"; return nullptr; }
void freeCtx(Ctx*) {}
Conn* accept(Ctx*, sock_t, std::string& err) { err = "no TLS"; return nullptr; }
long read(Conn*, void*, size_t) { return -1; }
long write(Conn*, const void*, size_t) { return -1; }
bool pending(Conn*) { return false; }
void close(Conn*) {}
}
#endif
