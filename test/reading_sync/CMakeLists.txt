add_executable(reading_sync_tests
  ReadingSyncPolicyTest.cpp
  ${REPO_ROOT}/src/reading_sync/ReadingSyncPolicy.cpp
)
target_include_directories(reading_sync_tests PRIVATE ${REPO_ROOT}/src)
target_link_libraries(reading_sync_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(reading_sync_tests)
