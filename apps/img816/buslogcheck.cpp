/**
 * buslogcheck: check an img816 bus access log (--buslog) on its own terms and
 * against hand-written expectations.
 *
 *   buslogcheck LOG [EXPECTED]
 *
 * For every case in LOG (written by `img816 --buslog`):
 *   - the records are numbered 0..n-1 and their CPU cycle numbers are
 *     consecutive, so there is exactly one record per CPU cycle;
 *   - the number of records equals the runner's own cycle count for the case
 *     (`end NAME runner_cyc=N`, the same count as the case line's cyc=);
 *   - no cycle pended a second access (`overlap=0` on the end line, when the
 *     logger reports it);
 *   - with a timing model: each record's c14m is base+wait+refresh+stretch,
 *     the next record starts where this one ended (t14m + c14m), the side is
 *     slow exactly when base is 14, each reason flag matches its column
 *     (refresh, stretch), and the c14m total equals the clock's own 14M delta
 *     for the case (`clock_c14m=`).
 * EXPECTED (optional) holds, per case, either the exact record lines
 * (`case NAME` ... `end`), compared line by line, or summary expectations
 * (`expect NAME key=value ...`, keys: n c14m fast slow fast_c14m slow_c14m
 * megaii io shadow slowmode refresh stretch fastrom). Every case named in
 * EXPECTED must be present in LOG.
 *
 * This program parses the log text itself; it shares no code with the logger.
 * Exit status: 0 all checks pass, 1 a check failed, 2 input error.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct Rec {
    std::string line;
    unsigned long long seq = 0, cycle = 0, t14m = 0;
    bool timed = false;
    std::string addr, kind, value, side, reasons;
    unsigned c14m = 0, base = 0, wait = 0, refresh = 0, stretch = 0;
};

struct Case {
    std::string name;
    std::vector<Rec> recs;
    bool ended = false;
    long long runner_cyc = -1;
    long long clock_c14m = -1;   // -1: no timing model
    long long overlap = -1;      // -1: not reported
};

static int failures = 0;
static void fail(const std::string &c, const std::string &what) {
    failures++;
    fprintf(stderr, "buslogcheck: %s: %s\n", c.c_str(), what.c_str());
}

static bool read_lines(const char *path, std::vector<std::string> &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::string all;
    char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
    fclose(f);
    std::istringstream in(all);
    std::string l;
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
    }
    return true;
}

static bool parse_rec(const std::string &l, Rec &r) {
    std::istringstream s(l);
    std::string t14m, c14m, base, wait, refresh, stretch;
    if (!(s >> r.seq >> r.cycle >> t14m >> r.addr >> r.kind >> r.value >> r.side >> c14m >> base >> wait >> refresh
            >> stretch >> r.reasons))
        return false;
    std::string extra;
    if (s >> extra) return false;
    r.line = l;
    r.timed = r.side != "-";
    if (r.timed) {
        r.t14m = strtoull(t14m.c_str(), nullptr, 10);
        r.c14m = (unsigned)strtoul(c14m.c_str(), nullptr, 10);
        r.base = (unsigned)strtoul(base.c_str(), nullptr, 10);
        r.wait = (unsigned)strtoul(wait.c_str(), nullptr, 10);
        r.refresh = (unsigned)strtoul(refresh.c_str(), nullptr, 10);
        r.stretch = (unsigned)strtoul(stretch.c_str(), nullptr, 10);
    }
    return true;
}

static bool has_reason(const Rec &r, const char *name) {
    std::string s = "," + r.reasons + ",";
    return s.find(std::string(",") + name + ",") != std::string::npos;
}

static std::map<std::string, unsigned long long> summary(const Case &c) {
    std::map<std::string, unsigned long long> m;
    const char *reasons[] = {"megaii", "io", "shadow", "slowmode", "refresh", "stretch", "fastrom"};
    m["n"] = c.recs.size();
    for (const char *k : {"c14m", "fast", "slow", "fast_c14m", "slow_c14m"}) m[k] = 0;
    for (const char *k : reasons) m[k] = 0;
    for (const Rec &r : c.recs) {
        m["c14m"] += r.c14m;
        if (r.side == "fast") { m["fast"]++; m["fast_c14m"] += r.c14m; }
        if (r.side == "slow") { m["slow"]++; m["slow_c14m"] += r.c14m; }
        for (const char *k : reasons) if (has_reason(r, k)) m[k]++;
    }
    return m;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: buslogcheck LOG [EXPECTED]\n");
        return 2;
    }
    std::vector<std::string> lines;
    if (!read_lines(argv[1], lines)) { fprintf(stderr, "buslogcheck: cannot open %s\n", argv[1]); return 2; }

    std::vector<Case> cases;
    std::map<std::string, size_t> by_name;
    bool seen_header = false;
    Case *cur = nullptr;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string &l = lines[i];
        if (l.empty()) continue;
        if (l[0] == '#') {
            if (!l.compare(0, 11, "# buslog v1")) seen_header = true;
            continue;
        }
        if (!l.compare(0, 5, "case ")) {
            if (cur && !cur->ended) { fprintf(stderr, "buslogcheck: line %zu: case inside a case\n", i + 1); return 2; }
            cases.push_back(Case());
            cur = &cases.back();
            cur->name = l.substr(5);
            by_name[cur->name] = cases.size() - 1;
            continue;
        }
        if (!cur || cur->ended) { fprintf(stderr, "buslogcheck: line %zu: record outside a case\n", i + 1); return 2; }
        if (!l.compare(0, 4, "end ")) {
            std::istringstream s(l);
            std::string kw, name, kv;
            s >> kw >> name;
            while (s >> kv) {
                size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                if (k == "runner_cyc") cur->runner_cyc = atoll(v.c_str());
                if (k == "clock_c14m" && v != "-") cur->clock_c14m = atoll(v.c_str());
                if (k == "overlap") cur->overlap = atoll(v.c_str());
            }
            if (name != cur->name) fail(cur->name, "end names another case: " + name);
            cur->ended = true;
            continue;
        }
        Rec r;
        if (!parse_rec(l, r)) { fprintf(stderr, "buslogcheck: line %zu: bad record: %s\n", i + 1, l.c_str()); return 2; }
        cur->recs.push_back(r);
    }
    if (!seen_header) { fprintf(stderr, "buslogcheck: no '# buslog v1' header\n"); return 2; }
    if (cur && !cur->ended) { fprintf(stderr, "buslogcheck: last case has no end line\n"); return 2; }

    // Self-consistency of every case.
    for (const Case &c : cases) {
        char m[256];
        if (c.runner_cyc < 0) fail(c.name, "no runner_cyc on the end line");
        if (c.overlap > 0) {
            snprintf(m, sizeof m, "%lld cycle(s) pended a second bus access (overlap)", c.overlap);
            fail(c.name, m);
        }
        else if ((long long)c.recs.size() != c.runner_cyc) {
            snprintf(m, sizeof m, "%zu records but the runner counted %lld cycles", c.recs.size(), c.runner_cyc);
            fail(c.name, m);
        }
        unsigned long long sum = 0;
        bool timed = !c.recs.empty() && c.recs[0].timed;
        for (size_t i = 0; i < c.recs.size(); i++) {
            const Rec &r = c.recs[i];
            if (r.seq != i) { snprintf(m, sizeof m, "record %zu has seq %llu", i, r.seq); fail(c.name, m); }
            if (i && r.cycle != c.recs[i - 1].cycle + 1) {
                snprintf(m, sizeof m, "record %zu: cycle %llu does not follow %llu", i, r.cycle, c.recs[i - 1].cycle);
                fail(c.name, m);
            }
            if (r.timed != timed) { snprintf(m, sizeof m, "record %zu: timing model comes and goes", i); fail(c.name, m); }
            if (!r.timed) continue;
            sum += r.c14m;
            if (r.c14m != r.base + r.wait + r.refresh + r.stretch) {
                snprintf(m, sizeof m, "record %zu: c14m %u != base+wait+refresh+stretch", i, r.c14m); fail(c.name, m);
            }
            if (i + 1 < c.recs.size() && c.recs[i + 1].t14m != r.t14m + r.c14m) {
                snprintf(m, sizeof m, "record %zu: next starts at %llu, not %llu", i, c.recs[i + 1].t14m, r.t14m + r.c14m);
                fail(c.name, m);
            }
            if ((r.side == "slow") != (r.base == 14)) { snprintf(m, sizeof m, "record %zu: side %s with base %u", i, r.side.c_str(), r.base); fail(c.name, m); }
            if ((r.refresh != 0) != has_reason(r, "refresh")) { snprintf(m, sizeof m, "record %zu: refresh column and reason disagree", i); fail(c.name, m); }
            if ((r.stretch != 0) != has_reason(r, "stretch")) { snprintf(m, sizeof m, "record %zu: stretch column and reason disagree", i); fail(c.name, m); }
            if (r.side == "slow" && r.refresh) { snprintf(m, sizeof m, "record %zu: a slow cycle carries a refresh", i); fail(c.name, m); }
        }
        if (timed && c.clock_c14m >= 0 && (long long)sum != c.clock_c14m) {
            snprintf(m, sizeof m, "c14m total %llu but the clock advanced %lld", sum, c.clock_c14m);
            fail(c.name, m);
        }
        if (timed && c.clock_c14m < 0) fail(c.name, "timed records but no clock_c14m on the end line");
    }

    long expectations = 0;
    if (argc == 3) {
        std::vector<std::string> ex;
        if (!read_lines(argv[2], ex)) { fprintf(stderr, "buslogcheck: cannot open %s\n", argv[2]); return 2; }
        for (size_t i = 0; i < ex.size(); i++) {
            const std::string &l = ex[i];
            if (l.empty() || l[0] == '#') continue;
            if (!l.compare(0, 7, "expect ")) {
                std::istringstream s(l);
                std::string kw, name, kv;
                s >> kw >> name;
                auto it = by_name.find(name);
                if (it == by_name.end()) { fail(name, "expected case is not in the log"); continue; }
                std::map<std::string, unsigned long long> sm = summary(cases[it->second]);
                while (s >> kv) {
                    size_t eq = kv.find('=');
                    std::string k = kv.substr(0, eq);
                    if (eq == std::string::npos || !sm.count(k)) { fprintf(stderr, "buslogcheck: expected line %zu: bad key %s\n", i + 1, kv.c_str()); return 2; }
                    unsigned long long want = strtoull(kv.c_str() + eq + 1, nullptr, 10);
                    expectations++;
                    if (sm[k] != want) {
                        char m[160];
                        snprintf(m, sizeof m, "%s=%llu, expected %llu", k.c_str(), sm[k], want);
                        fail(name, m);
                    }
                }
                continue;
            }
            if (!l.compare(0, 5, "case ")) {
                std::string name = l.substr(5);
                std::vector<std::string> want;
                size_t j = i + 1;
                for (; j < ex.size() && ex[j] != "end"; j++)
                    if (!ex[j].empty() && ex[j][0] != '#') want.push_back(ex[j]);
                if (j == ex.size()) { fprintf(stderr, "buslogcheck: expected case %s has no end\n", name.c_str()); return 2; }
                i = j;
                auto it = by_name.find(name);
                if (it == by_name.end()) { fail(name, "expected case is not in the log"); continue; }
                const Case &c = cases[it->second];
                size_t n = want.size() > c.recs.size() ? want.size() : c.recs.size();
                for (size_t k = 0; k < n; k++) {
                    const std::string got = k < c.recs.size() ? c.recs[k].line : "(missing)";
                    const std::string w = k < want.size() ? want[k] : "(missing)";
                    expectations++;
                    if (got != w) fail(name, "record " + std::to_string(k) + " differs\n  got  " + got + "\n  want " + w);
                }
                continue;
            }
            fprintf(stderr, "buslogcheck: expected line %zu: unknown directive\n", i + 1);
            return 2;
        }
    }
    fprintf(stderr, "buslogcheck: cases=%zu expectations=%ld failures=%d\n", cases.size(), expectations, failures);
    return failures ? 1 : 0;
}
