add_executable(sccltoudptest main.cpp)

target_link_libraries(sccltoudptest PRIVATE
    gs2_devices_scc8530
    gs2_util
)

add_test(NAME scc_ltoudp_wr3_inject COMMAND sccltoudptest)
