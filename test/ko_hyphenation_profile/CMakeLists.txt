add_executable(ko_hyphenation_profile_tests
  KoHyphenationProfileTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(ko_hyphenation_profile_tests PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
)

target_compile_definitions(ko_hyphenation_profile_tests PRIVATE ENABLE_KOREAN_VERSION)

target_link_libraries(ko_hyphenation_profile_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ko_hyphenation_profile_tests)
