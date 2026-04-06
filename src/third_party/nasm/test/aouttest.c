/*
 * test source file for assembling to a.out
 * build with:
 *    nasm -f aout aouttest.asm
 *    gcc -o aouttest aouttest.c aouttest.o
 * (assuming your gcc is a.out)
 */

#include <stdio.h>
#include <inttypes.h>

extern int lrotate(int32_t, int);
extern void greet(void);
extern int8_t asmstr[];
extern void *selfptr;
extern void *textptr;
extern int integer, commvar;

int main(void)
{

    printf("Testing lrotate: should get 0x00400000, 0x00000001\n");
    printf("lrotate(0x00040000, 4) = 0x%08lx\n", lrotate(0x40000, 4));
    printf("lrotate(0x00040000, 14) = 0x%08lx\n", lrotate(0x40000, 14));

    printf("This string should read `hello, world': `%s'\n", asmstr);

    printf("The integers here should be 1234, 1235 and 4321:\n");
    integer = 1234;
    commvar = 4321;
    greet();

    printf("These pointers should be equal: %p and %p\n", &greet, textptr);

    printf("So should these: %p and %p\n", selfptr, &selfptr);
}
