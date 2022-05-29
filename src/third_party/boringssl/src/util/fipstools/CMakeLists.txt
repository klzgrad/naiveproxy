include_directories(../../include)

if(FIPS)
  add_executable(
    test_fips

    test_fips.c
  )

  add_dependencies(test_fips global_target)
  target_link_libraries(test_fips crypto)
endif()
