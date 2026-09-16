#pragma once
/*
 * media_identity.hpp -- which image a drive holds, as a snapshot records it.
 *
 * FORK (fidelity study, storage section 7). A snapshot named a drive's image by its
 * file name alone. The same image copied to another path -- a harness's fresh working
 * directory -- would not restore, and a different image left at the same path was
 * given another disk's head position and written tracks.
 *
 * The identity is now the name plus a house-FNV of the file's bytes as they were when
 * the image was mounted. When both the snapshot and the drive have that hash, it alone
 * decides, whatever the path. The name decides only when either side has none: a block
 * written before the hash existed (version 1), or a file that could not be read back.
 */
#include <cstdint>
#include <cstdio>
#include <string>
#include "house_fnv.hpp"
#include "util/media.hpp"

// The identity layout the restore dispatcher is reading: 1 = the name only, 2 = the name
// and the content hash. Writers always write 2; the dispatcher sets this from the block's
// version before handing the block to a device.
inline int g_a2gspu_media_id_version = 2;

// Hash the image's host file as it is now. Called at mount, so the identity is the
// image as mounted, whatever later writes do to the file.
inline void media_identity_capture(media_descriptor *md) {
    if (!md) return;
    md->content_fnv_valid = false;
    md->content_fnv = 0;
    FILE *f = fopen(md->filename.c_str(), "rb");
    if (!f) return;
    uint64_t h = HOUSE_FNV_BASIS;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        for (size_t i = 0; i < n; i++) { h ^= buf[i]; h *= HOUSE_FNV_PRIME; }
    const bool ok = !ferror(f);
    fclose(f);
    if (!ok) return;
    md->content_fnv = h;
    md->content_fnv_valid = true;
}

struct media_identity_t {
    std::string name;
    bool has_fnv = false;
    uint64_t fnv = 0;
};

// `md` null: nothing mounted.
inline void media_identity_write(FILE *f, const media_descriptor *md) {
    const std::string name = md ? md->filename : std::string();
    uint32_t n = (uint32_t)name.size(); fwrite(&n, 4, 1, f); if (n) fwrite(name.data(), 1, n, f);
    const uint8_t has = (md && md->content_fnv_valid) ? 1 : 0; fwrite(&has, 1, 1, f);
    const uint64_t h = has ? md->content_fnv : 0; fwrite(&h, 8, 1, f);
}

inline bool media_identity_read(FILE *f, media_identity_t &id) {
    uint32_t n = 0; if (fread(&n, 4, 1, f) != 1 || n > 4096) return false;
    id.name.assign(n, '\0'); if (n && fread(&id.name[0], 1, n, f) != n) return false;
    id.has_fnv = false; id.fnv = 0;
    if (g_a2gspu_media_id_version >= 2) {
        uint8_t has = 0;
        if (fread(&has, 1, 1, f) != 1 || fread(&id.fnv, 8, 1, f) != 1) return false;
        id.has_fnv = has != 0;
    }
    return true;
}

// Does the drive (`now` null: nothing mounted) hold the image the snapshot saved?
inline bool media_identity_matches(const media_identity_t &saved, const media_descriptor *now) {
    if (!now) return saved.name.empty();
    if (saved.name.empty()) return false;
    if (saved.has_fnv && now->content_fnv_valid) return saved.fnv == now->content_fnv;
    return saved.name == now->filename;
}
