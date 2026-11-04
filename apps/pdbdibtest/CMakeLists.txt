# The card's class lives in pdblock3.cpp, so the test compiles that file itself
# and links what it uses (not gs2_devices_pdblock3, which would define it twice).
add_executable(pdbdibtest main.cpp)

target_link_libraries(pdbdibtest PRIVATE
    gs2_mmu
    gs2_util
    gs2_paths
)

add_test(NAME pdblock3_dib_empty_unit COMMAND pdbdibtest)
