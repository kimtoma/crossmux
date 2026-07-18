add_executable(standby_time_tests
  StandbyTimeTest.cpp
  ${REPO_ROOT}/src/activities/apps/standby/StandbyTime.cpp
  ${REPO_ROOT}/simulator/arduino-host/src/arduino_runtime.cpp
  ${REPO_ROOT}/simulator/hal/HalClock.cpp
)
target_include_directories(standby_time_tests PRIVATE
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/src/activities/apps/standby
)
target_include_directories(standby_time_tests SYSTEM PRIVATE
  ${REPO_ROOT}/simulator/arduino-host/include
  ${REPO_ROOT}/simulator/shims
)
target_compile_definitions(standby_time_tests PRIVATE CROSSPOINT_EMULATED=1)
target_link_libraries(standby_time_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(standby_time_tests)
