#pragma once
// ============================================================================
// a2gspu_trap.hpp -- A2GSPU_TRAP: PC traps that are configuration, not code.
//
// WHY THIS EXISTS
//
// The fork already had two kinds of PC trap and they were opposite shapes.
// A2GSPU_PCTRAP is a generic trigger with a fixed payload: first entry to a
// range, dump the registers and the PC ring, once. The three WIZ5 traps in
// wiz5_config.hpp are the reverse: hard-coded triggers with bespoke payloads
// (a decode table at $0D00 at driver entry, an output window at driver exit,
// the eval stack when a p-code sub-opcode is 6, disk blocks when X==1). Neither
// could express the other, so every new "dump X when the guest reaches Y" was
// a C++ change -- and per-project C++ is the one thing a fork meant to serve
// every project cannot keep accumulating.
//
// This rail is the general form of both. A trap is a TRIGGER (a PC, optionally
// a bank, optionally an exit PC to pair with), a CONDITION (expressions over
// registers and memory), a POLICY (once / every / first N per key), and a
// PAYLOAD (what to record). All of it comes from the environment. The three
// WIZ5 traps are written out as specs in Docs/A2GSPU_TRAP.md, which is the
// proof that nothing was lost in the generalization.
//
// GRAMMAR (one spec; several are separated by ';' in A2GSPU_TRAP, or one per
// line in A2GSPU_TRAP_FILE, '#' comments allowed)
//
//   name=<id>              tag on every record          (default trapN)
//   at=<pc>[-<pc>]         entry trigger: PC or range.  <pc> is HHHH (any bank,
//                          16-bit compare -- interpreter traps are bank-agnostic)
//                          or BB:HHHH (that bank only)
//   exit=<pc>              paired exit trigger; the exit record carries the
//                          entry's sequence number. One outstanding entry at a
//                          time: a second entry before the exit replaces it.
//   when=<cond>[,<cond>]   all must hold at entry.  cond = <expr>OP<expr>
//                          with OP one of == != < > <= >=, or <expr>~{v,v,..}
//                          ("is one of"). No spaces anywhere in a value.
//   once | every | first=N[/<expr>]
//                          once  : record the first qualifying hit only (default)
//                          every : record every qualifying hit
//                          first : record every hit, but attach the heavy=
//                                  payload only to the first N hits per distinct
//                                  value of <expr> (or the first N overall)
//   cap=N                  stop recording after N records (default 20000);
//                          hits are still counted for the close-out summary
//   do=<payload>[,...]     recorded on every entry record
//   heavy=<payload>[,...]  recorded on entry only when the policy allows
//   xdo=<payload>[,...]    recorded on every exit record
//   xheavy=<payload>[,...] recorded on exit when the paired entry was heavy
//   out=<file>             this spec's NDJSON sink (default A2GSPU_TRAP_OUT,
//                          else stdout, prefixed "IIGS TRAP: ")
//
// PAYLOADS
//
//   regs                   a x y s d dbr pbr pc p e (decimal)
//   ring[=N]               the last N instruction PCs, oldest first (max 256).
//                          The trap keeps its own ring; it does not arm BRKMEM.
//   zp[=LO-HI]             bank-0 bytes LO..HI as hex (default 00-FF)
//   stack[=N]              N bytes above S (default 32). Page-1 wrap in
//                          emulation mode or when S is 8-bit; 16-bit otherwise.
//   mem=<expr>:<len>       <len> (decimal, max 4096) bytes at the 24-bit address
//                          <expr>, as hex. The address can be computed:
//                          mem=m16(s16(1)+2):16 is "16 bytes at the pointer that
//                          follows the JSR return address", i.e. an MLI param block.
//   field=<name>:<expr>    one scalar, under "f"
//   text                   the 40x24 text page as one string
//   scan=<src>:<lo>-<hi>:<n>:<max>
//                          for every little-endian 16-bit value in window <src>
//                          (zp or stack, as configured above) that lands in
//                          [lo,hi], dump <n> bytes at it; at most <max> entries.
//                          This is the CSP6 trap's "chase every pointer".
//
// EXPRESSIONS (numbers are hex; a leading $ or 0x forces a number, which
// matters for a, d and e: bare they are registers, `$a` `$d` `$e` are digits)
//
//   registers  a x y s d dbr pbr pc fpc p e
//   memory     m8(e) m16(e) m24(e)   byte/word/24-bit at 24-bit address e
//              z8(e) z16(e)          bank-0 byte/word at e (zero page, or any
//                                    16-bit address)
//              s8(e) s16(e)          byte/word at S+e (same wrap rule as stack)
//   arithmetic e + e, e - e, parentheses
//
// GUARANTEES
//
//   * Off by default. With no A2GSPU_TRAP* set, g_trap_on is false and the CPU
//     loop pays one untaken branch. Stock behaviour is byte-identical.
//   * Observe-don't-disturb. Reads of I/O space ($C000-$CFFF in banks 00/01/E0/E1)
//     go through probe_peek and never fire a soft switch; every other read is
//     mmu->read, which is the true content of RAM (probe_peek returns floating
//     bus for the handler-paged banks $00/$01 on the IIgs, see mmu_iigs.hpp).
//     A trap never writes.
//   * Loud. A malformed spec is fatal at arm time, with the spec and the reason
//     on stderr -- an instrument that silently ignores its configuration is the
//     failure mode this fork exists to remove. Every armed spec is echoed at
//     arm time with its resolved trigger and sink. Every spec prints a summary
//     at close, including records=0, so silence is never mistaken for success.
//   * Every record carries frame and cycle stamps.
//
// WHAT IT DOES NOT DO (yet)
//
//   * Triggers other than PC (a store to an address, S entering a range): use
//     A2GSPU_WATCH / STACKTRAP. Adding trigger kinds here is the natural next
//     step and the parser is written so `at=` is one of several.
//   * Live (CTRL) arming. Launch-time only, like the other trace rails.
//   * The A2GSPU_PCTRAP one-shot is untouched and still works; `once do=regs,ring`
//     is its equivalent here.
// ============================================================================
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

#include "cpu.hpp"
#include "mmus/mmu.hpp"
#include "iigs_toolbox.hpp"    // g_iigs_cur_frame (headless frame counter)
#include "generic_tap.hpp"     // gtap_page_json (the 40-col text page as JSON)

namespace a2trap {

// ---------------------------------------------------------------- helpers ---
[[noreturn]] inline void fail(const std::string &spec, const std::string &why) {
    fprintf(stderr, "A2GSPU_TRAP: FAIL -- %s\n  in spec: %s\n", why.c_str(), spec.c_str());
    fflush(stderr);
    exit(2);
}

inline uint8_t peek(cpu_state *cpu, uint32_t a) {
    a &= 0xFFFFFF;
    const uint32_t bank = a >> 16, lo = a & 0xFFFF;
    const bool io_bank = (bank == 0x00 || bank == 0x01 || bank == 0xE0 || bank == 0xE1);
    if (io_bank && lo >= 0xC000 && lo <= 0xCFFF) return cpu->mmu->probe_peek(a);   // never fire a soft switch
    return cpu->mmu->read(a);
}

// Address of the k-th byte above S, with the same wrap rule everywhere.
inline uint32_t stack_addr(cpu_state *cpu, uint32_t k) {
    const bool page1 = (cpu->E != 0) || (cpu->sp < 0x100);
    if (page1) return 0x0100 | ((cpu->sp + k) & 0xFF);
    return (cpu->sp + k) & 0xFFFF;
}

// ------------------------------------------------------------ expressions ---
enum Reg { R_A, R_X, R_Y, R_S, R_D, R_DBR, R_PBR, R_PC, R_FPC, R_P, R_E };
enum Fn  { F_M8, F_M16, F_M24, F_Z8, F_Z16, F_S8, F_S16 };

struct Node {
    enum Kind { NUM, REG, FN, ADD, SUB } kind;
    uint32_t num = 0;
    int reg = 0, fn = 0;
    int a = -1, b = -1;
};

struct Expr {
    std::string src;
    std::vector<Node> nodes;
    int root = -1;
    bool empty() const { return root < 0; }
};

struct Parser {
    const char *p;
    const std::string &spec;
    Expr &e;
    Parser(const char *s, const std::string &sp, Expr &ex) : p(s), spec(sp), e(ex) {}

    void ws() { while (*p == ' ') p++; }
    int push(Node n) { e.nodes.push_back(n); return (int)e.nodes.size() - 1; }

    int parse_term() {
        ws();
        if (*p == '(') {
            p++; int r = parse_expr(); ws();
            if (*p != ')') fail(spec, "expected ')' in expression '" + e.src + "'");
            p++; return r;
        }
        // A '$' or '0x' prefix forces a number: `a`, `d`, `e` are registers, `$a`,
        // `$d`, `$e` are the hex digits.
        bool forced = false;
        if (*p == '$') { p++; forced = true; }
        else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; forced = true; }
        // identifier?
        if (!forced && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
            // but a bare hex number can also start with a-f: an identifier is a
            // known name followed by '(' or a non-hex-alnum boundary
            const char *q = p; std::string id;
            while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9')) { id += (char)tolower(*q); q++; }
            static const struct { const char *n; int fn; } fns[] = {
                {"m8", F_M8}, {"m16", F_M16}, {"m24", F_M24}, {"z8", F_Z8}, {"z16", F_Z16}, {"s8", F_S8}, {"s16", F_S16}};
            static const struct { const char *n; int reg; } regs[] = {
                {"a", R_A}, {"x", R_X}, {"y", R_Y}, {"s", R_S}, {"d", R_D}, {"dbr", R_DBR},
                {"pbr", R_PBR}, {"pc", R_PC}, {"fpc", R_FPC}, {"p", R_P}, {"e", R_E}};
            if (*q == '(') {
                for (auto &f : fns) if (id == f.n) {
                    p = q + 1; int arg = parse_expr(); ws();
                    if (*p != ')') fail(spec, "expected ')' after " + id + "(");
                    p++;
                    Node n; n.kind = Node::FN; n.fn = f.fn; n.a = arg; return push(n);
                }
                fail(spec, "unknown function '" + id + "' in '" + e.src + "'");
            }
            for (auto &r : regs) if (id == r.n) {
                p = q; Node n; n.kind = Node::REG; n.reg = r.reg; return push(n);
            }
            // fall through: treat as hex if it is all hex digits
            bool allhex = !id.empty();
            for (char c : id) if (!isxdigit((unsigned char)c)) allhex = false;
            if (!allhex) fail(spec, "unknown name '" + id + "' in '" + e.src + "'");
        }
        if (!isxdigit((unsigned char)*p)) fail(spec, "expected a number, register or function in '" + e.src + "'");
        char *end = nullptr;
        uint32_t v = (uint32_t)strtoul(p, &end, 16);
        p = end;
        Node n; n.kind = Node::NUM; n.num = v; return push(n);
    }
    int parse_expr() {
        int lhs = parse_term();
        for (;;) {
            ws();
            if (*p == '+' || *p == '-') {
                char op = *p++; int rhs = parse_term();
                Node n; n.kind = (op == '+') ? Node::ADD : Node::SUB; n.a = lhs; n.b = rhs; lhs = push(n);
            } else return lhs;
        }
    }
};

inline Expr parse_expr(const std::string &src, const std::string &spec) {
    Expr e; e.src = src;
    Parser ps(src.c_str(), spec, e);
    e.root = ps.parse_expr();
    ps.ws();
    if (*ps.p) fail(spec, "trailing text '" + std::string(ps.p) + "' in expression '" + src + "'");
    return e;
}

inline uint32_t eval(const Expr &e, int i, cpu_state *cpu) {
    const Node &n = e.nodes[i];
    switch (n.kind) {
    case Node::NUM: return n.num;
    case Node::REG:
        switch (n.reg) {
        case R_A: return cpu->a;  case R_X: return cpu->x;  case R_Y: return cpu->y;
        case R_S: return cpu->sp; case R_D: return cpu->d;  case R_DBR: return cpu->db;
        case R_PBR: return (cpu->full_pc >> 16) & 0xFF;
        case R_PC: return cpu->full_pc & 0xFFFF;   case R_FPC: return cpu->full_pc & 0xFFFFFF;
        case R_P: return cpu->p;  case R_E: return cpu->E;
        }
        return 0;
    case Node::FN: {
        uint32_t v = eval(e, n.a, cpu);
        switch (n.fn) {
        case F_M8:  return peek(cpu, v);
        case F_M16: return peek(cpu, v) | (peek(cpu, v + 1) << 8);
        case F_M24: return peek(cpu, v) | (peek(cpu, v + 1) << 8) | (peek(cpu, v + 2) << 16);
        case F_Z8:  return peek(cpu, v & 0xFFFF);
        case F_Z16: return peek(cpu, v & 0xFFFF) | (peek(cpu, (v + 1) & 0xFFFF) << 8);
        case F_S8:  return peek(cpu, stack_addr(cpu, v));
        case F_S16: return peek(cpu, stack_addr(cpu, v)) | (peek(cpu, stack_addr(cpu, v + 1)) << 8);
        }
        return 0;
    }
    case Node::ADD: return (eval(e, n.a, cpu) + eval(e, n.b, cpu)) & 0xFFFFFF;
    case Node::SUB: return (eval(e, n.a, cpu) - eval(e, n.b, cpu)) & 0xFFFFFF;
    }
    return 0;
}
inline uint32_t eval(const Expr &e, cpu_state *cpu) { return eval(e, e.root, cpu); }

// ------------------------------------------------------------- conditions ---
enum Op { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, OP_IN };
struct Cond { Expr lhs, rhs; Op op = OP_EQ; std::vector<uint32_t> set; std::string src; };

inline Cond parse_cond(const std::string &c, const std::string &spec) {
    Cond k; k.src = c;
    // `<expr>~{v,v,...}` -- "is one of". No spaces anywhere in a spec value: the
    // spec is split on spaces, so the set operator had to be spaceless.
    size_t i = c.find("~{");
    if (i != std::string::npos && c.back() == '}') {
        k.op = OP_IN;
        k.lhs = parse_expr(c.substr(0, i), spec);
        std::string body = c.substr(i + 2);
        body.pop_back();
        size_t s = 0;
        while (s <= body.size()) {
            size_t e = body.find(',', s); if (e == std::string::npos) e = body.size();
            std::string v = body.substr(s, e - s);
            if (!v.empty()) k.set.push_back((uint32_t)strtoul(v.c_str() + (v[0] == '$'), nullptr, 16));
            s = e + 1;
        }
        if (k.set.empty()) fail(spec, "empty set in condition '" + c + "'");
        return k;
    }
    static const struct { const char *t; Op op; } ops[] = {
        {"==", OP_EQ}, {"!=", OP_NE}, {"<=", OP_LE}, {">=", OP_GE}, {"<", OP_LT}, {">", OP_GT}};
    for (auto &o : ops) {
        size_t j = c.find(o.t);
        if (j != std::string::npos) {
            k.op = o.op;
            k.lhs = parse_expr(c.substr(0, j), spec);
            k.rhs = parse_expr(c.substr(j + strlen(o.t)), spec);
            return k;
        }
    }
    fail(spec, "no operator in condition '" + c + "'");
}

inline bool holds(const Cond &k, cpu_state *cpu) {
    const uint32_t l = eval(k.lhs, cpu);
    if (k.op == OP_IN) { for (uint32_t v : k.set) if (v == l) return true; return false; }
    const uint32_t r = eval(k.rhs, cpu);
    switch (k.op) {
    case OP_EQ: return l == r; case OP_NE: return l != r; case OP_LT: return l < r;
    case OP_GT: return l > r;  case OP_LE: return l <= r; case OP_GE: return l >= r;
    default: return false;
    }
}

// --------------------------------------------------------------- payloads ---
struct Payload {
    enum Kind { REGS, RING, ZP, STACK, MEM, FIELD, TEXT, SCAN } kind = REGS;
    uint32_t lo = 0, hi = 0xFF, n = 32, max = 24;
    bool scan_stack = false;
    Expr e;
    std::string name;
};

inline uint32_t dec(const std::string &s, const std::string &spec, const char *what) {
    if (s.empty()) fail(spec, std::string("missing ") + what);
    for (char c : s) if (c < '0' || c > '9') fail(spec, std::string("expected a decimal ") + what + ", got '" + s + "'");
    return (uint32_t)strtoul(s.c_str(), nullptr, 10);
}
inline uint32_t hex(const std::string &s, const std::string &spec, const char *what) {
    std::string t = s; if (!t.empty() && t[0] == '$') t.erase(0, 1);
    if (t.empty()) fail(spec, std::string("missing ") + what);
    for (char c : t) if (!isxdigit((unsigned char)c)) fail(spec, std::string("expected hex ") + what + ", got '" + s + "'");
    return (uint32_t)strtoul(t.c_str(), nullptr, 16);
}

inline Payload parse_payload(const std::string &item, const std::string &spec) {
    Payload p;
    const size_t eq = item.find('=');
    const std::string key = item.substr(0, eq), val = (eq == std::string::npos) ? "" : item.substr(eq + 1);
    if (key == "regs") { p.kind = Payload::REGS; }
    else if (key == "ring") { p.kind = Payload::RING; p.n = val.empty() ? 32 : dec(val, spec, "ring length"); if (p.n > 256) p.n = 256; }
    else if (key == "zp") {
        p.kind = Payload::ZP;
        if (!val.empty()) {
            size_t d = val.find('-'); if (d == std::string::npos) fail(spec, "zp wants LO-HI, got '" + val + "'");
            p.lo = hex(val.substr(0, d), spec, "zp low"); p.hi = hex(val.substr(d + 1), spec, "zp high");
            if (p.hi < p.lo || p.hi > 0xFFFF) fail(spec, "bad zp window '" + val + "'");
            if (p.hi - p.lo + 1 > 4096) fail(spec, "zp window larger than 4096 bytes; use mem= for that");
        }
    }
    else if (key == "stack") { p.kind = Payload::STACK; p.n = val.empty() ? 32 : dec(val, spec, "stack length"); if (p.n > 256) p.n = 256; }
    else if (key == "mem") {
        p.kind = Payload::MEM;
        size_t c = val.rfind(':'); if (c == std::string::npos) fail(spec, "mem wants <expr>:<len>, got '" + val + "'");
        p.e = parse_expr(val.substr(0, c), spec); p.n = dec(val.substr(c + 1), spec, "mem length");
        if (p.n == 0 || p.n > 4096) fail(spec, "mem length must be 1..4096");
    }
    else if (key == "field") {
        p.kind = Payload::FIELD;
        size_t c = val.find(':'); if (c == std::string::npos) fail(spec, "field wants <name>:<expr>, got '" + val + "'");
        p.name = val.substr(0, c); p.e = parse_expr(val.substr(c + 1), spec);
        if (p.name.empty()) fail(spec, "field needs a name");
    }
    else if (key == "text") { p.kind = Payload::TEXT; }
    else if (key == "scan") {
        // scan=<src>:<lo>-<hi>:<n>:<max>
        p.kind = Payload::SCAN;
        std::vector<std::string> parts; size_t s = 0;
        while (true) { size_t c = val.find(':', s); parts.push_back(val.substr(s, c == std::string::npos ? std::string::npos : c - s)); if (c == std::string::npos) break; s = c + 1; }
        if (parts.size() != 4) fail(spec, "scan wants <zp|stack>:<lo>-<hi>:<n>:<max>, got '" + val + "'");
        if (parts[0] == "stack") p.scan_stack = true; else if (parts[0] != "zp") fail(spec, "scan source must be zp or stack");
        size_t d = parts[1].find('-'); if (d == std::string::npos) fail(spec, "scan wants a lo-hi pointer range");
        p.lo = hex(parts[1].substr(0, d), spec, "scan low"); p.hi = hex(parts[1].substr(d + 1), spec, "scan high");
        p.n = dec(parts[2], spec, "scan bytes"); p.max = dec(parts[3], spec, "scan max");
        if (p.n == 0 || p.n > 256 || p.max == 0 || p.max > 64) fail(spec, "scan: bytes 1..256, max 1..64");
    }
    else fail(spec, "unknown payload '" + item + "'");
    return p;
}

// ------------------------------------------------------------------ specs ---
struct Trap {
    std::string name, src;
    bool any_bank = true; uint32_t lo = 0, hi = 0;          // entry trigger
    bool has_exit = false, exit_any_bank = true; uint32_t exit_pc = 0;
    std::vector<Cond> when;
    enum Policy { ONCE, EVERY, FIRST } policy = ONCE;
    uint32_t first_n = 1; bool has_key = false; Expr key;
    std::unordered_map<uint32_t, uint32_t> keycount;
    uint32_t cap = 20000;
    std::vector<Payload> pdo, pheavy, pxdo, pxheavy;
    FILE *out = nullptr; bool own_out = false; std::string out_path;
    // state
    uint32_t n = 0, entries = 0, exits = 0, hits = 0; bool fired = false, capped = false;
    long open_entry = -1; bool open_heavy = false;
    // zp/stack windows used by scan (the last ones configured; defaults otherwise)
    uint32_t zp_lo = 0, zp_hi = 0xFF, stack_n = 32;
};

inline bool g_on = false;
inline std::vector<Trap> g_traps;
inline bool g_ring_on = false;
inline uint32_t g_ring[256];
inline int g_ring_i = 0;
inline FILE *g_default_out = nullptr;
inline bool g_armed = false;

// split on sep at nesting depth 0 (parentheses and braces)
inline std::vector<std::string> split_top(const std::string &s, char sep) {
    std::vector<std::string> out; std::string cur; int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '{') depth++;
        else if (c == ')' || c == '}') depth--;
        if (c == sep && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

inline void parse_pc(const std::string &v, const std::string &spec, bool &any_bank, uint32_t &pc) {
    size_t c = v.find(':');
    if (c == std::string::npos) { any_bank = true; pc = hex(v, spec, "pc") & 0xFFFF; return; }
    std::string b = v.substr(0, c);
    if (b == "*") { any_bank = true; pc = hex(v.substr(c + 1), spec, "pc") & 0xFFFF; return; }
    any_bank = false;
    pc = ((hex(b, spec, "bank") & 0xFF) << 16) | (hex(v.substr(c + 1), spec, "pc") & 0xFFFF);
}

inline void parse_spec(const std::string &raw, int index) {
    Trap t; t.src = raw; t.name = "trap" + std::to_string(index);
    bool have_at = false;
    for (const std::string &tok0 : split_top(raw, ' ')) {
        if (tok0.empty()) continue;
        const std::string tok = tok0;
        const size_t eq = tok.find('=');
        const std::string key = tok.substr(0, eq), val = (eq == std::string::npos) ? "" : tok.substr(eq + 1);
        if (key == "name") { if (val.empty()) fail(raw, "name= needs a value"); t.name = val; }
        else if (key == "at") {
            have_at = true;
            size_t d = val.find('-');
            std::string a = (d == std::string::npos) ? val : val.substr(0, d);
            bool ab; uint32_t lo; parse_pc(a, raw, ab, lo);
            t.any_bank = ab; t.lo = lo; t.hi = lo;
            if (d != std::string::npos) {
                uint32_t hi = hex(val.substr(d + 1), raw, "range end") & 0xFFFF;
                t.hi = ab ? hi : ((lo & 0xFF0000) | hi);
                if (t.hi < t.lo) fail(raw, "at= range end below start");
            }
        }
        else if (key == "exit") { t.has_exit = true; parse_pc(val, raw, t.exit_any_bank, t.exit_pc); }
        else if (key == "when") { for (auto &c : split_top(val, ',')) if (!c.empty()) t.when.push_back(parse_cond(c, raw)); }
        else if (key == "once") t.policy = Trap::ONCE;
        else if (key == "every") t.policy = Trap::EVERY;
        else if (key == "first") {
            t.policy = Trap::FIRST;
            size_t s = val.find('/');
            t.first_n = dec(val.substr(0, s), raw, "first count");
            if (s != std::string::npos) { t.has_key = true; t.key = parse_expr(val.substr(s + 1), raw); }
        }
        else if (key == "cap") { t.cap = dec(val, raw, "cap"); if (t.cap == 0) fail(raw, "cap must be > 0"); }
        else if (key == "do" || key == "heavy" || key == "xdo" || key == "xheavy") {
            std::vector<Payload> &dst = (key == "do") ? t.pdo : (key == "heavy") ? t.pheavy : (key == "xdo") ? t.pxdo : t.pxheavy;
            for (auto &item : split_top(val, ',')) {
                if (item.empty()) continue;
                Payload p = parse_payload(item, raw);
                if (p.kind == Payload::RING) g_ring_on = true;
                if (p.kind == Payload::ZP) { t.zp_lo = p.lo; t.zp_hi = p.hi; }
                if (p.kind == Payload::STACK) t.stack_n = p.n;
                dst.push_back(p);
            }
        }
        else if (key == "out") {
            if (val.empty()) fail(raw, "out= needs a path");
            t.out = fopen(val.c_str(), "wb");
            if (!t.out) fail(raw, "cannot open out file '" + val + "'");
            t.own_out = true; t.out_path = val;
        }
        else fail(raw, "unknown key '" + key + "'");
    }
    if (!have_at) fail(raw, "a spec needs at=<pc>");
    if (t.pdo.empty() && t.pheavy.empty()) { Payload r; r.kind = Payload::REGS; t.pdo.push_back(r); }   // never a bare "it fired"
    if (!t.out) { t.out = g_default_out; }
    g_traps.push_back(std::move(t));
}

// ---------------------------------------------------------------- records ---
inline void hexdump(std::string &s, cpu_state *cpu, uint32_t base, uint32_t len, bool wrap_stack, uint32_t k0 = 0) {
    static const char *hx = "0123456789abcdef";
    for (uint32_t i = 0; i < len; i++) {
        uint8_t v = wrap_stack ? peek(cpu, stack_addr(cpu, k0 + i)) : peek(cpu, base + i);
        s += hx[v >> 4]; s += hx[v & 15];
    }
}

inline void pc_str(std::string &s, uint32_t fpc) {
    char b[16]; snprintf(b, sizeof b, "%02X:%04X", (fpc >> 16) & 0xFF, fpc & 0xFFFF); s += b;
}

inline void emit_payloads(std::string &s, const std::vector<Payload> &ps, Trap &t, cpu_state *cpu) {
    // Scalars are gathered into one "f" object and appended after everything
    // else, so a field= that precedes a mem= in the same list cannot swallow it.
    std::string f;
    for (const Payload &p : ps) {
        char b[96];
        switch (p.kind) {
        case Payload::REGS:
            snprintf(b, sizeof b, ",\"regs\":{\"a\":%u,\"x\":%u,\"y\":%u,\"s\":%u,\"d\":%u,\"dbr\":%u,\"pbr\":%u,\"p\":%u,\"e\":%u}",
                     (unsigned)cpu->a, (unsigned)cpu->x, (unsigned)cpu->y, (unsigned)cpu->sp, (unsigned)cpu->d,
                     (unsigned)cpu->db, (unsigned)((cpu->full_pc >> 16) & 0xFF), (unsigned)cpu->p, (unsigned)cpu->E);
            s += b; break;
        case Payload::RING: {
            s += ",\"ring\":[";
            int count = (int)p.n; if (count > g_ring_i) count = g_ring_i;
            for (int k = g_ring_i - count; k < g_ring_i; k++) {
                if (k != g_ring_i - count) s += ',';
                s += '"'; pc_str(s, g_ring[k & 255]); s += '"';
            }
            s += ']'; break;
        }
        case Payload::ZP:
            s += ",\"zp\":\""; hexdump(s, cpu, p.lo, p.hi - p.lo + 1, false); s += '"'; break;
        case Payload::STACK:
            s += ",\"stack\":\""; hexdump(s, cpu, 0, p.n, true, 1); s += '"'; break;
        case Payload::MEM: {
            uint32_t base = eval(p.e, cpu);
            snprintf(b, sizeof b, ",\"mem\":{\"at\":%u,\"expr\":\"", (unsigned)base);
            s += b; s += p.e.src; s += "\",\"d\":\""; hexdump(s, cpu, base, p.n, false); s += "\"}";
            break;
        }
        case Payload::FIELD:
            if (!f.empty()) f += ',';
            snprintf(b, sizeof b, "\"%s\":%u", p.name.c_str(), (unsigned)eval(p.e, cpu)); f += b;
            break;
        case Payload::TEXT: {
            std::string page; gtap_page_json(cpu, page);
            s += ",\"text\":\""; s += page; s += '"'; break;
        }
        case Payload::SCAN: {
            s += ",\"scan\":[";
            uint32_t len = p.scan_stack ? t.stack_n : (t.zp_hi - t.zp_lo + 1);
            std::vector<uint8_t> w(len + 1);
            for (uint32_t i = 0; i <= len; i++) w[i] = p.scan_stack ? peek(cpu, stack_addr(cpu, 1 + i)) : peek(cpu, t.zp_lo + i);
            uint32_t found = 0;
            for (uint32_t i = 0; i + 1 < len + 1 && found < p.max; i++) {
                uint32_t ptr = w[i] | (w[i + 1] << 8);
                if (ptr < p.lo || ptr > p.hi) continue;
                if (found) s += ',';
                snprintf(b, sizeof b, "{\"at\":\"%s%u\",\"ptr\":%u,\"d\":\"", p.scan_stack ? "s" : "zp", (unsigned)(p.scan_stack ? i + 1 : t.zp_lo + i), (unsigned)ptr);
                s += b; hexdump(s, cpu, ptr, p.n, false); s += "\"}";
                found++;
            }
            s += ']'; break;
        }
        }
    }
    if (!f.empty()) { s += ",\"f\":{"; s += f; s += '}'; }
}

inline void write_record(Trap &t, const std::string &body) {
    if (t.out) { fputs(body.c_str(), t.out); fputc('\n', t.out); fflush(t.out); }
    else { printf("IIGS TRAP: %s\n", body.c_str()); }
}

inline void emit(Trap &t, cpu_state *cpu, uint64_t cycles, bool is_exit, uint32_t n, bool heavy) {
    std::string s; s.reserve(512);
    char b[160];
    snprintf(b, sizeof b, "{\"event\":\"trap\",\"trap\":\"%s\",\"t\":\"%s\",\"n\":%u,\"frame\":%d,\"cycles\":%llu,\"pc\":\"",
             t.name.c_str(), is_exit ? "x" : "e", (unsigned)n, g_iigs_cur_frame, (unsigned long long)cycles);
    s += b; pc_str(s, cpu->full_pc & 0xFFFFFF); s += '"';
    snprintf(b, sizeof b, ",\"heavy\":%d", heavy ? 1 : 0); s += b;
    emit_payloads(s, is_exit ? t.pxdo : t.pdo, t, cpu);
    if (heavy) emit_payloads(s, is_exit ? t.pxheavy : t.pheavy, t, cpu);
    s += '}';
    write_record(t, s);
}

// ------------------------------------------------------------- the check ---
inline bool matches(bool any_bank, uint32_t lo, uint32_t hi, uint32_t fpc) {
    const uint32_t v = any_bank ? (fpc & 0xFFFF) : fpc;
    return v >= lo && v <= hi;
}

inline void check(cpu_state *cpu, uint64_t cycles) {
    const uint32_t fpc = cpu->full_pc & 0xFFFFFF;
    if (g_ring_on) { g_ring[g_ring_i & 255] = fpc; g_ring_i++; }
    for (Trap &t : g_traps) {
        if (t.has_exit && t.open_entry >= 0 && matches(t.exit_any_bank, t.exit_pc, t.exit_pc, fpc)) {
            t.exits++;
            emit(t, cpu, cycles, true, (uint32_t)t.open_entry, t.open_heavy);
            t.open_entry = -1;
        }
        if (!matches(t.any_bank, t.lo, t.hi, fpc)) continue;
        t.hits++;
        if (t.capped) continue;
        if (t.policy == Trap::ONCE && t.fired) continue;
        bool ok = true;
        for (const Cond &c : t.when) if (!holds(c, cpu)) { ok = false; break; }
        if (!ok) continue;
        bool heavy;
        if (t.policy == Trap::FIRST) {
            uint32_t key = t.has_key ? eval(t.key, cpu) : 0;
            uint32_t &cnt = t.keycount[key];
            heavy = cnt < t.first_n;
            if (heavy) cnt++;
        } else heavy = true;
        const uint32_t n = t.n++;
        t.entries++; t.fired = true;
        emit(t, cpu, cycles, false, n, heavy);
        if (t.has_exit) { t.open_entry = (long)n; t.open_heavy = heavy; }
        if (t.n >= t.cap) {
            t.capped = true;
            printf("A2GSPU_TRAP: %s reached cap=%u; further hits are counted but not recorded\n", t.name.c_str(), (unsigned)t.cap);
        }
    }
}

// ------------------------------------------------------------ arm / close ---
inline void describe(const Trap &t) {
    printf("A2GSPU_TRAP: armed %s at=", t.name.c_str());
    if (t.any_bank) printf("*:%04X", t.lo & 0xFFFF); else printf("%02X:%04X", (t.lo >> 16) & 0xFF, t.lo & 0xFFFF);
    if (t.hi != t.lo) printf("-%04X", t.hi & 0xFFFF);
    if (t.has_exit) { if (t.exit_any_bank) printf(" exit=*:%04X", t.exit_pc); else printf(" exit=%02X:%04X", (t.exit_pc >> 16) & 0xFF, t.exit_pc & 0xFFFF); }
    printf(" when=%zu policy=%s", t.when.size(), t.policy == Trap::ONCE ? "once" : t.policy == Trap::EVERY ? "every" : "first");
    if (t.policy == Trap::FIRST) printf("(%u%s)", (unsigned)t.first_n, t.has_key ? "/key" : "");
    printf(" cap=%u do=%zu heavy=%zu xdo=%zu xheavy=%zu out=%s\n", (unsigned)t.cap, t.pdo.size(), t.pheavy.size(),
           t.pxdo.size(), t.pxheavy.size(), t.out ? (t.own_out ? t.out_path.c_str() : "A2GSPU_TRAP_OUT") : "stdout");
}

inline void close_all() {
    for (Trap &t : g_traps) {
        printf("A2GSPU_TRAP: %s hits=%u records=%u entries=%u exits=%u%s%s\n", t.name.c_str(),
               (unsigned)t.hits, (unsigned)t.n, (unsigned)t.entries, (unsigned)t.exits,
               t.capped ? " CAPPED" : "", t.hits == 0 ? " (never reached)" : "");
        if (t.own_out && t.out) { fclose(t.out); t.out = nullptr; }
    }
    if (g_default_out) { fclose(g_default_out); g_default_out = nullptr; }
    fflush(stdout);
}

inline void arm() {
    if (g_armed) return;
    g_armed = true;
    const char *env = getenv("A2GSPU_TRAP");
    const char *file = getenv("A2GSPU_TRAP_FILE");
    const char *outp = getenv("A2GSPU_TRAP_OUT");
    if (!env && !file) return;                       // stock: nothing armed, nothing printed
    if (outp) {
        g_default_out = fopen(outp, "wb");
        if (!g_default_out) fail(outp, "cannot open A2GSPU_TRAP_OUT");
    }
    int index = 0;
    if (env) for (auto &spec : split_top(env, ';')) { std::string s = spec; while (!s.empty() && s.front() == ' ') s.erase(0, 1); if (!s.empty()) parse_spec(s, index++); }
    if (file) {
        FILE *f = fopen(file, "rb");
        if (!f) fail(file, "cannot open A2GSPU_TRAP_FILE");
        char line[2048];
        while (fgets(line, sizeof line, f)) {
            std::string s = line;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(0, 1);
            if (s.empty() || s[0] == '#') continue;
            parse_spec(s, index++);
        }
        fclose(f);
    }
    if (g_traps.empty()) fail(env ? env : file, "no trap specs found");
    for (const Trap &t : g_traps) describe(t);
    g_on = true;
    atexit(close_all);
}

}  // namespace a2trap

// The two names the rest of the tree uses. Kept flat so the CPU-loop gate reads
// like its neighbours: `if (g_trap_on) a2gspu_trap_check(cpu, cycles);`
inline bool &g_trap_on = a2trap::g_on;
inline void a2gspu_trap_arm() { a2trap::arm(); }
inline void a2gspu_trap_check(cpu_state *cpu, uint64_t cycles) { a2trap::check(cpu, cycles); }
