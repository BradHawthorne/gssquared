add_executable(img816 main.cpp)

target_link_libraries(img816 PRIVATE
    gs2_mmu
    gs2_cpu
    gs2_cpu_new
    gs2_debugger
    gs2_video_scanner
)

# Hand-checked cases (expected cycle counts from the WDC datasheet tables).
add_test(NAME img816_cases COMMAND img816 --check ${CMAKE_CURRENT_SOURCE_DIR}/cases.expected
         ${CMAKE_CURRENT_SOURCE_DIR}/cases.txt)
# Negative control: one expected cycle count is off by one, so the check must fail.
add_test(NAME img816_check_rejects_wrong_cycles COMMAND img816 --check ${CMAKE_CURRENT_SOURCE_DIR}/cases.wrong
         ${CMAKE_CURRENT_SOURCE_DIR}/cases.txt)
set_tests_properties(img816_check_rejects_wrong_cycles PROPERTIES WILL_FAIL TRUE)
