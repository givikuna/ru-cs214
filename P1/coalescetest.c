#include <stdio.h>
#include <stdlib.h>
#include "mymalloc.h"

int main()
{
    void *a = malloc(32);
    void *b = malloc(32);
    void *c = malloc(32);

    free(a);
    free(b);
    free(c);

    void *d = malloc(80);
    if (d != NULL)
    {
        printf("Coalescing successful.\n");
        free(d);
    }
    else
    {
        printf("Coalescing failed.\n");
    }

    return EXIT_SUCCESS;
}
