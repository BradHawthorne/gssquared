add_executable(sst816 main.cpp)

target_link_libraries(sst816 PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)
