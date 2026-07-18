add_executable(reading_sync_tests
  EpubCoverPolicyTest.cpp
  ReadingSyncClientPolicyTest.cpp
  ReadingSyncPolicyTest.cpp
  ReadingSyncUiPolicyTest.cpp
  ${REPO_ROOT}/src/reading_sync/ReadingSyncPolicy.cpp
  ${REPO_ROOT}/src/reading_sync/ReadingSyncResponseValidation.cpp
  ${REPO_ROOT}/src/reading_sync/ReadingSyncUiPolicy.cpp
)
target_include_directories(reading_sync_tests PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/simulator/arduino-host/include
  ${REPO_ROOT}/simulator/shims
)
target_link_libraries(reading_sync_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(reading_sync_tests)
