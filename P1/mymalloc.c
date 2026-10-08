#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mymalloc.h"

#define MEMLENGTH 4096
#define ALIGNMENT 8

static union
{
    char bytes[MEMLENGTH];
    double not_used;
} heap;

typedef struct
{
    size_t info;
} chunk_header;

#define GET_SIZE(h) ((h)->info & ~7ULL)
#define IS_ALLOCATED(h) ((h)->info & 1ULL)
#define SET_INFO(h, s, a) ((h)->info = ((s) & ~7ULL) | ((a) & 1ULL))

static int initialized = 0;

static void detect_leaks()
{
    int leak_count = 0;
    size_t leaked_bytes = 0;
    size_t offset = 0;

    while (offset < MEMLENGTH)
    {
        chunk_header *header = (chunk_header *)(heap.bytes + offset);
        size_t size = GET_SIZE(header);

        if (size == 0)
        {
            break;
        }

        if (IS_ALLOCATED(header))
        {
            leak_count++;
            leaked_bytes += size - sizeof(chunk_header);
        }

        offset += size;
    }

    if (leak_count > 0)
    {
        fprintf(stderr, "mymalloc: %zu bytes leaked in %d objects.\n", leaked_bytes, leak_count);
    }
}

static void init_heap()
{
    if (!initialized)
    {
        chunk_header *initial = (chunk_header *)heap.bytes;

        SET_INFO(initial, MEMLENGTH, 0);

        initialized = 1;

        atexit(detect_leaks);
    }
}

void *mymalloc(size_t size, char *file, int line)
{
    init_heap();

    if (size == 0)
    {
        return NULL;
    }

    size_t payload_size = (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
    size_t required_size = payload_size + sizeof(chunk_header);

    size_t offset = 0;
    while (offset < MEMLENGTH)
    {
        chunk_header *header = (chunk_header *)(heap.bytes + offset);
        size_t chunk_size = GET_SIZE(header);

        if (chunk_size == 0)
        {
            break;
        }

        if (!IS_ALLOCATED(header) && chunk_size >= required_size)
        {
            if (chunk_size - required_size >= sizeof(chunk_header) + ALIGNMENT)
            {
                SET_INFO(header, required_size, 1);

                chunk_header *next = (chunk_header *)(heap.bytes + offset + required_size);
                SET_INFO(next, chunk_size - required_size, 0);
            }
            else
            {
                SET_INFO(header, chunk_size, 1);
            }

            return (void *)((char *)header + sizeof(chunk_header));
        }
        offset += chunk_size;
    }

    fprintf(stderr, "malloc: Unable to allocate %zu bytes (%s:%d)\n", size, file, line);

    return NULL;
}

void myfree(void *ptr, char *file, int line)
{
    init_heap();

    if (ptr == NULL)
    {
        fprintf(stderr, "free: Inappropriate pointer (%s:%d)\n", file, line);

        exit(2);
    }

    size_t offset = 0;
    int found = 0;

    while (offset < MEMLENGTH)
    {
        chunk_header *header = (chunk_header *)(heap.bytes + offset);
        size_t chunk_size = GET_SIZE(header);

        if (chunk_size == 0)
        {
            break;
        }

        void *payload = (void *)((char *)header + sizeof(chunk_header));

        if (payload == ptr)
        {
            if (!IS_ALLOCATED(header))
            {
                fprintf(stderr, "free: Inappropriate pointer (%s:%d)\n", file, line);
                exit(2);
            }

            SET_INFO(header, chunk_size, 0);
            found = 1;
            break;
        }

        offset += chunk_size;
    }

    if (!found)
    {
        fprintf(stderr, "free: Inappropriate pointer (%s:%d)\n", file, line);
        exit(2);
    }

    offset = 0;
    while (offset < MEMLENGTH)
    {
        chunk_header *header = (chunk_header *)(heap.bytes + offset);

        size_t chunk_size = GET_SIZE(header);
        if (chunk_size == 0)
        {
            break;
        }

        size_t next_offset = offset + chunk_size;

        if (!IS_ALLOCATED(header) && next_offset < MEMLENGTH)
        {
            chunk_header *next = (chunk_header *)(heap.bytes + next_offset);

            if (!IS_ALLOCATED(next))
            {
                SET_INFO(header, chunk_size + GET_SIZE(next), 0);
                continue;
            }
        }

        offset += GET_SIZE(header);
    }
}
