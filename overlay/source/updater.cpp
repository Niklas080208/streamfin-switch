#include "updater.hpp"
#include "ovl_log.hpp"

#include <download_funcs.hpp>

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace updater {

    namespace {
        constexpr const char *API_URL =
            "https://api.github.com/repos/dammitjeff/streamfin-switch/releases/latest";
        constexpr const char *TMP_PATH = "sdmc:/config/streamfin/.update_check.json";

        Thread  g_thread;
        Mutex   g_mtx;
        CondVar g_cv;
        bool    g_run     = false;
        bool    g_have    = false;
        bool    g_request = false;
        State   g_state   = State::Idle;
        char    g_latest[32] = {};

        // "v1.2.3" / "1.2.3-foo" -> {1,2,3}, ignoring anything past the patch.
        void parse_ver(const char *s, int v[3]) {
            v[0] = v[1] = v[2] = 0;
            if (!s) return;
            if (*s == 'v' || *s == 'V') s++;
            std::sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
        }

        int cmp_ver(const char *a, const char *b) {
            int va[3], vb[3];
            parse_ver(a, va);
            parse_ver(b, vb);
            for (int i = 0; i < 3; i++)
                if (va[i] != vb[i]) return va[i] - vb[i];
            return 0;
        }

        // Pull the "tag_name" string out of the releases JSON without a parser.
        bool extract_tag(const char *json, char *out, size_t out_sz) {
            const char *k = std::strstr(json, "\"tag_name\"");
            if (!k) return false;
            const char *c = std::strchr(k, ':');
            if (!c) return false;
            const char *q = std::strchr(c, '"');
            if (!q) return false;
            q++;
            const char *e = std::strchr(q, '"');
            if (!e || (size_t)(e - q) >= out_sz) return false;
            std::memcpy(out, q, e - q);
            out[e - q] = '\0';
            return true;
        }

        bool fetch_latest_tag(char *out, size_t out_sz) {
            ult::createDirectory("sdmc:/config/streamfin/");   // ensure the temp dir exists
            // noSocketInit=true: reuse the overlay's already-open socket.
            // noPercentagePolling=true: small JSON, no progress bar needed.
            if (!ult::downloadFile(API_URL, TMP_PATH, true, true))
                return false;

            FILE *f = std::fopen(TMP_PATH, "rb");
            if (!f) return false;
            std::string body;
            char buf[1024];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
                body.append(buf, n);
                if (body.size() > 256 * 1024) break;   // releases JSON is small; cap it
            }
            std::fclose(f);
            std::remove(TMP_PATH);

            return extract_tag(body.c_str(), out, out_sz);
        }

        void worker(void *) {
            while (true) {
                mutexLock(&g_mtx);
                while (g_run && !g_request) condvarWait(&g_cv, &g_mtx);
                if (!g_run) { mutexUnlock(&g_mtx); return; }
                g_request = false;
                g_state   = State::Checking;
                mutexUnlock(&g_mtx);

                char tag[32] = {};
                bool ok = fetch_latest_tag(tag, sizeof tag);

                mutexLock(&g_mtx);
                if (!ok) {
                    g_state = State::Failed;
                    g_latest[0] = '\0';
                    ovl_log::Line("updater: check failed");
                } else {
                    std::snprintf(g_latest, sizeof g_latest, "%s", tag);
                    g_state = (cmp_ver(tag, VERSION) > 0) ? State::Available : State::UpToDate;
                    ovl_log::Line("updater: latest %s, current %s -> %s",
                                  tag, VERSION, g_state == State::Available ? "update" : "current");
                }
                mutexUnlock(&g_mtx);
            }
        }
    }

    void Init() {
        if (g_have) return;
        mutexInit(&g_mtx);
        condvarInit(&g_cv);
        g_run = true;
        // 128KB stack: curl + the mbedTLS handshake are stack-hungry.
        if (R_SUCCEEDED(threadCreate(&g_thread, worker, nullptr, nullptr, 0x20000, 0x3B, -2))) {
            threadStart(&g_thread);
            g_have = true;
        } else {
            g_run = false;
            ovl_log::Line("updater: worker FAILED to start");
        }
    }

    void Exit() {
        if (!g_have) return;
        mutexLock(&g_mtx);
        g_run = false;
        condvarWakeAll(&g_cv);
        mutexUnlock(&g_mtx);
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_have = false;
    }

    void Check() {
        if (!g_have) return;
        mutexLock(&g_mtx);
        if (g_state != State::Checking) {
            g_request = true;
            g_state   = State::Checking;
            condvarWakeAll(&g_cv);
        }
        mutexUnlock(&g_mtx);
    }

    State Poll(char *latest, size_t latest_sz) {
        State s;
        mutexLock(&g_mtx);
        s = g_state;
        if (latest_sz) std::snprintf(latest, latest_sz, "%s", g_latest);
        mutexUnlock(&g_mtx);
        return s;
    }

}
