#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "mymalloc.h"

void task1()
{
    int sizes[] = {8, 16, 32, 64, 128, 512, 1024};
    void *ptrs[7];

    for (int i = 0; i < 7; i++)
    {
        ptrs[i] = malloc(sizes[i]);
    }

    for (int i = 6; i >= 0; i--)
    {
        free(ptrs[i]);
    }
}

void task2()
{
    void *ptrs[120];

    for (int i = 0; i < 120; i++)
    {
        ptrs[i] = malloc(1);
    }

    for (int i = 0; i < 120; i++)
    {
        free(ptrs[i]);
    }
}

void task3()
{
    void *ptrs[120];

    int allocated[120] = {0};
    int alloc_count = 0;
    int current_allocs = 0;

    while (alloc_count < 120)
    {
        int choice = rand() % 2;

        if (choice == 0 || current_allocs == 0)
        {
            for (int i = 0; i < 120; i++)
            {
                if (!allocated[i])
                {
                    ptrs[i] = malloc(1);

                    allocated[i] = 1;
                    alloc_count++;

                    current_allocs++;
                    break;
                }
            }
        }
        else
        {
            int target = rand() % current_allocs;
            int count = 0;

            for (int i = 0; i < 120; i++)
            {
                if (allocated[i])
                {
                    if (count == target)
                    {
                        free(ptrs[i]);

                        allocated[i] = 0;
                        current_allocs--;

                        break;
                    }
                    count++;
                }
            }
        }
    }

    for (int i = 0; i < 120; i++)
    {
        if (allocated[i])
        {
            free(ptrs[i]);
        }
    }
}

void task4()
{
    void *ptrs[120];

    for (int i = 0; i < 120; i++)
    {
        ptrs[i] = malloc(8);
    }

    for (int i = 1; i < 120; i += 2)
    {
        free(ptrs[i]);
    }

    for (int i = 0; i < 120; i += 2)
    {
        free(ptrs[i]);
    }
}

void task5()
{
    void *ptrs[60];

    for (int i = 0; i < 60; i++)
    {
        ptrs[i] = malloc(32);
    }

    for (int i = 59; i >= 30; i--)
    {
        free(ptrs[i]);
    }

    for (int i = 0; i < 30; i++)
    {
        free(ptrs[i]);
    }
}

long get_time_diff(struct timeval start, struct timeval end)
{
    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

int main()
{
    struct timeval start, end;

    gettimeofday(&start, NULL);

    for (int i = 0; i < 50; i++)
    {
        task1();
        task2();
        task3();
        task4();
        task5();
    }

    gettimeofday(&end, NULL);

    long total_time = get_time_diff(start, end);
    printf("Average Workload Time: %ld microseconds\n", total_time / 50);

    return 0;
}
