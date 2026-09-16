#pragma once
#include "a2gspu_rails_registry.hpp"   // a2rails::Registrar: this rail registers its own reader (M8)
#include "a2record.hpp"   // the record contract: one writer, one shape, one global sink
// ============================================================================
// a2gspu_liveness.hpp -- A2GSPU_LIVENESS=N: a progress proof every N frames.
//
// WHY: the Apple IIgs Dealer Demo with one of its two disks halts on
// "file not found -- Hit any key" before it draws a pixel, and a run of that
// state produces store counts, a visible fraction, a coverage bitmap and
// status=OK. Nothing in the run's output distinguished an error screen with a
// wait loop from a show. What exposed it was running the workload at three
// lengths and noticing the SHR hash never changed. That observation is now a
// rail: every N frames, hash what is on the screen, say whether it changed,
// and at the end say since when it has not.
//
// It is a SIGNAL, not a verdict. An idle Finder desktop also leaves the screen
// alone for seconds at a time. So the field is named for what was measured --
// screen_unchanged_since -- and the consumer decides what that means for its
// workload. What it rules out is the specific lie above: a run that quietly
// stopped progressing while its counters kept climbing.
//
// Env:
//   A2GSPU_LIVENESS=N         sample every N frames (unset or 0 = off)
//   A2GSPU_LIVENESS_STALL=K   unchanged samples in a row before the screen counts
//                             as static (default 3)
//   A2GSPU_LIVENESS_OUT=file  NDJSON sink for the samples; default is one stdout
//                             line per sample, prefixed "A2GSPU LIVENESS:"
//
// Sample: frame (since arm), cycles, screen hash, text hash, keyboard reads
// since the previous sample (IIgs: KeyGloo $C000/$C010 reads; II family: the
// keyboard device's $C000/$C010 reads), what changed, and -- from the taps in
// a2gspu_progress_taps.hpp -- why a hash that did not change did not:
//   insns         instructions executed since arm
//   screen_store  cycle of the last store onto the screen range (null: none yet)
//   text_store    cycle of the last store onto the text page (null: none yet)
//   kbd_poll      cycle of the last keyboard read (null: none yet)
// A static screen with a recent screen_store is a program redrawing the same
// bytes; with an old one it stopped drawing; with insns not advancing the CPU
// itself stopped.
//   screen  IIgs: the SHR window $E1:2000-9FFF, read from the Mega II image
//           II-family: HGR page 1, $2000-3FFF
//   text    the 40-column text page, $0400-07FF ($E0 on the IIgs)
// Both are read side-effect-free (probe_peek). House FNV-1a-64.
//
// Close-out (always, when armed): samples, the frame of each surface's last
// change, screen_unchanged_since= / text_unchanged_since=<frame>|none, and
// cpu_stopped_since=<frame>|none (insns did not advance for STALL samples). The
// two surfaces are tracked separately because a halted SHR workload keeps
// redrawing its text page from the wait loop (measured on the one-disk Dealer
// Demo: four cycling text hashes over a static SHR window). The same two
// fields are appended to GSDIAG while the rail is armed, and absent when it is
// not -- their presence says the rail ran.
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "computer.hpp"
#include "cpu.hpp"
#include "mmus/mmu.hpp"
#include "house_fnv.hpp"
#include "iigs_diag.hpp"               // g_gsdiag_extra
#include "devices/adb/keygloo.hpp"     // a2gspu_keygloo_read_counts
#include "a2gspu_progress_taps.hpp"    // insns, last screen/text store, last keyboard poll
#include "a2gspu_rail_setters.hpp"      // the launch arms the rails through their setters

inline int      g_liveness_every = 0;              // 0 = off (the CPU/frame loop gate)
inline int      g_liveness_stall = 3;
inline bool     g_liveness_is_iigs = false;
inline FILE    *g_liveness_out = nullptr;
inline int      g_liveness_frame = 0;               // frames since arm
inline int      g_liveness_samples = 0;
inline uint64_t g_liveness_prev_screen = 0, g_liveness_prev_text = 0, g_liveness_prev_kbd = 0;
// Two surfaces, tracked separately: a SHR workload that halts on an error
// screen keeps redrawing its text page from the wait loop (measured -- the
// one-disk Dealer Demo cycles four text hashes while its SHR window never
// moves), so "any change" would have hidden the static screen. The consumer
// picks the surface its workload displays.
inline int      g_liveness_screen_last = 0, g_liveness_text_last = 0;   // frame of the last change
inline int      g_liveness_screen_run = 0,  g_liveness_text_run = 0;    // consecutive unchanged samples
inline int      g_liveness_screen_since = -1, g_liveness_text_since = -1;   // static since, or -1
// The CPU as a third surface: "changed" is insns advancing. A stopped CPU and a
// static screen look identical to the two hashes; this is what tells them apart.
inline uint64_t g_liveness_prev_insns = 0;
inline int      g_liveness_cpu_last = 0, g_liveness_cpu_run = 0, g_liveness_cpu_since = -1;
inline bool     g_liveness_armed = false;
inline char     g_liveness_gsdiag[128];

inline void a2gspu_liveness_track(bool first, bool changed, int &last, int &run, int &since) {
    if (first || changed) { last = g_liveness_frame; run = 0; since = -1; }
    else if (++run >= g_liveness_stall && since < 0) since = last;
}

inline uint64_t a2gspu_liveness_hash(MMU *mmu, uint32_t lo, uint32_t hi) {
    uint64_t h = HOUSE_FNV_BASIS;
    for (uint32_t a = lo; a <= hi; a++) { h ^= mmu->probe_peek(a); h *= HOUSE_FNV_PRIME; }
    return h;
}

inline void a2gspu_liveness_since_str(int since, char *buf, size_t n) {
    if (since >= 0) snprintf(buf, n, "%d", since); else snprintf(buf, n, "none");
}

// A tap's cycle stamp as text: the cycle, or `none` before the first event.
inline void a2gspu_liveness_cycle_str(bool seen, uint64_t cycle, char *buf, size_t n) {
    if (seen) snprintf(buf, n, "%llu", (unsigned long long)cycle); else snprintf(buf, n, "none");
}

inline const char *a2gspu_liveness_gsdiag_extra() {
    char a[16], b[16];
    a2gspu_liveness_since_str(g_liveness_screen_since, a, sizeof a);
    a2gspu_liveness_since_str(g_liveness_text_since, b, sizeof b);
    char c[16];
    a2gspu_liveness_since_str(g_liveness_cpu_since, c, sizeof c);
    snprintf(g_liveness_gsdiag, sizeof g_liveness_gsdiag, " screen_unchanged_since=%s text_unchanged_since=%s cpu_stopped_since=%s", a, b, c);
    return g_liveness_gsdiag;
}

inline void a2gspu_liveness_close() {
    if (!g_liveness_armed) return;
    g_liveness_armed = false;   // once per window: stop() and atexit both come here
    char a[16], b[16], c[16], ss[24], ts[24], kp[24];
    a2gspu_liveness_since_str(g_liveness_screen_since, a, sizeof a);
    a2gspu_liveness_since_str(g_liveness_text_since, b, sizeof b);
    a2gspu_liveness_since_str(g_liveness_cpu_since, c, sizeof c);
    a2gspu_liveness_cycle_str(g_progress_screen_seen, g_progress_screen_store, ss, sizeof ss);
    a2gspu_liveness_cycle_str(g_progress_text_seen, g_progress_text_store, ts, sizeof ts);
    a2gspu_liveness_cycle_str(g_progress_kbd_seen, g_progress_kbd_poll, kp, sizeof kp);
    printf("A2GSPU LIVENESS: samples=%d frames=%d screen_last_changed=%d screen_unchanged_since=%s text_last_changed=%d text_unchanged_since=%s"
           " insns=%llu cpu_stopped_since=%s screen_store=%s text_store=%s kbd_poll=%s\n",
           g_liveness_samples, g_liveness_frame, g_liveness_screen_last, a, g_liveness_text_last, b,
           (unsigned long long)g_progress_insns, c, ss, ts, kp);
    if (g_liveness_out) { fclose(g_liveness_out); g_liveness_out = nullptr; }
    fflush(stdout);
}

// ---- readback: `rail read A2GSPU_LIVENESS` ----------------------------------
inline void a2gspu_liveness_format(char *buf, size_t n) {
    if (!g_liveness_armed) { snprintf(buf, n, "status=FAIL liveness-not-armed -- set A2GSPU_LIVENESS=N at launch, or `rail set A2GSPU_LIVENESS N`"); return; }
    char a[16], b[16], c[16], ss[24], ts[24], kp[24];
    a2gspu_liveness_since_str(g_liveness_screen_since, a, sizeof a);
    a2gspu_liveness_since_str(g_liveness_text_since, b, sizeof b);
    a2gspu_liveness_since_str(g_liveness_cpu_since, c, sizeof c);
    a2gspu_liveness_cycle_str(g_progress_screen_seen, g_progress_screen_store, ss, sizeof ss);
    a2gspu_liveness_cycle_str(g_progress_text_seen, g_progress_text_store, ts, sizeof ts);
    a2gspu_liveness_cycle_str(g_progress_kbd_seen, g_progress_kbd_poll, kp, sizeof kp);
    snprintf(buf, n, "status=OK every=%d stall=%d samples=%d frames=%d screen_last_changed=%d screen_unchanged_since=%s text_last_changed=%d text_unchanged_since=%s"
             " insns=%llu cpu_stopped_since=%s screen_store=%s text_store=%s kbd_poll=%s",
             g_liveness_every, g_liveness_stall, g_liveness_samples, g_liveness_frame,
             g_liveness_screen_last, a, g_liveness_text_last, b,
             (unsigned long long)g_progress_insns, c, ss, ts, kp);
}

// Start sampling every `every` frames, from a fresh count. `rail set` and the
// launch both come here through the rail's one setter (a2gspu_rail_setters.cpp);
// a re-arm prints nothing for the arm it replaces and starts its own window.
inline void a2gspu_liveness_start(int every, bool is_iigs) {
    g_liveness_every = every;
    g_liveness_is_iigs = is_iigs;
    g_liveness_frame = 0; g_liveness_samples = 0;
    g_liveness_prev_screen = g_liveness_prev_text = 0;
    g_liveness_prev_kbd = 0;
    g_liveness_screen_last = g_liveness_text_last = 0;
    g_liveness_screen_run = g_liveness_text_run = 0;
    g_liveness_screen_since = g_liveness_text_since = -1;
    g_liveness_cpu_last = g_liveness_cpu_run = 0; g_liveness_cpu_since = -1;
    g_liveness_prev_insns = g_progress_insns;
    g_gsdiag_extra = a2gspu_liveness_gsdiag_extra;
    static bool s_atexit = false;
    if (!s_atexit) { s_atexit = true; atexit(a2gspu_liveness_close); }
    g_liveness_armed = true;
    // The taps: clock from the record contract, which both arm sites register
    // first (a2record_register), so a stamp and a record's cycles= agree.
    g_progress_taps_iigs = is_iigs;
    g_progress_now = [] { return (uint64_t)a2record::cycles_now(); };
    g_progress_taps = true;
    printf("A2GSPU LIVENESS: armed every=%d stall=%d screen=%s text=%s kbd=%s out=%s taps=insns,screen_store,text_store,kbd_poll\n",
           g_liveness_every, g_liveness_stall,
           is_iigs ? "$E1:2000-9FFF" : "$2000-3FFF", is_iigs ? "$E0:0400-07FF" : "$0400-07FF",
           is_iigs ? "KeyGloo C000+C010 reads" : "keyboard C000+C010 reads",
           g_liveness_out ? "ndjson" : "stdout");
}
// Stop: the close-out for the window that ran, and GSDIAG loses the fields.
// The taps stay on -- the run hash may be counting with them.
inline void a2gspu_liveness_stop() {
    a2gspu_liveness_close();
    g_liveness_every = 0;
    g_liveness_armed = false;
    if (g_gsdiag_extra == a2gspu_liveness_gsdiag_extra) g_gsdiag_extra = nullptr;
}

// The launch. Both arm sites call it (the spike, then run_one_frame's lazy arm),
// so it runs once; the rails go through their setters, a bad value is fatal
// (exit 2) as before, and the sink is not opened for a rail that is not armed.
inline void a2gspu_liveness_arm(bool /*is_iigs: the setter asks the live MMU*/) {
    static bool s_once = false;
    if (s_once) return;
    s_once = true;
    if (!getenv("A2GSPU_LIVENESS")) return;
    a2gspu_rail_arm_env("A2GSPU_LIVENESS_OUT");
    a2gspu_rail_arm_env("A2GSPU_LIVENESS_STALL");
    a2gspu_rail_arm_env("A2GSPU_LIVENESS");
}

// Called at the end of every frame (run_one_frame), gated by g_liveness_every.
inline void a2gspu_liveness_tick(computer_t *computer) {
    g_liveness_frame++;
    if (g_liveness_frame % g_liveness_every) return;
    MMU *mmu = computer->cpu->mmu;
    const uint64_t screen = g_liveness_is_iigs ? a2gspu_liveness_hash(mmu, 0xE12000, 0xE19FFF)
                                               : a2gspu_liveness_hash(mmu, 0x002000, 0x003FFF);
    const uint64_t text   = g_liveness_is_iigs ? a2gspu_liveness_hash(mmu, 0xE00400, 0xE007FF)
                                               : a2gspu_liveness_hash(mmu, 0x000400, 0x0007FF);
    uint64_t kbd_total = g_progress_kbd_reads;   // II family: the keyboard device's reads
    if (g_liveness_is_iigs) { uint64_t c[5]; a2gspu_keygloo_read_counts(c); kbd_total = c[0] + c[1]; }
    const uint64_t kbd = kbd_total - g_liveness_prev_kbd;
    g_liveness_prev_kbd = kbd_total;
    const uint64_t cycles = computer->clock->get_cycles();

    const bool first = (g_liveness_samples == 0);
    const bool sch = !first && screen != g_liveness_prev_screen;
    const bool tch = !first && text != g_liveness_prev_text;
    const char *changed = first ? "first" : (sch && tch) ? "both" : sch ? "screen" : tch ? "text" : "none";
    a2gspu_liveness_track(first, sch, g_liveness_screen_last, g_liveness_screen_run, g_liveness_screen_since);
    a2gspu_liveness_track(first, tch, g_liveness_text_last, g_liveness_text_run, g_liveness_text_since);
    const bool ran = !first && g_progress_insns != g_liveness_prev_insns;
    a2gspu_liveness_track(first, ran, g_liveness_cpu_last, g_liveness_cpu_run, g_liveness_cpu_since);
    g_liveness_prev_insns = g_progress_insns;
    g_liveness_prev_screen = screen; g_liveness_prev_text = text; g_liveness_samples++;
    char ss[24], ts[24], kp[24];
    a2gspu_liveness_cycle_str(g_progress_screen_seen, g_progress_screen_store, ss, sizeof ss);
    a2gspu_liveness_cycle_str(g_progress_text_seen, g_progress_text_store, ts, sizeof ts);
    a2gspu_liveness_cycle_str(g_progress_kbd_seen, g_progress_kbd_poll, kp, sizeof kp);

    if (FILE *sink = a2record::sink(g_liveness_out)) {
        a2record::Record rec("liveness", "sample", g_liveness_frame, (unsigned long long)cycles);
        rec.hex16("screen", (unsigned long long)screen).hex16("text", (unsigned long long)text);
        rec.u("kbd", (unsigned long long)kbd);
        rec.str("changed", changed);
        rec.u("insns", (unsigned long long)g_progress_insns);
        if (g_progress_screen_seen) rec.u("screen_store", (unsigned long long)g_progress_screen_store); else rec.null("screen_store");
        if (g_progress_text_seen)   rec.u("text_store", (unsigned long long)g_progress_text_store);     else rec.null("text_store");
        if (g_progress_kbd_seen)    rec.u("kbd_poll", (unsigned long long)g_progress_kbd_poll);         else rec.null("kbd_poll");
        rec.write(sink, true);
    } else {
        printf("A2GSPU LIVENESS: frame=%d cycles=%llu screen=%016llX text=%016llX kbd=%llu changed=%s insns=%llu screen_store=%s text_store=%s kbd_poll=%s\n",
               g_liveness_frame, (unsigned long long)cycles, (unsigned long long)screen, (unsigned long long)text,
               (unsigned long long)kbd, changed, (unsigned long long)g_progress_insns, ss, ts, kp);
    }
}

// The liveness readback, supplied with the rail (M8): `rail read A2GSPU_LIVENESS`.
inline a2rails::Registrar g_reader_liveness{"A2GSPU_LIVENESS", a2gspu_liveness_format};
