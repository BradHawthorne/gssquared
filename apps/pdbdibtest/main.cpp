// pdbdibtest -- BazFast (pdblock3) SmartPort STATUS code $03 (the DIB) for an
// empty unit.
//
// GS/OS reads each unit's DIB once and copies the status byte into the device
// characteristics; it does not refresh write-allowed when media is mounted
// later. The firmware used to answer an empty unit with "device offline" and
// leave the caller's buffer as it was, so the empty unit inherited whatever the
// previous call left there: after a write-protected volume (an APM CD), every
// later volume came up write-protected. Upstream cf828d2a: an empty unit
// returns a full DIB, status $E0 (offline, write allowed), block count 0.
//
// The sequence below is that one: DIB of the write-protected unit 1 into a
// buffer, then DIB of the empty unit 2 into the same buffer, through the card's
// own command path (put / execute / get_error), standard and extended calls.
#include "devices/pdblock3/pdblock3.cpp"

#include <cstdio>
#include <filesystem>
#include <string>

gs2_app_t gs2_app_values;
uint64_t debug_level = 0;

class FlatMMU : public MMU {
public:
    std::vector<uint8_t> mem;
    FlatMMU() : MMU(256, 0x10000), mem(1u << 24, 0) {}
    uint8_t read(uint32_t a) override { return mem[a & 0xFFFFFF]; }
    uint8_t vp_read(uint32_t a) override { return mem[a & 0xFFFFFF]; }
    void write(uint32_t a, uint8_t v) override { mem[a & 0xFFFFFF] = v; }
    uint8_t probe_peek(uint32_t a) override { return mem[a & 0xFFFFFF]; }
};

static int failures = 0;
static void check(bool ok, const std::string &what) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) failures++;
}

static const uint32_t CMDBLK = 0x0300, CLIST = 0x0310, BUF = 0x0400;

// One SmartPort STATUS call through the card: the command block names the
// command list, the v2 buffer names the command block.
static uint8_t sp_status(PDBlock3 &pdb, FlatMMU &m, bool extended, uint8_t unit, uint8_t code) {
    if (extended) {
        m.write(CMDBLK, 0x40);
        m.write(CMDBLK + 1, CLIST & 0xFF); m.write(CMDBLK + 2, CLIST >> 8);
        m.write(CMDBLK + 3, 0); m.write(CMDBLK + 4, 0);
        const uint8_t cl[] = {3, unit, BUF & 0xFF, BUF >> 8, 0, 0, code};
        for (unsigned i = 0; i < sizeof cl; i++) m.write(CLIST + i, cl[i]);
    } else {
        m.write(CMDBLK, 0x00);
        m.write(CMDBLK + 1, CLIST & 0xFF); m.write(CMDBLK + 2, CLIST >> 8);
        const uint8_t cl[] = {3, unit, BUF & 0xFF, BUF >> 8, code};
        for (unsigned i = 0; i < sizeof cl; i++) m.write(CLIST + i, cl[i]);
    }
    pdb.reset();
    pdb.put(0x02); pdb.put(CMDBLK & 0xFF); pdb.put(CMDBLK >> 8);
    pdb.put((uint8_t)(0x02 ^ (CMDBLK & 0xFF) ^ (CMDBLK >> 8)));
    pdb.execute();
    return pdb.get_error();
}

static void run(PDBlock3 &pdb, FlatMMU &m, bool ext) {
    const char *tag = ext ? "extended" : "standard";
    const unsigned cnt = ext ? 4 : 3;           // block-count bytes
    auto blk = [&]() { uint32_t v = 0; for (unsigned i = 0; i < cnt; i++) v |= (uint32_t)m.mem[BUF + 1 + i] << (8 * i); return v; };
    const uint32_t idlen = BUF + 1 + cnt, id = idlen + 1, dtype = id + 16;

    for (uint32_t i = 0; i < 32; i++) m.mem[BUF + i] = 0x5A;
    uint8_t e = sp_status(pdb, m, ext, 1, 0x03);
    char d[160];
    snprintf(d, sizeof d, "%s: DIB of the write-protected unit 1: error 0, status $B4 (+switched), 16 blocks (err=$%02X st=$%02X blk=%u)",
             tag, e, m.mem[BUF], blk());
    // Bit 0 is disk-switched, set by the first status call after the mount.
    check(e == 0 && (m.mem[BUF] & 0xFE) == 0xB4 && blk() == 16, d);

    // Same buffer: unit 2 is empty. It must overwrite the status, not inherit $B4.
    e = sp_status(pdb, m, ext, 2, 0x03);
    snprintf(d, sizeof d, "%s: DIB of the empty unit 2: error 0, status $E0 (err=$%02X st=$%02X)", tag, e, m.mem[BUF]);
    check(e == 0 && m.mem[BUF] == 0xE0, d);
    snprintf(d, sizeof d, "%s: empty unit 2: block count 0 (blk=%u)", tag, blk());
    check(blk() == 0, d);
    std::string name((const char *)&m.mem[id], m.mem[idlen] <= 16 ? m.mem[idlen] : 16);
    check(name == "BAZFAST3B" && m.mem[dtype] == 0x02,
          std::string(tag) + ": empty unit 2: rest of the DIB is filled in (id '" + name + "', type $02)");

    // A unit past the card's range is still an error and writes nothing.
    for (uint32_t i = 0; i < 32; i++) m.mem[BUF + i] = 0x5A;
    e = sp_status(pdb, m, ext, PDB3_MAX_UNITS + 1, 0x03);
    snprintf(d, sizeof d, "%s: unit %d (no such unit): an error, buffer untouched (err=$%02X)", tag, PDB3_MAX_UNITS + 1, e);
    check(e != 0 && m.mem[BUF] == 0x5A, d);
}

int main() {
    namespace fs = std::filesystem;
    const fs::path img = fs::temp_directory_path() / "pdbdibtest_wp.po";
    {
        FILE *f = fopen(img.string().c_str(), "wb");
        if (!f) { printf("FAIL: cannot create %s\n", img.string().c_str()); return 1; }
        std::vector<uint8_t> z(16 * 512, 0);
        fwrite(z.data(), 1, z.size(), f);
        fclose(f);
    }
    media_descriptor md;
    md.filename = img.string();
    md.media_type = MEDIA_BLK;
    md.block_size = 512;
    md.block_count = 16;
    md.file_size = md.data_size = 16 * 512;
    md.write_protected = true;

    FlatMMU m;
    {
        PDBlock3 pdb(7, &m);
        storage_key_t key; key.slot = 7; key.drive = 0;
        check(pdb.mount(key, {&md}), "mount a 16-block write-protected image on unit 1");
        run(pdb, m, false);
        run(pdb, m, true);
    }
    std::error_code ec;
    fs::remove(img, ec);

    printf("\n%s: pdbdibtest - %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
