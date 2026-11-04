add_executable(featurestest main.cpp)
add_test(NAME protocol_features COMMAND featurestest ${CMAKE_CURRENT_SOURCE_DIR}/expected_replies.txt)
