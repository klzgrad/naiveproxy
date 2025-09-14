/*
 *  build with:
 *	nasm -f elf64 elf64so.asm
 *	ld -shared -o elf64so.so elf64so.o
 * test with:
 *	gcc -o elf64so elftest64.c ./elf64so.so
 *	./elf64so
 */

#include <stdio.h>
#include <inttypes.h>

extern long lrotate(long, int);
extern void greet_s(void);
extern void greet_m(void);
extern int8_t asmstr[];
extern void *selfptr;
extern void *textptr;
extern long integer;
long commvar;

int main(void)
{

    printf("Testing lrotate: should get 0x00400000, 0x00000001\n");
    printf("lrotate(0x00040000, 4) = 0x%08lx\n", lrotate(0x40000, 4));
    printf("lrotate(0x00040000, 46) = 0x%08lx\n", lrotate(0x40000, 46));

    printf("This string should read `hello, world': `%s'\n", asmstr);

    printf("&integer = %p, &commvar = %p\n", &integer, &commvar);
    printf("The integers here should be 1234, 1235 and 4321:\n");
    integer = 1234;
    commvar = 4321;
    greet_s();
    greet_m();

    printf("These pointers should be equal: %p and %p\n", &greet_s, textptr);

    printf("So should these: %p and %p\n", selfptr, &selfptr);

    return 0;
}
