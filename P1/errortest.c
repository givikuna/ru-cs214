#include <stdio.h>
#include <stdlib.h>
#include "mymalloc.h"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("usage: %s <test_number>\n", argv[0]);
        printf("1: free address not obtained from malloc\n");
        printf("2: free address not at start of chunk\n");
        printf("3: double free\n");
        return EXIT_FAILURE;
    }

    int test = atoi(argv[1]);

    if (test == 1)
    {
        int x;

        free(&x);
    }
    else if (test == 2)
    {
        int *p = malloc(sizeof(int) * 2);

        free(p + 1);
    }
    else if (test == 3)
    {
        int *p = malloc(sizeof(int) * 10);

        free(p);
        free(p);
    }
    else
    {
        printf("incorrect test number\n");
    }

    return EXIT_SUCCESS;
}
