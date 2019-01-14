/* Copyright (c) 2016, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <openssl/cpu.h>

#include "cpu-arm-linux.h"
#include "internal.h"

#if defined(OPENSSL_ARM) && !defined(OPENSSL_STATIC_ARMCAP)
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/arm_arch.h>
#include <openssl/buf.h>
#include <openssl/mem.h>
#endif


// The following functions are only used in ARM, but they are defined on all
// platforms for testing and fuzzing purposes.

static int STRING_PIECE_equals(const STRING_PIECE *a, const char *b) {
  size_t b_len = strlen(b);
  return a->len == b_len && OPENSSL_memcmp(a->data, b, b_len) == 0;
}

// STRING_PIECE_split finds the first occurence of |sep| in |in| and, if found,
// sets |*out_left| and |*out_right| to |in| split before and after it. It
// returns one if |sep| was found and zero otherwise.
static int STRING_PIECE_split(STRING_PIECE *out_left, STRING_PIECE *out_right,
                              const STRING_PIECE *in, char sep) {
  const char *p = OPENSSL_memchr(in->data, sep, in->len);
  if (p == NULL) {
    return 0;
  }
  // |out_left| or |out_right| may alias |in|, so make a copy.
  STRING_PIECE in_copy = *in;
  out_left->data = in_copy.data;
  out_left->len = p - in_copy.data;
  out_right->data = in_copy.data + out_left->len + 1;
  out_right->len = in_copy.len - out_left->len - 1;
  return 1;
}

// STRING_PIECE_get_delimited reads a |sep|-delimited entry from |s|, writing it
// to |out| and updating |s| to point beyond it. It returns one on success and
// zero if |s| is empty. If |s| is has no copies of |sep| and is non-empty, it
// reads the entire string to |out|.
static int STRING_PIECE_get_delimited(STRING_PIECE *s, STRING_PIECE *out, char sep) {
  if (s->len == 0) {
    return 0;
  }
  if (!STRING_PIECE_split(out, s, s, sep)) {
    // |s| had no instances of |sep|. Return the entire string.
    *out = *s;
    s->data += s->len;
    s->len = 0;
  }
  return 1;
}

// STRING_PIECE_trim removes leading and trailing whitespace from |s|.
static void STRING_PIECE_trim(STRING_PIECE *s) {
  while (s->len != 0 && (s->data[0] == ' ' || s->data[0] == '\t')) {
    s->data++;
    s->len--;
  }
  while (s->len != 0 &&
         (s->data[s->len - 1] == ' ' || s->data[s->len - 1] == '\t')) {
    s->len--;
  }
}

// extract_cpuinfo_field extracts a /proc/cpuinfo field named |field| from
// |in|. If found, it sets |*out| to the value and returns one. Otherwise, it
// returns zero.
static int extract_cpuinfo_field(STRING_PIECE *out, const STRING_PIECE *in,
                                 const char *field) {
  // Process |in| one line at a time.
  STRING_PIECE remaining = *in, line;
  while (STRING_PIECE_get_delimited(&remaining, &line, '\n')) {
    STRING_PIECE key, value;
    if (!STRING_PIECE_split(&key, &value, &line, ':')) {
      continue;
    }
    STRING_PIECE_trim(&key);
    if (STRING_PIECE_equals(&key, field)) {
      STRING_PIECE_trim(&value);
      *out = value;
      return 1;
    }
  }

  return 0;
}

static int cpuinfo_field_equals(const STRING_PIECE *cpuinfo, const char *field,
                                const char *value) {
  STRING_PIECE extracted;
  return extract_cpuinfo_field(&extracted, cpuinfo, field) &&
         STRING_PIECE_equals(&extracted, value);
}

// has_list_item treats |list| as a space-separated list of items and returns
// one if |item| is contained in |list| and zero otherwise.
static int has_list_item(const STRING_PIECE *list, const char *item) {
  STRING_PIECE remaining = *list, feature;
  while (STRING_PIECE_get_delimited(&remaining, &feature, ' ')) {
    if (STRING_PIECE_equals(&feature, item)) {
      return 1;
    }
  }
  return 0;
}

unsigned long crypto_get_arm_hwcap_from_cpuinfo(const STRING_PIECE *cpuinfo) {
  if (cpuinfo_field_equals(cpuinfo, "CPU architecture", "8")) {
    // This is a 32-bit ARM binary running on a 64-bit kernel. NEON is always
    // available on ARMv8. Linux omits required features, so reading the
    // "Features" line does not work. (For simplicity, use strict equality. We
    // assume everything running on future ARM architectures will have a
    // working |getauxval|.)
    return HWCAP_NEON;
  }

  STRING_PIECE features;
  if (extract_cpuinfo_field(&features, cpuinfo, "Features") &&
      has_list_item(&features, "neon")) {
    return HWCAP_NEON;
  }
  return 0;
}

unsigned long crypto_get_arm_hwcap2_from_cpuinfo(const STRING_PIECE *cpuinfo) {
  STRING_PIECE features;
  if (!extract_cpuinfo_field(&features, cpuinfo, "Features")) {
    return 0;
  }

  unsigned long ret = 0;
  if (has_list_item(&features, "aes")) {
    ret |= HWCAP2_AES;
  }
  if (has_list_item(&features, "pmull")) {
    ret |= HWCAP2_PMULL;
  }
  if (has_list_item(&features, "sha1")) {
    ret |= HWCAP2_SHA1;
  }
  if (has_list_item(&features, "sha2")) {
    ret |= HWCAP2_SHA2;
  }
  return ret;
}

int crypto_cpuinfo_has_broken_neon(const STRING_PIECE *cpuinfo) {
  return cpuinfo_field_equals(cpuinfo, "CPU implementer", "0x51") &&
         cpuinfo_field_equals(cpuinfo, "CPU architecture", "7") &&
         cpuinfo_field_equals(cpuinfo, "CPU variant", "0x1") &&
         cpuinfo_field_equals(cpuinfo, "CPU part", "0x04d") &&
         cpuinfo_field_equals(cpuinfo, "CPU revision", "0");
}

#if defined(OPENSSL_ARM) && !defined(OPENSSL_STATIC_ARMCAP)

#define AT_HWCAP 16
#define AT_HWCAP2 26

// |getauxval| is not available on Android until API level 20. Link it as a weak
// symbol and use other methods as fallback.
unsigned long getauxval(unsigned long type) __attribute__((weak));

static int open_eintr(const char *path, int flags) {
  int ret;
  do {
    ret = open(path, flags);
  } while (ret < 0 && errno == EINTR);
  return ret;
}

static ssize_t read_eintr(int fd, void *out, size_t len) {
  ssize_t ret;
  do {
    ret = read(fd, out, len);
  } while (ret < 0 && errno == EINTR);
  return ret;
}

// read_full reads exactly |len| bytes from |fd| to |out|. On error or end of
// file, it returns zero.
static int read_full(int fd, void *out, size_t len) {
  char *outp = out;
  while (len > 0) {
    ssize_t ret = read_eintr(fd, outp, len);
    if (ret <= 0) {
      return 0;
    }
    outp += ret;
    len -= ret;
  }
  return 1;
}

// read_file opens |path| and reads until end-of-file. On success, it returns
// one and sets |*out_ptr| and |*out_len| to a newly-allocated buffer with the
// contents. Otherwise, it returns zero.
static int read_file(char **out_ptr, size_t *out_len, const char *path) {
  int fd = open_eintr(path, O_RDONLY);
  if (fd < 0) {
    return 0;
  }

  static const size_t kReadSize = 1024;
  int ret = 0;
  size_t cap = kReadSize, len = 0;
  char *buf = OPENSSL_malloc(cap);
  if (buf == NULL) {
    goto err;
  }

  for (;;) {
    if (cap - len < kReadSize) {
      size_t new_cap = cap * 2;
      if (new_cap < cap) {
        goto err;
      }
      char *new_buf = OPENSSL_realloc(buf, new_cap);
      if (new_buf == NULL) {
        goto err;
      }
      buf = new_buf;
      cap = new_cap;
    }

    ssize_t bytes_read = read_eintr(fd, buf + len, kReadSize);
    if (bytes_read < 0) {
      goto err;
    }
    if (bytes_read == 0) {
      break;
    }
    len += bytes_read;
  }

  *out_ptr = buf;
  *out_len = len;
  ret = 1;
  buf = NULL;

err:
  OPENSSL_free(buf);
  close(fd);
  return ret;
}

// getauxval_proc behaves like |getauxval| but reads from /proc/self/auxv.
static unsigned long getauxval_proc(unsigned long type) {
  int fd = open_eintr("/proc/self/auxv", O_RDONLY);
  if (fd < 0) {
    return 0;
  }

  struct {
    unsigned long tag;
    unsigned long value;
  } entry;

  for (;;) {
    if (!read_full(fd, &entry, sizeof(entry)) ||
        (entry.tag == 0 && entry.value == 0)) {
      break;
    }
    if (entry.tag == type) {
      close(fd);
      return entry.value;
    }
  }
  close(fd);
  return 0;
}

extern uint32_t OPENSSL_armcap_P;

static int g_has_broken_neon, g_needs_hwcap2_workaround;

void OPENSSL_cpuid_setup(void) {
  char *cpuinfo_data;
  size_t cpuinfo_len;
  if (!read_file(&cpuinfo_data, &cpuinfo_len, "/proc/cpuinfo")) {
    return;
  }
  STRING_PIECE cpuinfo;
  cpuinfo.data = cpuinfo_data;
  cpuinfo.len = cpuinfo_len;

  // |getauxval| is not available on Android until API level 20. If it is
  // unavailable, read from /proc/self/auxv as a fallback. This is unreadable
  // on some versions of Android, so further fall back to /proc/cpuinfo.
  //
  // See
  // https://android.googlesource.com/platform/ndk/+/882ac8f3392858991a0e1af33b4b7387ec856bd2
  // and b/13679666 (Google-internal) for details.
  unsigned long hwcap = 0;
  if (getauxval != NULL) {
    hwcap = getauxval(AT_HWCAP);
  }
  if (hwcap == 0) {
    hwcap = getauxval_proc(AT_HWCAP);
  }
  if (hwcap == 0) {
    hwcap = crypto_get_arm_hwcap_from_cpuinfo(&cpuinfo);
  }

  // Clear NEON support if known broken.
  g_has_broken_neon = crypto_cpuinfo_has_broken_neon(&cpuinfo);
  if (g_has_broken_neon) {
    hwcap &= ~HWCAP_NEON;
  }

  // Matching OpenSSL, only report other features if NEON is present.
  if (hwcap & HWCAP_NEON) {
    OPENSSL_armcap_P |= ARMV7_NEON;

    // Some ARMv8 Android devices don't expose AT_HWCAP2. Fall back to
    // /proc/cpuinfo. See https://crbug.com/596156.
    unsigned long hwcap2 = 0;
    if (getauxval != NULL) {
      hwcap2 = getauxval(AT_HWCAP2);
    }
    if (hwcap2 == 0) {
      hwcap2 = crypto_get_arm_hwcap2_from_cpuinfo(&cpuinfo);
      g_needs_hwcap2_workaround = hwcap2 != 0;
    }

    if (hwcap2 & HWCAP2_AES) {
      OPENSSL_armcap_P |= ARMV8_AES;
    }
    if (hwcap2 & HWCAP2_PMULL) {
      OPENSSL_armcap_P |= ARMV8_PMULL;
    }
    if (hwcap2 & HWCAP2_SHA1) {
      OPENSSL_armcap_P |= ARMV8_SHA1;
    }
    if (hwcap2 & HWCAP2_SHA2) {
      OPENSSL_armcap_P |= ARMV8_SHA256;
    }
  }

  OPENSSL_free(cpuinfo_data);
}

int CRYPTO_has_broken_NEON(void) { return g_has_broken_neon; }

int CRYPTO_needs_hwcap2_workaround(void) { return g_needs_hwcap2_workaround; }

#endif  // OPENSSL_ARM && !OPENSSL_STATIC_ARMCAP
