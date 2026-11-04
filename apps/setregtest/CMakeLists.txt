add_executable(setregtest main.cpp)

target_link_libraries(setregtest PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)

add_test(NAME ctrl_setreg_banks COMMAND setregtest)
