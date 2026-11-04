// featurestest -- the versioned-extension table behind the `features` CTRL verb.
//
// Two kinds of check:
//   * table invariants: names unique and well formed, versions >= 1, every
//     `since` within 1..minor, and the reported minor equal to the largest
//     `since` (the rule that makes adding a row the whole version bump);
//   * exact reply text against a hand-written fixture
//     (expected_replies.txt), so a change to the wire format or to the table
//     shows up as a fixture diff that has to be made on purpose.
//
// usage: featurestest <expected_replies.txt>
#include "../../src/a2gspu_features.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const std::string &what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; }
}

static bool well_formed(const char *name) {
    if (!name || !*name || *name == '-') return false;
    for (const char *p = name; *p; ++p)
        if (!(std::islower((unsigned char)*p) || std::isdigit((unsigned char)*p) || *p == '-')) return false;
    return name[std::strlen(name) - 1] != '-';
}

int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "usage: featurestest <expected_replies.txt>\n"; return 2; }

    int n = 0;
    const a2features::Feature *rows = a2features::features(n);
    const int minor = a2features::protocol_minor();
    check(n >= 1, "table has at least one row");
    check(minor >= 1, "protocol minor is at least 1");
    std::set<std::string> seen;
    int max_since = 0;
    for (int i = 0; i < n; ++i) {
        const auto &f = rows[i];
        check(well_formed(f.name), std::string("name is lower-case words joined by '-': ") + f.name);
        check(seen.insert(f.name).second, std::string("name is unique: ") + f.name);
        check(f.version >= 1, std::string("version >= 1: ") + f.name);
        check(f.since >= 1 && f.since <= minor, std::string("since within 1..minor: ") + f.name);
        check(f.summary && *f.summary && !std::strchr(f.summary, '\''), std::string("summary present, no single quote: ") + f.name);
        check(a2features::find(f.name) == &f, std::string("find() returns the row: ") + f.name);
        if (f.since > max_since) max_since = f.since;
    }
    check(minor == max_since, "minor equals the largest since");
    check(a2features::find("features") != nullptr, "the query lists itself");
    check(a2features::find("no-such-extension") == nullptr, "unknown name is not found");

    // Exact replies against the fixture: line 1 = `features`,
    // line 2 = `features features`, line 3 = `features batch` (unknown).
    std::ifstream in(argv[1]);
    check(bool(in), std::string("fixture opens: ") + argv[1]);
    std::vector<std::string> want;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') want.push_back(line);
    }
    check(want.size() == 3, "fixture holds three replies");
    char buf[512];
    std::vector<std::string> got;
    a2features::format_list(buf, sizeof buf);               got.push_back(buf);
    a2features::format_one("features", buf, sizeof buf);    got.push_back(buf);
    a2features::format_one("batch", buf, sizeof buf);       got.push_back(buf);
    for (size_t i = 0; i < got.size() && i < want.size(); ++i)
        check(got[i] == want[i], "reply " + std::to_string(i + 1) + "\n  got:  " + got[i] + "\n  want: " + want[i]);

    // A buffer too small must truncate, not overrun.
    char tiny[16];
    std::memset(tiny, 'X', sizeof tiny);
    a2features::format_list(tiny, 8);
    check(std::strlen(tiny) < 8 && tiny[15] == 'X', "format_list respects the buffer size");

    if (failures) { std::cerr << failures << " check(s) failed\n"; return 1; }
    std::cout << "featurestest: OK (" << n << " extension(s), protocol " << a2features::kProtocolMajor
              << "." << minor << ")\n";
    return 0;
}
