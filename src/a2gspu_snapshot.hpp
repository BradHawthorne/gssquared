#pragma once
// Snapshot file I/O: the CPU block, the MMU block (in the MMU classes), the named
// device blocks, the sentinel and the manifest trailer; the transactional CTRL
// save/restore; and the SAVE_AT breakpoint-checkpoint. See Docs/A2GSPU_RAILS.md.
#include <cstdio>
#include <string>
struct cpu_state; struct computer_t; struct GS2AppState; class MMU_IIgs;

inline constexpr uint32_t A2GSPU_SNAP_MAGIC = 0x47535053;  // 'GSPS'
inline constexpr uint32_t A2GSPU_SNAP_VER = 1;
inline constexpr uint32_t A2GSPU_SNAP_MANIFEST = 0x4D505347;  // 'GSPM'
inline constexpr uint32_t A2GSPU_SNAP_DEV = 0x56445347;  // 'GSDV'
inline constexpr uint32_t A2GSPU_SNAP_END = 0x444E4553;  // 'SEND'

void        a2gspu_cpu_save(FILE *f, cpu_state *cpu);
bool        a2gspu_cpu_load(FILE *f, cpu_state *cpu);
std::string a2gspu_snap_manifest_text(int frame, const std::string &devices);
std::string a2gspu_snap_write_devices(FILE *f, computer_t *computer);
bool        a2gspu_snap_read_devices(FILE *f, computer_t *computer);
void        a2gspu_snap_print_devices();
void        a2gspu_snap_write_manifest(FILE *f, const std::string &m);
std::string a2gspu_snap_read_manifest(FILE *f);
void        a2gspu_snap_print_manifest(const std::string &m);
void        a2gspu_snap_write_sentinel(FILE *f);
bool        a2gspu_snap_check_sentinel(FILE *f);
bool        a2gspu_ctrl_snap_save(const char *path, computer_t *computer, GS2AppState *state);
bool        a2gspu_ctrl_snap_restore(const char *path, computer_t *computer, GS2AppState *state,
                                     std::string &manifest, const char **stage);
void        a2gspu_save_at_cb(cpu_state *cpu);
extern std::string g_snap_dev_restored, g_snap_dev_skipped;
extern MMU_IIgs   *g_saveat_mmu;
extern computer_t *g_saveat_computer;
extern char        g_saveat_path[512];
