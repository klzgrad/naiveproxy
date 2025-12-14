# ################################################################
# ZSTD Package Configuration
# ################################################################

include(CMakePackageConfigHelpers)

# Generate version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/zstdConfigVersion.cmake"
    VERSION ${zstd_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Export targets for build directory
export(EXPORT zstdExports
    FILE "${CMAKE_CURRENT_BINARY_DIR}/zstdTargets.cmake"
    NAMESPACE zstd::
)

# Configure package for installation
set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/zstd)

# Install exported targets
install(EXPORT zstdExports
    FILE zstdTargets.cmake
    NAMESPACE zstd::
    DESTINATION ${ConfigPackageLocation}
)

# Configure and install package config file
configure_package_config_file(
    zstdConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/zstdConfig.cmake"
    INSTALL_DESTINATION ${ConfigPackageLocation}
)

# Install config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/zstdConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/zstdConfigVersion.cmake"
    DESTINATION ${ConfigPackageLocation}
)
