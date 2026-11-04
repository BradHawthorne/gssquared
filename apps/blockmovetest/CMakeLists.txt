add_executable(blockmovetest main.cpp)

target_link_libraries(blockmovetest PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)

add_test(NAME cpu_block_move COMMAND blockmovetest ${CMAKE_CURRENT_SOURCE_DIR}/cases.txt)
