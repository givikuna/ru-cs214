#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

typedef struct Node
{
    char *word;
    int count;
    struct Node *next;
} Node;

static Node *head = NULL;

static void add_word(const char *w)
{
    for (Node *cur = head; cur; cur = cur->next)
    {
        if (strcmp(cur->word, w) == 0)
        {
            cur->count++;

            return;
        }
    }

    Node *n = malloc(sizeof(Node));

    n->word = strcpy(malloc(strlen(w) + 1), w);
    n->count = 1;
    n->next = head;

    head = n;
}

static void scan_file(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0)
    {
        perror(path);
        return;
    }

    char *word = malloc(64);
    char buf[4096];

    int cap = 64;
    int length = 0;
    int pend_hyph = 0;

    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        for (ssize_t i = 0; i < n; i++)
        {
            char c = buf[i];

            if (isalpha((unsigned char)c))
            {
                if (length + 3 >= cap)
                {
                    word = realloc(word, cap *= 2);
                }

                if (pend_hyph)
                {
                    word[length++] = '-';
                    pend_hyph = 0;
                }

                word[length++] = c;
            }
            else if (c == '\'')
            {
                if (pend_hyph)
                {
                    word[length] = '\0';

                    add_word(word);

                    length = 0;
                    pend_hyph = 0;
                }

                if (length + 2 >= cap)
                {
                    word = realloc(word, cap *= 2);
                }
                word[length++] = c;
            }
            else if (c == '-' && length > 0 && isalpha((unsigned char)word[length - 1]) && !pend_hyph)
            {
                pend_hyph = 1;
            }
            else
            {
                if (length > 0)
                {
                    word[length] = '\0';
                    add_word(word);
                    length = 0;
                }

                pend_hyph = 0;
            }
        }
    }
    if (length > 0)
    {
        word[length] = '\0';

        add_word(word);
    }

    free(word);
    close(fd);
}

static void scan_dir(const char *dir_path, const char *suffix)
{
    DIR *curr_dir = opendir(dir_path);

    if (!curr_dir)
    {
        perror(dir_path);

        return;
    }

    struct dirent *de;

    while ((de = readdir(curr_dir)) != NULL)
    {
        if (de->d_name[0] == '.')
        {
            continue;
        }

        char *path = malloc(strlen(dir_path) + strlen(de->d_name) + 2);

        strcpy(path, dir_path);
        strcat(path, "/");
        strcat(path, de->d_name);

        struct stat st;

        if (stat(path, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                scan_dir(path, suffix);
            }
            else if (S_ISREG(st.st_mode))
            {
                size_t nlen = strlen(de->d_name), slen = strlen(suffix);

                if (nlen >= slen && strcmp(de->d_name + nlen - slen, suffix) == 0)
                {
                    scan_file(path);
                }
            }
        }

        free(path);
    }

    closedir(curr_dir);
}

static int comes_before(Node *a, Node *b)
{
    if (a->count != b->count)
    {
        return a->count > b->count;
    }

    return strcmp(a->word, b->word) < 0;
}

int main(int argc, char *argv[])
{
    const char *suffix = ".txt";

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
        {
            suffix = argv[++i];
        }
        else if (strncmp(argv[i], "-s", 2) == 0)
        {
            suffix = argv[i] + 2;
        }
    }

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-s") == 0)
        {
            i++;

            continue;
        }
        if (strncmp(argv[i], "-s", 2) == 0)
        {
            continue;
        }

        struct stat st;

        if (stat(argv[i], &st) < 0)
        {
            perror(argv[i]);

            continue;
        }

        if (S_ISDIR(st.st_mode))
        {
            scan_dir(argv[i], suffix);
        }
        else
        {
            scan_file(argv[i]);
        }
    }

    Node *sorted = NULL;
    while (head)
    {
        Node *cur = head;

        head = head->next;

        if (!sorted || comes_before(cur, sorted))
        {
            cur->next = sorted;
            sorted = cur;
        }
        else
        {
            Node *p = sorted;

            while (p->next && !comes_before(cur, p->next))
            {
                p = p->next;
            }

            cur->next = p->next;
            p->next = cur;
        }
    }

    char out_buf[64];

    while (sorted)
    {
        Node *next = sorted->next;

        int n = sprintf(out_buf, " %d\n", sorted->count);

        // this is because the compiler is warning me
        // and i'm getting really annoyed
        ssize_t _1 = write(STDOUT_FILENO, sorted->word, strlen(sorted->word));
        ssize_t _2 = write(STDOUT_FILENO, out_buf, n);
        // apparently this is not enough
        // it is still warning me but i don't really care

        // makefile makes it stop warning me
        // huge win

        free(sorted->word);
        free(sorted);

        sorted = next;
    }

    return 0;
}
