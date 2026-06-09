#include "jelly_net.hpp"
#include "jelly_http.hpp"
#include "jelly_config.hpp"

#include <switch.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <new>

namespace jelly {

    // Re-read the live server/token from the SD config (delegates to the shared
    // jelly_cfg state). Called per track open so a sign-in via the overlay takes
    // effect without a reboot.
    void LoadConfig() {
        jelly_cfg::Reload();
    }

    // ---- persistent streaming reader ----
    Stream::Stream(const char *path) : m_path(path) {
        m_ring = new (std::nothrow) u8[RING];
        mutexInit(&m_mtx);
        condvarInit(&m_cv_data);
        condvarInit(&m_cv_space);
    }

    Stream::~Stream() {
        shutdown();
        delete[] m_ring;
    }

    // Ring-buffer byte moves with wraparound. Caller holds m_mtx and adjusts
    // m_count; these just shuttle bytes and advance the head/tail index.
    void Stream::ring_put(const u8 *src, size_t n) {
        size_t first = RING - m_tail; if (first > n) first = n;
        std::memcpy(m_ring + m_tail, src, first);
        std::memcpy(m_ring, src + first, n - first);
        m_tail = (m_tail + n) % RING;
    }
    void Stream::ring_get(u8 *dst, size_t n) {
        size_t first = RING - m_head; if (first > n) first = n;
        std::memcpy(dst, m_ring + m_head, first);
        std::memcpy(dst + first, m_ring, n - first);
        m_head = (m_head + n) % RING;
    }

    // Background producer: pull bytes off the connection into the ring buffer.
    void Stream::reader() {
        u8 tmp[16384];
        while (true) {
            ssize_t n = jelly_http::conn_recv(m_conn, tmp, sizeof tmp);
            if (n <= 0) {  // EOF (Connection: close), error, or socket closed by shutdown()
                mutexLock(&m_mtx);
                m_eof = true;
                condvarWakeAll(&m_cv_data);
                mutexUnlock(&m_mtx);
                return;
            }
            size_t off = 0;
            while (off < (size_t)n) {
                mutexLock(&m_mtx);
                while (m_count == RING && !m_stop) condvarWait(&m_cv_space, &m_mtx);
                if (m_stop) { mutexUnlock(&m_mtx); return; }
                size_t put = RING - m_count;
                if (put > (size_t)n - off) put = (size_t)n - off;
                ring_put(tmp + off, put);
                m_count += put;
                off += put;
                condvarWakeAll(&m_cv_data);
                mutexUnlock(&m_mtx);
            }
        }
    }

    void Stream::shutdown() {
        if (m_have_thread) {
            mutexLock(&m_mtx);
            m_stop = true;
            condvarWakeAll(&m_cv_space);
            condvarWakeAll(&m_cv_data);
            mutexUnlock(&m_mtx);
            jelly_http::conn_interrupt(m_conn);   // unblock the reader's conn_recv
            threadWaitForExit(&m_thread);
            threadClose(&m_thread);
            jelly_http::conn_close(m_conn);        // free the TLS ctx now the reader is gone
            m_have_thread = false;
        } else {
            jelly_http::conn_close(m_conn);
        }
        m_head = m_tail = m_count = 0;
        m_eof = m_stop = false;
    }

    bool Stream::open(long offset) {
        shutdown();
        if (!m_ring) return false;

        char range[48];
        std::snprintf(range, sizeof range, "Range: bytes=%ld-\r\n", offset);
        const std::string extra = jelly_http::auth_header(jelly_cfg::token(), true) + "\r\n" + range;
        jelly_http::Response r = jelly_http::request(jelly_cfg::host(), jelly_cfg::port(), jelly_cfg::tls(),
                                                     5000, "GET", m_path, extra, "");
        if (!jelly_http::conn_ok(r.conn)) return false;
        if (r.status != 200 && r.status != 206) { jelly_http::conn_close(r.conn); return false; }
        m_conn = r.conn;

        if (m_total < 0) {
            long total = jelly_http::parse_content_range_total(r.headers);
            if (total >= 0) {
                m_total = total;
            } else {
                auto cl = r.headers.find("Content-Length:");
                if (cl != std::string::npos) m_total = offset + std::strtol(r.headers.c_str() + cl + 15, nullptr, 10);
            }
        }

        size_t seed = r.residual.size() < RING ? r.residual.size() : RING;
        std::memcpy(m_ring, r.residual.data(), seed);
        m_tail = seed % RING;
        m_count = seed;
        m_head = 0;
        m_pos = offset;
        m_eof = m_stop = false;

        struct timeval zero = { 0, 0 };   // stream without a recv timeout
        setsockopt(m_conn.fd, SOL_SOCKET, SO_RCVTIMEO, &zero, sizeof zero);

        if (R_FAILED(threadCreate(&m_thread, reader_trampoline, this, nullptr, 0x8000, 0x20, -2))) {
            jelly_http::conn_close(m_conn); return false;
        }
        threadStart(&m_thread);
        m_have_thread = true;
        return true;
    }

    long Stream::total() {
        if (m_total >= 0) return m_total;
        if (open(0)) return m_total < 0 ? 0 : m_total;
        return 0;
    }

    long Stream::read(long offset, void *buf, long size) {
        // (Re)open on first use or on a non-sequential seek.
        if (!m_have_thread || offset != m_pos) {
            if (!open(offset)) return -1;
        }

        u8 *out = (u8 *)buf;
        long got = 0;
        while (got < size) {
            mutexLock(&m_mtx);
            while (m_count == 0 && !m_eof) condvarWait(&m_cv_data, &m_mtx);
            if (m_count == 0 && m_eof) { mutexUnlock(&m_mtx); break; }
            size_t take = m_count;
            if (take > (size_t)(size - got)) take = (size_t)(size - got);
            ring_get(out + got, take);
            m_count -= take;
            got += take;
            condvarWakeAll(&m_cv_space);
            mutexUnlock(&m_mtx);
        }
        m_pos += got;
        return got;
    }

}
