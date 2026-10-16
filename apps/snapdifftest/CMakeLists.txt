add_executable(snapdiff main.cpp)
add_test(NAME snapshot_diff COMMAND snapdiff --selftest)
