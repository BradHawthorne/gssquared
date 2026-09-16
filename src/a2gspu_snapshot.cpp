/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// Snapshot file I/O -- moved out of gs2.cpp; see gs2_app.hpp.
#include "gs2_app.hpp"
#include "devices/diskii/diskii_controller.hpp"   // g_a2gspu_diskii: the Disk II controllers, by slot


// ---- A2GSPU warm-boot snapshot: file magic + cpu_state value-field (de)serialize ----
// Magic + version guard the on-disk layout: a stale snap.bin from an older binary is
// rejected loudly instead of mis-read. cpu serialize touches ONLY value fields. It NEVER
// touches the owned/plumbing pointers (mmu/cpun/core/trace_buffer) — a blanket copy would
// double-free the unique_ptr cpun. cpu_type is construction-fixed (set by the same -p 5
// platform across both runs) and intentionally excluded.
void a2gspu_cpu_save(FILE *f, cpu_state *cpu) {
    uint32_t magic = A2GSPU_SNAP_MAGIC, ver = A2GSPU_SNAP_VER;
    fwrite(&magic, 4, 1, f); fwrite(&ver, 4, 1, f);
    fwrite(&cpu->full_pc, 4, 1, f);     // union: pc+pb (the field the inject path sets)
    fwrite(&cpu->full_db, 4, 1, f);     // union: data bank in byte 2
    fwrite(&cpu->sp, 2, 1, f);
    fwrite(&cpu->a, 2, 1, f);
    fwrite(&cpu->x, 2, 1, f);
    fwrite(&cpu->y, 2, 1, f);
    fwrite(&cpu->d, 2, 1, f);
    fwrite(&cpu->p, 1, 1, f);           // union byte: all flag bitfields alias this
    uint8_t e = cpu->E;       fwrite(&e, 1, 1, f);       // 1-bit bitfield -> temp
    uint8_t cs = cpu->clock_stopped ? 1 : 0; fwrite(&cs, 1, 1, f);
    fwrite(&cpu->halt, 1, 1, f);
    uint8_t ia = cpu->irq_asserted ? 1 : 0;  fwrite(&ia, 1, 1, f);
    fwrite(&cpu->irq_pipe, 1, 1, f);
    fwrite(&cpu->reset_asserted, 8, 1, f);   // uint64_t bitmask (NOT a bool)
    uint8_t rd = cpu->rdy ? 1 : 0;     fwrite(&rd, 1, 1, f);
}
// Checked fread: read exactly `cnt` elements of `sz` bytes; on a short/failed read,
// `return false` out of the enclosing function. A truncated snapshot (interrupted
// save / partial copy) thus fails loud instead of restoring a half-initialized
// machine and reporting success.
#define A2GSPU_FREAD(ptr, sz, cnt, f) do { \
        if (fread((ptr), (sz), (cnt), (f)) != (size_t)(cnt)) return false; \
    } while (0)

// Returns false (without consuming the cpu block) on a bad magic/version, or on any
// truncated read of the cpu block.
bool a2gspu_cpu_load(FILE *f, cpu_state *cpu) {
    uint32_t magic = 0, ver = 0;
    if (fread(&magic, 4, 1, f) != 1 || fread(&ver, 4, 1, f) != 1) return false;
    if (magic != A2GSPU_SNAP_MAGIC || ver != A2GSPU_SNAP_VER) return false;
    A2GSPU_FREAD(&cpu->full_pc, 4, 1, f);
    A2GSPU_FREAD(&cpu->full_db, 4, 1, f);
    A2GSPU_FREAD(&cpu->sp, 2, 1, f);
    A2GSPU_FREAD(&cpu->a, 2, 1, f);
    A2GSPU_FREAD(&cpu->x, 2, 1, f);
    A2GSPU_FREAD(&cpu->y, 2, 1, f);
    A2GSPU_FREAD(&cpu->d, 2, 1, f);
    A2GSPU_FREAD(&cpu->p, 1, 1, f);
    uint8_t e = 0;  A2GSPU_FREAD(&e, 1, 1, f);  cpu->E = e & 1;
    uint8_t cs = 0; A2GSPU_FREAD(&cs, 1, 1, f); cpu->clock_stopped = (cs != 0);
    A2GSPU_FREAD(&cpu->halt, 1, 1, f);
    uint8_t ia = 0; A2GSPU_FREAD(&ia, 1, 1, f); cpu->irq_asserted = (ia != 0);
    A2GSPU_FREAD(&cpu->irq_pipe, 1, 1, f);
    A2GSPU_FREAD(&cpu->reset_asserted, 8, 1, f);
    uint8_t rd = 0; A2GSPU_FREAD(&rd, 1, 1, f); cpu->rdy = (rd != 0);
    return true;
}
#undef A2GSPU_FREAD

// ---- A2GSPU snapshot manifest: what the file holds, and what it does not ----
// (History: snapshots began as Class A -- CPU + MMU + soft switches + RAM -- and
// AGENTIC_ORACLE.md said so while the file and the restore did not. Device blocks
// have since been added; the manifest below is what says which a file holds.) A restored desktop never polls the keyboard
// again (the ADB micro is not in it) and a workload that needs the Ensoniq cannot be
// windowed from one at all -- both were learned the slow way. So the save appends,
// after the sentinel, a manifest naming the included and excluded state, the frame,
// the pins and the binary; the restore prints it, or prints that it is absent and
// what Class A means. Optional on read, so every existing snapshot still loads.
// `devices` is the comma-separated list of device blocks the save actually
// wrote (a2gspu_snap_write_devices); the manifest says what is in the file,
// never what a class letter would imply.
std::string a2gspu_snap_manifest_text(int frame, const std::string &devices) {
    const char *seed = getenv("A2GSPU_SEED"), *ft = getenv("A2GSPU_FAKETIME");
    char bram[40]; a2gspu_bram_field(bram, sizeof bram);
    uint64_t eh = 0; char exe[24];
    if (a2gspu_fnv_file(g_a2gspu_argv0, eh)) snprintf(exe, sizeof exe, "%016llX", (unsigned long long)eh);
    else snprintf(exe, sizeof exe, "unreadable");
    static const char *all[] = { "adb", "ensoniq", "vgcint", "iwm", "diskii", "pdblock", "media", "scanner", "rtc", "serial", "clock" };
    std::string included = "cpu,mmu,softswitches,ram,rom-map,megaii", excluded;
    const std::string have = "," + devices + ",";
    auto holds = [&](const char *d) { return have.find(std::string(",") + d + ",") != std::string::npos; };
    for (const char *d : all) {
        if (holds(d)) { included += ","; included += d; }
        else { if (!excluded.empty()) excluded += ","; excluded += d; }
    }
    /* THE CLASS LETTER IS DERIVED, not a constant.
     *
     * It was the literal 'A' in this snprintf, printed whatever the save had
     * actually written -- so a IIgs file holding five device blocks (adb,
     * ensoniq, vgcint, iwm, pdblock) still announced itself as Class A, "CPU and
     * MMU only". class= and excluded= are the two fields a consumer reads to
     * decide whether a workload can be windowed from a snapshot or has to pay a
     * cold boot, and both said no while the answer was yes. That is Event D of
     * INSTRUMENTATION_SHAPE.md section 1 pointing the other way, and it is the
     * reason the comment above this function says the manifest reports what is
     * in the file and never what a class letter would imply -- the generator was
     * honest and the letter beside it was a vestige.
     *
     * The letters are INSTRUMENTATION_SHAPE.md M5's own roadmap and nothing
     * else: A = cpu+mmu+softswitches+RAM, B adds scanner/video state, C adds the
     * ADB micro, the Ensoniq and the IWM. A letter is only claimed when its
     * definition is actually met -- C needs all three of adb/ensoniq/iwm, B
     * needs the video/interrupt state -- so a save that holds a device the
     * roadmap never named (a IIe holding only `pdblock`) stays class=A and lets
     * devices=/included= carry it. Inventing a B for it would replace one false
     * claim with another. devices=/included=/excluded= remain the contract; the
     * letter is a summary of them, and it can now be wrong only by being too
     * modest. */
    const char *cls = (holds("adb") && holds("ensoniq") && holds("iwm")) ? "C"
                    : (holds("vgcint") || holds("scanner"))              ? "B"
                                                                         : "A";
    char buf[768];
    snprintf(buf, sizeof buf,
             "class=%s devices=%s included=%s excluded=%s "
             "frame=%d seed=%s faketime=%s ramdisk=%d bram=%s build=%s exe=%s",
             cls, devices.empty() ? "-" : devices.c_str(), included.c_str(), excluded.c_str(),
             frame, seed ? seed : "unset", ft ? ft : "unset", getenv("A2GSPU_RAMDISK") ? 1 : 0,
             bram, BUILD_VERSION, exe);
    return buf;
}

// ---- device blocks (M5): between the MMU block and the sentinel ----
// Each block is 'GSDV', an 8-byte NUL-padded name, a version, a payload length,
// and the payload, so a reader that does not know a block can step over it and
// say so, and a snapshot from before device blocks reads exactly as it did.
static long a2gspu_snap_dev_begin(FILE *f, const char *name, uint32_t version) {
    uint32_t magic = A2GSPU_SNAP_DEV, len = 0; char nm[8] = {0};
    strncpy(nm, name, 7);
    fwrite(&magic, 4, 1, f); fwrite(nm, 1, 8, f); fwrite(&version, 4, 1, f);
    long at = ftell(f); fwrite(&len, 4, 1, f);
    return at;
}
// A u32 length in front of a sub-record, so a reader can step over one it cannot apply.
static long a2gspu_snap_len_begin(FILE *f) { uint32_t len = 0; long at = ftell(f); fwrite(&len, 4, 1, f); return at; }
static void a2gspu_snap_len_end(FILE *f, long at) {
    long end = ftell(f); uint32_t len = (uint32_t)(end - at - 4);
    fseek(f, at, SEEK_SET); fwrite(&len, 4, 1, f); fseek(f, end, SEEK_SET);
}
static void a2gspu_snap_dev_end(FILE *f, long at) {
    long end = ftell(f); uint32_t len = (uint32_t)(end - at - 4);
    fseek(f, at, SEEK_SET); fwrite(&len, 4, 1, f); fseek(f, end, SEEK_SET);
}
// Every device this machine has and this binary can save. Returns their names.
std::string a2gspu_snap_write_devices(FILE *f, computer_t *computer) {
    std::string names;
    keygloo_state_t *kg = (keygloo_state_t *)computer->get_module_state(MODULE_KEYGLOO);
    if (kg && kg->kg) {
        long at = a2gspu_snap_dev_begin(f, "adb", 1); kg->kg->A2GSPU_snapshot(f); a2gspu_snap_dev_end(f, at);
        names += "adb";
    }
    if (g_a2gspu_ensoniq) {
        long at = a2gspu_snap_dev_begin(f, "ensoniq", 1); ensoniq_A2GSPU_snapshot(g_a2gspu_ensoniq, f); a2gspu_snap_dev_end(f, at);
        names += names.empty() ? "ensoniq" : ",ensoniq";
    }
    display_state_t *ds = (display_state_t *)computer->get_module_state(MODULE_DISPLAY);
    if (ds && ds->video_scanner_type == Scanner_AppleIIgs) {
        long at = a2gspu_snap_dev_begin(f, "vgcint", 1); display_A2GSPU_snapshot(ds, f); a2gspu_snap_dev_end(f, at);
        names += names.empty() ? "vgcint" : ",vgcint";
    }
    if (ds && ds->video_scanner) {
        long at = a2gspu_snap_dev_begin(f, "scanner", 1);
        display_A2GSPU_new_video_snapshot(ds, f);          // the display's NEWVIDEO first
        ds->video_scanner->A2GSPU_snapshot(f);
        a2gspu_snap_dev_end(f, at);
        names += names.empty() ? "scanner" : ",scanner";
    }
    if (g_a2gspu_iwm && g_a2gspu_iwm->iwm) {
        long at = a2gspu_snap_dev_begin(f, "iwm", 1); iwm_A2GSPU_snapshot(g_a2gspu_iwm, f); a2gspu_snap_dev_end(f, at);
        names += names.empty() ? "iwm" : ",iwm";
    }
    // Every Disk II controller, by slot; each entry length-prefixed, so a machine
    // without a controller in that slot steps over it.
    int ndiskii = 0;
    for (int s = 1; s < 8; s++) if (g_a2gspu_diskii[s]) ndiskii++;
    if (ndiskii) {
        long at = a2gspu_snap_dev_begin(f, "diskii", 1);
        uint8_t cnt = (uint8_t)ndiskii; fwrite(&cnt, 1, 1, f);
        for (int s = 1; s < 8; s++) if (g_a2gspu_diskii[s]) {
            uint8_t sl = (uint8_t)s; fwrite(&sl, 1, 1, f);
            long la = a2gspu_snap_len_begin(f); g_a2gspu_diskii[s]->A2GSPU_snapshot(f); a2gspu_snap_len_end(f, la);
        }
        a2gspu_snap_dev_end(f, at);
        names += names.empty() ? "diskii" : ",diskii";
    }
    // The disks behind the heads: every track of a disk written since it was
    // mounted, for the IWM's drives and each Disk II's, length-prefixed likewise.
    if ((g_a2gspu_iwm && g_a2gspu_iwm->iwm) || ndiskii) {
        long at = a2gspu_snap_dev_begin(f, "media", 1);
        uint8_t has_iwm = (g_a2gspu_iwm && g_a2gspu_iwm->iwm) ? 1 : 0; fwrite(&has_iwm, 1, 1, f);
        if (has_iwm) { long la = a2gspu_snap_len_begin(f); iwm_A2GSPU_media_snapshot(g_a2gspu_iwm, f); a2gspu_snap_len_end(f, la); }
        uint8_t cnt = (uint8_t)ndiskii; fwrite(&cnt, 1, 1, f);
        for (int s = 1; s < 8; s++) if (g_a2gspu_diskii[s]) {
            uint8_t sl = (uint8_t)s; fwrite(&sl, 1, 1, f);
            long la = a2gspu_snap_len_begin(f); g_a2gspu_diskii[s]->A2GSPU_media_snapshot(f); a2gspu_snap_len_end(f, la);
        }
        a2gspu_snap_dev_end(f, at);
        names += names.empty() ? "media" : ",media";
    }
    if (pdblock3_A2GSPU_present()) {
        long at = a2gspu_snap_dev_begin(f, "pdblock", 1); pdblock3_A2GSPU_snapshot(f); a2gspu_snap_dev_end(f, at);
        names += names.empty() ? "pdblock" : ",pdblock";
    }
    return names;
}
// Reads device blocks up to the sentinel, which it leaves for the caller. A block
// this binary does not know, or for a device this machine does not have, is
// stepped over and named in `skipped`; a block that fails to restore is fatal.
std::string g_snap_dev_restored, g_snap_dev_skipped;
bool a2gspu_snap_read_devices(FILE *f, computer_t *computer) {
    g_snap_dev_restored.clear(); g_snap_dev_skipped.clear();
    for (;;) {
        uint32_t magic = 0;
        if (fread(&magic, 4, 1, f) != 1) return false;
        if (magic != A2GSPU_SNAP_DEV) { fseek(f, -4, SEEK_CUR); return true; }   // the sentinel (or damage): the caller checks
        char nm[9] = {0}; uint32_t ver = 0, len = 0;
        if (fread(nm, 1, 8, f) != 8 || fread(&ver, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1) return false;
        const long payload = ftell(f);
        enum { ABSENT, RESTORED, FAILED } r = ABSENT;
        if (!strcmp(nm, "adb") && ver == 1) {
            keygloo_state_t *kg = (keygloo_state_t *)computer->get_module_state(MODULE_KEYGLOO);
            if (kg && kg->kg) {
                r = kg->kg->A2GSPU_restore(f) ? RESTORED : FAILED;
                if (r == RESTORED) keygloo_update_interrupt_status(kg, kg->kg);
            }
        } else if (!strcmp(nm, "ensoniq") && ver == 1) {
            if (g_a2gspu_ensoniq) r = ensoniq_A2GSPU_restore(g_a2gspu_ensoniq, f) ? RESTORED : FAILED;
        } else if (!strcmp(nm, "vgcint") && ver == 1) {
            display_state_t *ds = (display_state_t *)computer->get_module_state(MODULE_DISPLAY);
            if (ds && ds->video_scanner_type == Scanner_AppleIIgs) r = display_A2GSPU_restore(ds, f) ? RESTORED : FAILED;
        } else if (!strcmp(nm, "scanner") && ver == 1) {
            // Class B (M5). A block from a different scanner type does not fit this
            // one's layout; the length check below realigns and the read fails loud.
            display_state_t *ds = (display_state_t *)computer->get_module_state(MODULE_DISPLAY);
            if (ds && ds->video_scanner) {
                r = (display_A2GSPU_new_video_restore(ds, f) && ds->video_scanner->A2GSPU_restore(f)) ? RESTORED : FAILED;
                if (r == RESTORED && ftell(f) != payload + (long)len) r = FAILED;   // not this scanner's layout
            }
        } else if (!strcmp(nm, "iwm") && ver == 1) {
            if (g_a2gspu_iwm && g_a2gspu_iwm->iwm) {
                int mismatch = 0;
                r = iwm_A2GSPU_restore(g_a2gspu_iwm, f, &mismatch) ? RESTORED : FAILED;
                if (r == RESTORED && mismatch)
                    a2gspu_warn("A2GSPU SNAP", "iwm: a drive holds a different image than the snapshot's; its head state was left alone "
                                "(mount what the manifest's run mounted)");
            }
        } else if (!strcmp(nm, "diskii") && ver == 1) {
            // Each saved controller applies to this machine's controller in the same
            // slot; one this machine lacks is stepped over and said.
            int mismatch = 0, absent = 0;
            uint8_t cnt = 0;
            bool ok = fread(&cnt, 1, 1, f) == 1;
            for (int i = 0; ok && i < cnt; i++) {
                uint8_t sl = 0; uint32_t l = 0;
                ok = fread(&sl, 1, 1, f) == 1 && fread(&l, 4, 1, f) == 1;
                if (!ok) break;
                const long start = ftell(f);
                if (sl < 8 && g_a2gspu_diskii[sl]) ok = g_a2gspu_diskii[sl]->A2GSPU_restore(f, &mismatch) && ftell(f) == start + (long)l;
                else { absent++; ok = fseek(f, start + (long)l, SEEK_SET) == 0; }
            }
            r = ok ? RESTORED : FAILED;
            if (r == RESTORED && cnt && absent == cnt) r = ABSENT;   // nothing here to apply it to: skipped= says so
            if (r == RESTORED && mismatch)
                a2gspu_warn("A2GSPU SNAP", "diskii: a drive holds a different image than the snapshot's; its head state was left alone "
                            "(mount what the manifest's run mounted)");
            if (r == RESTORED && absent)
                a2gspu_warn("A2GSPU SNAP", "diskii: a saved controller's slot has no Disk II in this machine; that controller was not applied");
        } else if (!strcmp(nm, "media") && ver == 1) {
            int mismatch = 0, applied = 0, absent = 0;
            uint8_t has_iwm = 0; uint32_t l = 0;
            bool ok = fread(&has_iwm, 1, 1, f) == 1;
            if (ok && has_iwm) {
                ok = fread(&l, 4, 1, f) == 1;
                const long start = ftell(f);
                if (ok && g_a2gspu_iwm && g_a2gspu_iwm->iwm) ok = iwm_A2GSPU_media_restore(g_a2gspu_iwm, f, &mismatch, &applied) && ftell(f) == start + (long)l;
                else if (ok) { absent++; ok = fseek(f, start + (long)l, SEEK_SET) == 0; }
            }
            uint8_t cnt = 0;
            ok = ok && fread(&cnt, 1, 1, f) == 1;
            for (int i = 0; ok && i < cnt; i++) {
                uint8_t sl = 0;
                ok = fread(&sl, 1, 1, f) == 1 && fread(&l, 4, 1, f) == 1;
                if (!ok) break;
                const long start = ftell(f);
                if (sl < 8 && g_a2gspu_diskii[sl]) ok = g_a2gspu_diskii[sl]->A2GSPU_media_restore(f, &mismatch, &applied) && ftell(f) == start + (long)l;
                else { absent++; ok = fseek(f, start + (long)l, SEEK_SET) == 0; }
            }
            r = ok ? RESTORED : FAILED;
            if (r == RESTORED && absent && absent == (int)cnt + (has_iwm ? 1 : 0)) r = ABSENT;
            if (r == RESTORED && applied)
                printf("A2GSPU SNAP: media: %d written disk(s) restored track by track\n", applied);
            if (r == RESTORED && mismatch)
                a2gspu_warn("A2GSPU SNAP", "media: a written disk's tracks were not applied -- the drive holds a different "
                            "image, or the same name with a different track layout; that disk reads what its file holds");
            if (r == RESTORED && absent)
                a2gspu_warn("A2GSPU SNAP", "media: a saved controller's disks have no controller to go to in this machine");
        } else if (!strcmp(nm, "pdblock") && ver == 1) {
            if (pdblock3_A2GSPU_present()) {
                int mismatch = 0;
                r = pdblock3_A2GSPU_restore(f, &mismatch) ? RESTORED : FAILED;
                if (r == RESTORED && mismatch)
                    a2gspu_warn("A2GSPU SNAP", "pdblock: a unit's overlay was not applied -- a different image or size than the "
                                "snapshot's, or the unit is not in RAM mode (A2GSPU_RAMDISK); the volume is what the file holds");
            }
        }
        if (r == FAILED) return false;
        std::string &list = (r == RESTORED) ? g_snap_dev_restored : g_snap_dev_skipped;
        if (!list.empty()) list += ",";
        list += nm;
        fseek(f, payload + (long)len, SEEK_SET);   // realign whether the block was read, skipped, or unknown
    }
}
void a2gspu_snap_print_devices() {
    printf("A2GSPU SNAP: devices restored=%s skipped=%s\n",
           g_snap_dev_restored.empty() ? "-" : g_snap_dev_restored.c_str(),
           g_snap_dev_skipped.empty() ? "-" : g_snap_dev_skipped.c_str());
    if (!g_snap_dev_skipped.empty())
        a2gspu_warn("A2GSPU SNAP", "a device block in the snapshot was not restored (devices skipped= names it): "
                    "this binary does not know it or this machine does not have it; that device starts from reset");
}
void a2gspu_snap_write_manifest(FILE *f, const std::string &m) {
    uint32_t magic = A2GSPU_SNAP_MANIFEST, n = (uint32_t)m.size();
    fwrite(&magic, 4, 1, f); fwrite(&n, 4, 1, f); fwrite(m.data(), 1, n, f);
}
// Empty when the snapshot predates manifests (or the trailer is damaged).
std::string a2gspu_snap_read_manifest(FILE *f) {
    uint32_t magic = 0, n = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != A2GSPU_SNAP_MANIFEST) return std::string();
    if (fread(&n, 4, 1, f) != 1 || n == 0 || n > 4096) return std::string();
    std::string m(n, '\0');
    if (fread(&m[0], 1, n, f) != n) return std::string();
    return m;
}
void a2gspu_snap_print_manifest(const std::string &m) {
    if (m.empty())
        printf("A2GSPU SNAP: manifest absent (pre-manifest snapshot) -- class A assumed: cpu, mmu, soft switches, "
               "RAM; every device excluded (adb, ensoniq, vgcint, iwm, media, scanner, rtc, serial, clock). A restored machine "
               "does not poll the keyboard and has no sound state.\n");
    else
        printf("A2GSPU SNAP: manifest %s\n", m.c_str());
}

// Trailing snapshot sentinel: a magic word written LAST (after the cpu + mmu blocks)
// on save, and required to read back exactly on load. The per-field checked-fread above
// catches a truncation that lands inside a known field; the sentinel additionally catches
// a snapshot truncated at/after the final field (e.g. the mmu block cut short, or a save
// interrupted right before its last bytes) — the load would otherwise hit EOF only after
// every field happened to read, and restore a silently-incomplete machine as success.
void a2gspu_snap_write_sentinel(FILE *f) {
    uint32_t end = A2GSPU_SNAP_END;
    fwrite(&end, 4, 1, f);
}
bool a2gspu_snap_check_sentinel(FILE *f) {
    uint32_t end = 0;
    if (fread(&end, 4, 1, f) != 1) return false;
    return end == A2GSPU_SNAP_END;
}


// A2GSPU_SAVE_AT host callback (registered into g_save_at_fn) — writes the SAME
// snapshot the SNAP_SAVE path writes, but at the EXACT break instruction (invoked
// from iigs_tb_on_landing). Reuses a2gspu_cpu_save + MMU_IIgs::A2GSPU_snapshot +
// the trailing sentinel; the mmu handle + path are captured at parse time.
MMU_IIgs *g_saveat_mmu = nullptr;
computer_t *g_saveat_computer = nullptr;
char      g_saveat_path[512] = "snap_at.bin";
void a2gspu_save_at_cb(cpu_state *cpu) {
    FILE *sf = fopen(g_saveat_path, "wb");
    if (!sf) { printf("A2GSPU SAVE_AT: could not open '%s' for save\n", g_saveat_path); return; }
    if (!g_saveat_mmu) { a2gspu_warn("A2GSPU SAVE_AT", "no IIgs MMU (use -p 5) -- skipped"); fclose(sf); return; }
    a2gspu_cpu_save(sf, cpu);
    g_saveat_mmu->A2GSPU_snapshot(sf);
    const std::string devs = g_saveat_computer ? a2gspu_snap_write_devices(sf, g_saveat_computer) : std::string();
    a2gspu_snap_write_sentinel(sf);
    a2gspu_snap_write_manifest(sf, a2gspu_snap_manifest_text(g_iigs_cur_frame, devs));
    fclose(sf);
    printf("A2GSPU SAVE_AT: snapshot at %02X/%04X -> '%s'\n", cpu->pb, cpu->pc, g_saveat_path);
}


// Instruction-granular step helpers: a2ctrl::arm_cpu / step_one / format_cpu_line
// (a2gspu_ctrl_cmds.hpp). run/step/run-until/keys/press/key: try_exec / try_input.

bool a2gspu_ctrl_snap_save(const char *path, computer_t *computer, GS2AppState *state) {
    FILE *sf = fopen(path, "wb");
    if (!sf) return false;
    bool ok = false;
    a2gspu_cpu_save(sf, computer->cpu);
    if (state->mmu_iigs) { state->mmu_iigs->A2GSPU_snapshot(sf); ok = true; }
    else if (MMU_IIe *m = dynamic_cast<MMU_IIe *>(rail_mmu(computer))) { m->A2GSPU_snapshot(sf); ok = true; }
    if (ok) {
        const std::string devs = a2gspu_snap_write_devices(sf, computer);
        a2gspu_snap_write_sentinel(sf);
        a2gspu_snap_write_manifest(sf, a2gspu_snap_manifest_text(g_iigs_cur_frame, devs));
    }
    fclose(sf);
    if (!ok) remove(path);
    return ok;
}

// Restores into the live machine, in order, and reports the first step that
// refused as *stage (cpu | mmu | devices | sentinel). On false the machine may be
// partly overwritten -- the caller owns the rollback.
bool a2gspu_ctrl_snap_restore(const char *path, computer_t *computer, GS2AppState *state,
                                     std::string &manifest, const char **stage) {
    FILE *sf = fopen(path, "rb");
    if (!sf) { *stage = "open"; return false; }
    bool ok = a2gspu_cpu_load(sf, computer->cpu);
    if (!ok) *stage = "cpu";
    if (ok) {
        if (state->mmu_iigs) ok = state->mmu_iigs->A2GSPU_restore(sf);
        else if (MMU_IIe *m = dynamic_cast<MMU_IIe *>(rail_mmu(computer))) ok = m->A2GSPU_restore(sf);
        else ok = false;
        if (!ok) *stage = "mmu";
    }
    if (ok) { ok = a2gspu_snap_read_devices(sf, computer); if (!ok) *stage = "devices"; }
    if (ok) { ok = a2gspu_snap_check_sentinel(sf); if (!ok) *stage = "sentinel"; }
    if (ok) manifest = a2gspu_snap_read_manifest(sf);
    fclose(sf);
    return ok;
}
