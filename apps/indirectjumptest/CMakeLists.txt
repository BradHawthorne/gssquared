add_executable(indirectjumptest main.cpp)

target_link_libraries(indirectjumptest PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)

add_test(NAME cpu_indirect_jump COMMAND indirectjumptest ${CMAKE_CURRENT_SOURCE_DIR}/cases.txt)
