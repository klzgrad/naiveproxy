add_library(
  test_support

  OBJECT

  file_test.cc
  malloc.cc
  test_util.cc
  wycheproof_util.cc
)

add_library(
  boringssl_gtest_main

  OBJECT

  gtest_main.cc
)
