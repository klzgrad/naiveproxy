/*
 * Produce a "generalized CRC" table.  Assumes a platform with
 * /dev/urandom -- otherwise reimplement get_random_byte().
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t get_random_byte(void)
{
    static int fd = -1;
    uint8_t buf;
    int rv;

    if (fd < 0)
	fd = open("/dev/urandom", O_RDONLY);

    do {
	errno = 0;
	rv = read(fd, &buf, 1);
	if (rv < 1 && errno != EAGAIN)
	    abort();
    } while (rv < 1);

    return buf;
}

static void random_permute(uint8_t *buf)
{
    int i, j, k;
    int m;

    for (i = 0; i < 256; i++)
	buf[i] = i;

    m = 255;
    for (i = 255; i > 0; i--) {
	if (i <= (m >> 1))
	    m >>= 1;
	do {
	    j = get_random_byte() & m;
	} while (j > i);
	k = buf[i];
	buf[i] = buf[j];
	buf[j] = k;
    }
}

static void xcrc_table(uint64_t *buf)
{
    uint8_t perm[256];
    int i, j;

    memset(buf, 0, 8*256);	/* Make static checkers happy */

    for (i = 0; i < 8; i++) {
	random_permute(perm);
	for (j = 0; j < 256; j++)
	    buf[j] = (buf[j] << 8) | perm[j];
    }
}

int main(void)
{
    int i;
    uint64_t buf[256];

    xcrc_table(buf);

    for (i = 0; i < 256; i++) {
	printf("%016"PRIx64"\n", buf[i]);
    }

    return 0;
}
