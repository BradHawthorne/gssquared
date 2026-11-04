add_executable(writelogtest main.cpp)

target_link_libraries(writelogtest PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)

add_test(NAME cpu_write_log COMMAND writelogtest)
