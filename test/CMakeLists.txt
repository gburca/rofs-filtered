add_test(NAME filtered
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/verifyFiltered.bash -x $<TARGET_FILE:rofs-filtered> -s ${CMAKE_SOURCE_DIR})
add_test(NAME inverted
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/verifyInverted.bash -x $<TARGET_FILE:rofs-filtered> -s ${CMAKE_SOURCE_DIR})
