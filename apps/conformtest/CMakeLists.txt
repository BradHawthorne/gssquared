# conformtest -- owned per-opcode SEMANTIC conformance test (results/flags/memory).
# Companion to cycletest, which covers timing only. Same link set.
add_executable(conformtest main.cpp)

target_link_libraries(conformtest PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)
