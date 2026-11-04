#pragma once
// a2gspu_features.hpp -- versioned CTRL protocol extensions.
//
// `protocol` answers the MAJOR version (version=2) and, since minor 1, the
// MINOR version (minor=N). A client that wants to know whether an additive
// extension exists asks `features`, which answers on one line without writing a
// file:
//
//     status=OK features protocol=2.1 count=1 list=features/1
//
// and `features <name>` answers one row (or refuses an unknown name).
//
// RULE FOR LATER ADDITIONS. Every additive extension is one row below. Its
// `since` is the protocol minor that introduced it, and the minor the rail
// reports is the LARGEST `since` in the table -- so adding a row with the next
// minor is the whole version bump, and the two cannot drift apart. A change
// that breaks an existing row's contract bumps that row's `version` instead.
// Names are lower-case words joined by '-', unique in the table.
//
// Self-contained on purpose (no emulator headers) so the unit test in
// apps/featurestest can check the table and the exact reply text offline.

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace a2features {

constexpr int kProtocolMajor = 2;

struct Feature {
    const char *name;     // extension name, as listed by `features`
    int         version;  // contract version of this extension (starts at 1)
    int         since;    // protocol minor that introduced it
    const char *summary;  // one line, no single quotes
};

inline const Feature *features(int &count) {
    static const Feature rows[] = {
        {"features", 1, 1, "this query: protocol minor version and the list of versioned extensions"},
        {"setreg-banks", 1, 2, "setreg dbr and setreg pbr set the data and program bank registers ($00-$FF)"},
        {"assert-registers", 1, 3, "assert reg:a|x|y|s|d|p|dbr|pbr|pc|e with an optional &MASK compares CPU registers and masked flags"},
    };
    count = (int)(sizeof rows / sizeof rows[0]);
    return rows;
}

inline int protocol_minor() {
    int n = 0, minor = 0;
    const Feature *r = features(n);
    for (int i = 0; i < n; i++)
        if (r[i].since > minor) minor = r[i].since;
    return minor;
}

inline const Feature *find(const char *name) {
    int n = 0;
    const Feature *r = features(n);
    for (int i = 0; i < n; i++)
        if (!std::strcmp(r[i].name, name)) return &r[i];
    return nullptr;
}

// `features` -> "status=OK features protocol=2.<minor> count=<n> list=<name>/<version>,..."
inline void format_list(char *buf, size_t bufsz) {
    int n = 0;
    const Feature *r = features(n);
    size_t used = 0;
    int w = std::snprintf(buf, bufsz, "status=OK features protocol=%d.%d count=%d list=",
                          kProtocolMajor, protocol_minor(), n);
    if (w < 0) return;
    used = (size_t)w;
    for (int i = 0; i < n && used < bufsz; i++) {
        w = std::snprintf(buf + used, bufsz - used, "%s%s/%d", i ? "," : "", r[i].name, r[i].version);
        if (w < 0) return;
        used += (size_t)w;
    }
}

// `features <name>` -> one row, or a refusal naming the unknown extension.
inline void format_one(const char *name, char *buf, size_t bufsz) {
    const Feature *f = find(name);
    if (!f)
        std::snprintf(buf, bufsz, "status=FAIL features-unknown '%s' -- `features` lists every extension", name);
    else
        std::snprintf(buf, bufsz, "status=OK features name=%s version=%d since=%d.%d summary='%s'",
                      f->name, f->version, kProtocolMajor, f->since, f->summary);
}

// The tail of the `protocol` reply: " minor=<minor> extensions='<name>,<name>,...'"
// with every row of `rows`, in table order, and the minor taken from the same
// rows. The live reply passes the table above, so `protocol` names exactly the
// extensions `features` lists and a new row reaches both without another edit.
// Truncates to fit the buffer.
inline void format_protocol_tail(const Feature *rows, int n, char *buf, size_t bufsz) {
    if (!bufsz) return;
    buf[0] = '\0';
    int minor = 0;
    for (int i = 0; i < n; i++)
        if (rows[i].since > minor) minor = rows[i].since;
    int w = std::snprintf(buf, bufsz, " minor=%d extensions='", minor);
    if (w < 0) return;
    size_t used = (size_t)w;
    for (int i = 0; i < n && used < bufsz; i++) {
        w = std::snprintf(buf + used, bufsz - used, "%s%s", i ? "," : "", rows[i].name);
        if (w < 0) return;
        used += (size_t)w;
    }
    if (used < bufsz) std::snprintf(buf + used, bufsz - used, "'");
}

inline void format_protocol_tail(char *buf, size_t bufsz) {
    int n = 0;
    const Feature *r = features(n);
    format_protocol_tail(r, n, buf, bufsz);
}

} // namespace a2features
