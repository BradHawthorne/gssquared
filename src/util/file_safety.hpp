#pragma once
/*
 * file_safety.hpp -- writing a user's disk image without being able to destroy it.
 *
 * FORK. Every writer here produces the complete new file under a temporary name in
 * the same directory, flushes it to disk, and only then renames it over the
 * original. A crash, a full disk or a failed write leaves the original exactly as it
 * was; the rename is the only step that changes it, and it either happens or does
 * not. The in-place writers these replace truncated the file first ("wb") or patched
 * it in place ("r+b"), so any failure part-way left a damaged image behind.
 */
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace file_safety {

inline bool flush_to_disk(FILE *fp) {
    if (fflush(fp) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(fp)) == 0;
#else
    return fsync(fileno(fp)) == 0;
#endif
}

// Replace `path` with `data`, atomically. On failure the original is untouched and
// `err` says why.
inline bool atomic_write(const std::string &path, const uint8_t *data, size_t size, std::string &err) {
    const std::string tmp = path + ".gs2tmp";
    FILE *fp = fopen(tmp.c_str(), "wb");
    if (!fp) { err = "cannot create '" + tmp + "'"; return false; }
    const bool wrote = size == 0 || fwrite(data, 1, size, fp) == size;
    const bool flushed = wrote && flush_to_disk(fp);
    fclose(fp);
    std::error_code ec;
    if (!wrote || !flushed) {
        std::filesystem::remove(tmp, ec);
        err = std::string(wrote ? "flush" : "write") + " failed on '" + tmp + "'";
        return false;
    }
    std::filesystem::rename(tmp, path, ec);   // replaces an existing file
    if (ec) {
        std::filesystem::remove(tmp, ec);
        err = "cannot replace '" + path + "': " + ec.message();
        return false;
    }
    return true;
}

// Write `size` bytes at `offset` into a copy of `path` (whatever else the file holds,
// such as a 2MG header, is kept), then replace the original with that copy.
inline bool atomic_patch(const std::string &path, uint64_t offset, const uint8_t *data, size_t size,
                         std::string &err) {
    std::vector<uint8_t> whole;
    if (FILE *in = fopen(path.c_str(), "rb")) {
        uint8_t buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) whole.insert(whole.end(), buf, buf + n);
        const bool read_error = ferror(in) != 0;
        fclose(in);
        if (read_error) { err = "read error on '" + path + "'"; return false; }
    }
    if (whole.size() < offset + size) whole.resize(offset + size, 0);
    std::copy(data, data + size, whole.begin() + static_cast<std::ptrdiff_t>(offset));
    return atomic_write(path, whole.data(), whole.size(), err);
}

}  // namespace file_safety
