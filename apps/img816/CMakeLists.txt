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

# Bus access log (--buslog, src/a2gspu_buslog.hpp). buslogcheck parses a log on its own
# and checks it: one record per CPU cycle, as many records as the runner's cycle count,
# the 14M costs adding up to the clock's own 14M advance, and (optionally) hand-derived
# records and summaries.
add_executable(buslogcheck buslogcheck.cpp)

# The IIgs map: fast RAM, $E0/$E1, $C0xx, a shadowed write, the 1 MHz machine, a scan line.
add_test(NAME img816_buslog_iigs COMMAND img816 --machine iigs --buslog ${CMAKE_CURRENT_BINARY_DIR}/buslog_iigs.txt
         --check ${CMAKE_CURRENT_SOURCE_DIR}/buslog_cases.expected ${CMAKE_CURRENT_SOURCE_DIR}/buslog_cases.txt)
set_tests_properties(img816_buslog_iigs PROPERTIES FIXTURES_SETUP buslog_iigs)
add_test(NAME img816_buslog_iigs_records COMMAND buslogcheck ${CMAKE_CURRENT_BINARY_DIR}/buslog_iigs.txt
         ${CMAKE_CURRENT_SOURCE_DIR}/buslog.expected)
set_tests_properties(img816_buslog_iigs_records PROPERTIES FIXTURES_REQUIRED buslog_iigs)
# Negative control: one hand-derived record is off (wait 7, not 8), so the check must fail.
add_test(NAME img816_buslog_rejects_wrong_record COMMAND buslogcheck ${CMAKE_CURRENT_BINARY_DIR}/buslog_iigs.txt
         ${CMAKE_CURRENT_SOURCE_DIR}/buslog.wrong)
set_tests_properties(img816_buslog_rejects_wrong_record PROPERTIES FIXTURES_REQUIRED buslog_iigs WILL_FAIL TRUE)

# The hand-checked flat cases, logged on both machines: the case lines (cycle counts
# included) must still match cases.expected, and each log must hold exactly one record
# per counted cycle.
add_test(NAME img816_buslog_flat COMMAND img816 --buslog ${CMAKE_CURRENT_BINARY_DIR}/buslog_flat.txt
         --check ${CMAKE_CURRENT_SOURCE_DIR}/cases.expected ${CMAKE_CURRENT_SOURCE_DIR}/cases.txt)
set_tests_properties(img816_buslog_flat PROPERTIES FIXTURES_SETUP buslog_flat)
add_test(NAME img816_buslog_flat_records COMMAND buslogcheck ${CMAKE_CURRENT_BINARY_DIR}/buslog_flat.txt)
set_tests_properties(img816_buslog_flat_records PROPERTIES FIXTURES_REQUIRED buslog_flat)
add_test(NAME img816_buslog_iigs_same_image COMMAND img816 --machine iigs
         --buslog ${CMAKE_CURRENT_BINARY_DIR}/buslog_iigs_cases.txt
         --check ${CMAKE_CURRENT_SOURCE_DIR}/cases.expected ${CMAKE_CURRENT_SOURCE_DIR}/cases.txt)
set_tests_properties(img816_buslog_iigs_same_image PROPERTIES FIXTURES_SETUP buslog_iigs_cases)
add_test(NAME img816_buslog_iigs_same_image_records COMMAND buslogcheck ${CMAKE_CURRENT_BINARY_DIR}/buslog_iigs_cases.txt)
set_tests_properties(img816_buslog_iigs_same_image_records PROPERTIES FIXTURES_REQUIRED buslog_iigs_cases)

# One access per cycle: a second access pended in one CPU cycle is counted (overlap=)
# and, built with A2_BUSLOG_ASSERT, asserts. buslogunit checks the counter, the report
# and the cap ceiling; buslogunit_assert is the same header with the assertion on and
# must stop on it. buslogcheck must refuse a log whose end line
# reports an overlap (buslog.overlap, a planted log).
add_executable(buslogunit buslogunit.cpp)
target_include_directories(buslogunit PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME buslog_unit COMMAND buslogunit)
add_executable(buslogunit_assert buslogunit.cpp)
target_include_directories(buslogunit_assert PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(buslogunit_assert PRIVATE BUSLOGUNIT_ASSERT)
add_test(NAME buslog_unit_assert COMMAND buslogunit_assert)
set_tests_properties(buslog_unit_assert PROPERTIES
    PASS_REGULAR_EXPRESSION "a second bus access was pended in one CPU cycle"
    FAIL_REGULAR_EXPRESSION "did not assert")
add_test(NAME img816_buslog_rejects_overlap COMMAND buslogcheck ${CMAKE_CURRENT_SOURCE_DIR}/buslog.overlap)
set_tests_properties(img816_buslog_rejects_overlap PROPERTIES
    PASS_REGULAR_EXPRESSION "pended a second bus access")
