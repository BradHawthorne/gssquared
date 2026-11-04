add_executable(assertregtest main.cpp)

target_link_libraries(assertregtest PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)

add_test(NAME assert_registers COMMAND assertregtest)
