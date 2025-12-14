#include <inttypes.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    /* Polynomial in bit-reversed notation */
    uint64_t poly;
    uint64_t crctab[256], v;
    int i, j;

    poly = strtoumax(argv[1], NULL, 0);

    printf("/* C */\n");
    printf("static const uint64_t crc64_tab[256] = {\n");
    for (i = 0; i < 256; i++) {
	v = i;
	for (j = 0; j < 8; j++)
	    v = (v >> 1) ^ ((v & 1) ? poly : 0);
	crctab[i] = v;
    }

    for (i = 0; i < 256; i += 2) {
	printf("    /* %02x */ UINT64_C(0x%016"PRIx64"), "
	       "UINT64_C(0x%016"PRIx64")%s\n",
	       i, crctab[i], crctab[i+1], (i == 254) ? "" : ",");
    }
    printf("};\n\n");

    printf("# perl\n");
    printf("@crc64_tab = (\n");
    for (i = 0; i < 256; i += 2) {
	printf("    [0x%08"PRIx32", 0x%08"PRIx32"], "
	       "[0x%08"PRIx32", 0x%08"PRIx32"]%-1s    # %02x\n",
	       (uint32_t)(crctab[i] >> 32),
	       (uint32_t)(crctab[i]),
	       (uint32_t)(crctab[i+1] >> 32),
	       (uint32_t)(crctab[i+1]),
	       (i == 254) ? "" : ",",
	       i);
    }
    printf(");\n");

    return 0;
}
