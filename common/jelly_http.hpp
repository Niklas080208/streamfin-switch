#pragma once

#include <string>
#include <switch.h>
#include <sys/types.h>

// Shared low-level Jellyfin HTTP transport, linked into BOTH binaries: the
// overlay (one-shot request/response) and the sysmodule (one long-lived streaming
// connection feeding a ring buffer). Only the connect/build/parse/auth primitives
// are shared here — each caller keeps its own read loop.
namespace jelly_http {

    // A connection: plain TCP (ssl == nullptr) or a TLS session (ssl = opaque ctx).
    struct Conn {
        int   fd  = -1;
        void *ssl = nullptr;
    };

    // Connect to host:port (connect timeout in ms). When tls, also run a TLS
    // handshake (SNI = host, no cert-chain verification). conn_ok() is false on
    // any failure, including empty host / port 0 ("not signed in yet").
    Conn open(const char *host, u16 port, bool tls, int timeout_ms);
    bool conn_ok(const Conn &c);

    ssize_t conn_send(Conn &c, const void *buf, size_t n);
    ssize_t conn_recv(Conn &c, void *buf, size_t n);
    void    conn_interrupt(Conn &c);   // close the socket to unblock a blocking recv; keeps the TLS ctx
    void    conn_close(Conn &c);        // full teardown: free TLS ctx + close socket

    // Open + send + read through end of headers; connection stays open at the
    // first body byte for the caller to drain, then conn_close(conn).
    struct Response {
        Conn        conn;
        int         status = 0;
        std::string headers;
        std::string residual;
    };
    Response request(const char *host, u16 port, bool tls, int timeout_ms,
                     const char *method, const std::string &path,
                     const std::string &extra_headers, const std::string &body = "");

    // The "Authorization: MediaBrowser ..." header LINE (no trailing CRLF).
    // with_token appends the saved access token.
    std::string auth_header(const char *token, bool with_token);

    // Assemble a raw HTTP/1.1 request. `extra_headers` carries any request-specific
    // header lines, each already CRLF-terminated (e.g. the auth line, a Range line).
    // When `body` is non-empty, JSON Content-Type + Content-Length are added.
    std::string build_request(const char *method, const std::string &path, const char *host,
                              const std::string &extra_headers, const std::string &body = "");

    // Status code from a raw "HTTP/x.y NNN ..." response (0 if unparseable).
    int parse_status(const char *raw);

    // Total size from a "Content-Range: bytes a-b/TOTAL" header within `headers`,
    // or -1 if absent.
    long parse_content_range_total(const std::string &headers);

}
