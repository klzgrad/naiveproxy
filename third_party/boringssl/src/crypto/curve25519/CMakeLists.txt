include_directories(../../include)

if (${ARCH} STREQUAL "arm")
  set(
    CURVE25519_ARCH_SOURCES

    asm/x25519-asm-arm.S
  )
endif()

add_library(
  curve25519

  OBJECT

  spake25519.c

  ${CURVE25519_ARCH_SOURCES}
)
