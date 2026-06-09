#include "jelly_http.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>

namespace jelly_http {

    namespace {
        // Plain TCP connect with a connect timeout. Numeric IPs go through
        // inet_pton; hostnames resolve via getaddrinfo. Returns a blocking fd or -1.
        int tcp_connect(const char *host, u16 port, int timeout_ms) {

        struct sockaddr_in sa;
        std::memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(port);

        if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
            // Not a numeric IP -> resolve via DNS (flaky in an overlay, so only
            // real hostnames hit this path; numeric IPs took the fast path above).
            struct addrinfo hints;
            std::memset(&hints, 0, sizeof hints);
            hints.ai_family   = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo *res = nullptr;
            if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return -1;
            sa.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
            freeaddrinfo(res);
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        // Non-blocking connect + select() so a dead server can't hang the thread.
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(fd, (struct sockaddr *)&sa, sizeof sa);
        if (rc != 0) {
            if (errno != EINPROGRESS) { close(fd); return -1; }
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            struct timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            if (select(fd + 1, nullptr, &wset, nullptr, &tv) <= 0) { close(fd); return -1; }
            int err = 0; socklen_t len = sizeof err;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) { close(fd); return -1; }
        }
        fcntl(fd, F_SETFL, flags);   // back to blocking for send/recv
        return fd;
        }

        struct TlsCtx {
            mbedtls_ssl_context      ssl;
            mbedtls_ssl_config       conf;
            mbedtls_ctr_drbg_context drbg;
            int                      fd;
        };

        // Entropy straight from the console CSPRNG (mbedTLS's default source
        // doesn't work on Switch — this is the classic gotcha).
        int entropy_src(void *, unsigned char *out, size_t len) {
            randomGet(out, len);
            return 0;
        }

        // BIO over a blocking socket. We never use non-blocking I/O, so EAGAIN here
        // can only be an SO_RCVTIMEO handshake timeout -> treat as failure.
        int bio_send(void *ctx, const unsigned char *b, size_t n) {
            ssize_t r = ::send(*(int *)ctx, b, n, 0);
            return r >= 0 ? (int)r : MBEDTLS_ERR_NET_SEND_FAILED;
        }
        int bio_recv(void *ctx, unsigned char *b, size_t n) {
            ssize_t r = ::recv(*(int *)ctx, b, n, 0);
            return r >= 0 ? (int)r : MBEDTLS_ERR_NET_RECV_FAILED;
        }

        void tls_destroy(TlsCtx *t) {
            mbedtls_ssl_free(&t->ssl);
            mbedtls_ssl_config_free(&t->conf);
            mbedtls_ctr_drbg_free(&t->drbg);
            if (t->fd >= 0) close(t->fd);
            delete t;
        }
    }

    Conn open(const char *host, u16 port, bool tls, int timeout_ms) {
        Conn c;
        int fd = tcp_connect(host, port, timeout_ms);
        if (fd < 0) return c;            // c.fd == -1
        if (!tls) { c.fd = fd; return c; }

        // Bound the handshake reads; streaming switches back to block-forever below.
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

        TlsCtx *t = new (std::nothrow) TlsCtx;
        if (!t) { close(fd); return c; }
        t->fd = fd;
        mbedtls_ssl_init(&t->ssl);
        mbedtls_ssl_config_init(&t->conf);
        mbedtls_ctr_drbg_init(&t->drbg);

        bool ok =
            mbedtls_ctr_drbg_seed(&t->drbg, entropy_src, nullptr,
                                  (const unsigned char *)"streamfin", 9) == 0 &&
            mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) == 0;
        if (ok) {
            mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_NONE);   // encrypt, don't verify chain
            mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
            ok = mbedtls_ssl_setup(&t->ssl, &t->conf) == 0 &&
                 mbedtls_ssl_set_hostname(&t->ssl, host) == 0;       // SNI — required for the proxy
        }
        if (ok) {
            mbedtls_ssl_set_bio(&t->ssl, &t->fd, bio_send, bio_recv, nullptr);
            int r;
            while ((r = mbedtls_ssl_handshake(&t->ssl)) != 0)
                if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) { ok = false; break; }
        }
        if (!ok) { tls_destroy(t); return c; }
        struct timeval zero = { 0, 0 };   // streaming: block until data / interrupt
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &zero, sizeof zero);
        c.fd  = fd;
        c.ssl = t;
        return c;
    }

    bool conn_ok(const Conn &c) { return c.fd >= 0; }

    ssize_t conn_send(Conn &c, const void *buf, size_t n) {
        if (c.ssl) {
            int r = mbedtls_ssl_write(&((TlsCtx *)c.ssl)->ssl, (const unsigned char *)buf, n);
            return r < 0 ? -1 : r;
        }
        return ::send(c.fd, buf, n, 0);
    }

    ssize_t conn_recv(Conn &c, void *buf, size_t n) {
        if (c.ssl) {
            int r = mbedtls_ssl_read(&((TlsCtx *)c.ssl)->ssl, (unsigned char *)buf, n);
            if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;   // clean EOF
            return r < 0 ? -1 : r;
        }
        return ::recv(c.fd, buf, n, 0);
    }

    // Close just the socket so a blocking conn_recv on another thread returns;
    // leaves the TLS ctx intact (free it with conn_close after the reader exits).
    void conn_interrupt(Conn &c) {
        int *pfd = c.ssl ? &((TlsCtx *)c.ssl)->fd : &c.fd;
        int fd = *pfd;
        if (fd >= 0) {
            *pfd = -1;
            ::shutdown(fd, SHUT_RDWR);
            close(fd);
        }
    }

    void conn_close(Conn &c) {
        if (c.ssl)            tls_destroy((TlsCtx *)c.ssl), c.ssl = nullptr;
        else if (c.fd >= 0)   close(c.fd);
        c.fd = -1;
    }

    Response request(const char *host, u16 port, bool tls, int timeout_ms,
                     const char *method, const std::string &path,
                     const std::string &extra_headers, const std::string &body) {
        Response r;
        r.conn = open(host, port, tls, timeout_ms);
        if (!conn_ok(r.conn)) return r;
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        setsockopt(r.conn.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

        const std::string req = build_request(method, path, host, extra_headers, body);
        if (conn_send(r.conn, req.data(), req.size()) < 0) { conn_close(r.conn); return r; }

        std::string raw; char buf[4096]; size_t he;
        while ((he = raw.find("\r\n\r\n")) == std::string::npos) {
            ssize_t n = conn_recv(r.conn, buf, sizeof buf);
            if (n <= 0) { conn_close(r.conn); return r; }
            raw.append(buf, n);
        }
        r.status   = parse_status(raw.c_str());
        r.headers  = raw.substr(0, he);
        r.residual = raw.substr(he + 4);
        return r;
    }

    std::string auth_header(const char *token, bool with_token) {
        // DeviceId is the overlay's Quick Connect device identity; the sysmodule
        // reuses the same (harmless) string — Jellyfin only needs the Token.
        std::string a = "Authorization: MediaBrowser Client=\"Streamfin\", Device=\"Switch\", "
                        "DeviceId=\"streamfin-overlay\", Version=\"0.1\"";
        if (with_token && token) { a += ", Token=\""; a += token; a += "\""; }
        return a;
    }

    std::string build_request(const char *method, const std::string &path, const char *host,
                              const std::string &extra_headers, const std::string &body) {
        std::string r = std::string(method) + " " + path + " HTTP/1.1\r\n";
        r += "Host: "; r += (host ? host : ""); r += "\r\n";
        r += extra_headers;                 // each line already CRLF-terminated
        r += "User-Agent: Streamfin\r\n";
        if (!body.empty()) {
            r += "Content-Type: application/json\r\n";
            r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        }
        r += "Connection: close\r\n\r\n";
        r += body;
        return r;
    }

    int parse_status(const char *raw) {
        int status = 0;
        std::sscanf(raw, "HTTP/%*d.%*d %d", &status);
        return status;
    }

    long parse_content_range_total(const std::string &headers) {
        auto cr = headers.find("Content-Range:");
        if (cr == std::string::npos) return -1;
        auto sl = headers.find('/', cr);
        if (sl == std::string::npos) return -1;
        return std::strtol(headers.c_str() + sl + 1, nullptr, 10);
    }

}
