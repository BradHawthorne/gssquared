#pragma once
// ============================================================================
// a2gspu_artifact_meta.hpp -- a self-description beside every raw artifact (M6).
//
// WHY: the headless run writes raw binary artifacts -- the Mega II $E1 image, the
// bus-trace, slot-bus and MMU-state streams -- and `vram` writes a 16K dump. None
// has a header, and a consumer's harness reads spike_e1.bin as 65,536 raw bytes,
// so a header cannot be added without breaking it. That left each file a black
// box of the Event E kind: a reader had to know the record layout from the C++,
// and nothing let it prove it had read what was written (the coverage bitmap was
// misread for a week that way).
//
// So the bytes stay exactly as they were, and each artifact gets a sidecar,
// <file>.meta.json: one record in the record contract's envelope saying what the
// file is, how its records are laid out, how many there are, the writer's own
// content hash (and what that hash covers), and the house-FNV of the file bytes.
// tools/a2rail/artifacts.py `raw <file>` is the reader: it refuses an artifact
// with no sidecar, an unknown format, a length that is not count x record size,
// or a hash that does not re-derive.
//
// file_hash is integrity (these bytes, padding included); content_hash is the
// determinism witness the writer already printed (the fields, never padding).
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <string>
#include "a2record.hpp"
#include "house_fnv.hpp"
#include "a2gspu_artifact_io.hpp"
#include <limits>

inline uint64_t a2gspu_artifact_file_fnv(const char *path, uint64_t &bytes) {
    uint64_t h = HOUSE_FNV_BASIS;
    bytes = 0;
    errno=0;FILE *f = fopen(path, "rb");
    if (!f) throw a2artifact::Error("read-open",path);
    uint8_t buf[65536];
    size_t n;
    while ((errno=0, n = fread(buf, 1, sizeof buf, f)) > 0) {
        for (size_t i = 0; i < n; i++) { h ^= buf[i]; h *= HOUSE_FNV_PRIME; }
        bytes += n;
    }
    const bool failed=ferror(f)!=0;const int read_error=errno;
    errno=0;const int closed=fclose(f);const int close_error=errno;
    if(failed || closed!=0)throw a2artifact::Error(failed?"read":"read-close",path,failed?read_error:close_error);
    return h;
}

// Write <path>.meta.json and print one SPIKE ARTIFACT line. `layout` names the
// record fields in order (little-endian); `hash_covers` says what content_hash is
// taken over, so a reader can re-derive it without reading this file's source.
// Returns false (and says so) when the artifact or the sidecar cannot be read or
// written -- an artifact without its description is exactly what this exists to
// prevent, so it is not a silent case.
inline bool a2gspu_artifact_meta(const char *path, const char *format, unsigned record_size, uint64_t count,
                                 const char *layout, const char *hash_covers, uint64_t content_hash,
                                 int page = -1, a2artifact::WriteOps ops = {}) {
    const std::string mp = std::string(path) + ".meta.json";
    bool opened=false;
    try {
    if(!record_size || count>(std::numeric_limits<uint64_t>::max)()/record_size)
        throw a2artifact::Error("invalid-record-size-or-count",path,0);
    uint64_t bytes = 0;
    const uint64_t fh = a2gspu_artifact_file_fnv(path, bytes);
    if(bytes!=(uint64_t)record_size*count)throw a2artifact::Error("length-not-count-times-record-size",path,0);
    a2artifact::Output meta(mp.c_str(),ops);opened=true;
    char h1[20], h2[20];
    snprintf(h1, sizeof h1, "%016llX", (unsigned long long)content_hash);
    snprintf(h2, sizeof h2, "%016llX", (unsigned long long)fh);
    a2record::Record rec("artifact", "meta", a2record::frame_now(), a2record::cycles_now());
    rec.str("schema", "a2artifact-v1").str("format", format)
       .u("record_size", record_size).u("count", (unsigned long long)count).u("bytes", (unsigned long long)bytes)
       .str("layout", layout).str("content_hash", h1).str("content_hash_covers", hash_covers)
       .str("file_hash", h2).str("file_hash_covers", "house-FNV-1a-64 over every byte of the file");
    if (page >= 0) rec.i("page", page);
    if(!rec.write_checked(meta.get(),true))throw a2artifact::Error("sidecar-write-or-flush",mp.c_str());
    meta.finish();
    printf("SPIKE ARTIFACT: %s format=%s count=%llu bytes=%llu content-hash=%s file-hash=%s meta=%s\n",
           path, format, (unsigned long long)count, (unsigned long long)bytes, h1, h2, mp.c_str());
    return true;
    } catch(const a2artifact::Error &error) {
        if(opened && std::remove(mp.c_str())!=0)
            std::fprintf(stderr,"SPIKE ARTIFACT: cannot remove failed sidecar %s\n",mp.c_str());
        printf("SPIKE ARTIFACT: %s NO-DESCRIPTION -- %s\n",path,error.what());
        return false;
    }
}

inline void a2gspu_artifact_require_meta(const char *path,const char *format,unsigned record_size,uint64_t count,
                                        const char *layout,const char *hash_covers,uint64_t content_hash,int page=-1) {
    if(!a2gspu_artifact_meta(path,format,record_size,count,layout,hash_covers,content_hash,page))
        throw a2artifact::Error("description",path,0);
}
