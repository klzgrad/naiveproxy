# Copyright 2022 The BoringSSL Authors
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

include(${CMAKE_CURRENT_LIST_DIR}/OpenSSLTargets.cmake)

# Recursively collect dependency locations for the imported targets.
macro(_openssl_config_libraries libraries target)
  get_property(_DEPS TARGET ${target} PROPERTY INTERFACE_LINK_LIBRARIES)
  foreach(_DEP ${_DEPS})
    if(TARGET ${_DEP})
      _openssl_config_libraries(${libraries} ${_DEP})
    else()
      list(APPEND ${libraries} ${_DEP})
    endif()
  endforeach()
  get_property(_LOC TARGET ${target} PROPERTY LOCATION)
  list(APPEND ${libraries} ${_LOC})
endmacro()

set(OPENSSL_FOUND YES)
get_property(OPENSSL_INCLUDE_DIR TARGET OpenSSL::SSL PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
get_property(OPENSSL_CRYPTO_LIBRARY TARGET OpenSSL::Crypto PROPERTY LOCATION)
_openssl_config_libraries(OPENSSL_CRYPTO_LIBRARIES OpenSSL::Crypto)
list(REMOVE_DUPLICATES OPENSSL_CRYPTO_LIBRARIES)

get_property(OPENSSL_SSL_LIBRARY TARGET OpenSSL::Crypto PROPERTY LOCATION)
_openssl_config_libraries(OPENSSL_SSL_LIBRARIES OpenSSL::SSL)
list(REMOVE_DUPLICATES OPENSSL_SSL_LIBRARIES)

set(OPENSSL_LIBRARIES ${OPENSSL_CRYPTO_LIBRARIES} ${OPENSSL_SSL_LIBRARIES})
list(REMOVE_DUPLICATES OPENSSL_LIBRARIES)

set(_DEP)
set(_DEPS)
set(_LOC)
