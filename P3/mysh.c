#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int want_exit = 0;

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);

    if (!q)
    {
        perror("realloc");
        exit(1);
    }

    return q;
}

static char *xstrdup(const char *s)
{
    char *r = strdup(s);
    if (!r)
    {
        perror("strdup");
        exit(1);
    }
    return r;
}

static void move_fd(int from, int to)
{
    if (from == to)
    {
        return;
    }

    if (dup2(from, to) < 0)
    {
        perror("dup2");
        _exit(1);
    }

    close(from);
}

struct Stage
{
    char **argv;
    int argc;
    int cap;
    char *in;
    char *out;
    int append;
};

struct Pipeline
{
    struct Stage *stages;
    int count;
    int cap;
};

static void stage_init(struct Stage *s)
{
    s->argv = NULL;
    s->argc = 0;
    s->cap = 0;
    s->in = NULL;
    s->out = NULL;
    s->append = 0;
}

static void stage_add_arg(struct Stage *s, const char *a)
{
    if (s->argc + 2 > s->cap)
    {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->argv = xrealloc(s->argv, s->cap * sizeof(char *));
    }

    s->argv[s->argc++] = xstrdup(a);
    s->argv[s->argc] = NULL;
}

static struct Stage *pipeline_add(struct Pipeline *p)
{
    if (p->count + 1 > p->cap)
    {
        p->cap = p->cap ? p->cap * 2 : 4;
        p->stages = xrealloc(p->stages, p->cap * sizeof(struct Stage));
    }

    struct Stage *s = &p->stages[p->count++];

    stage_init(s);

    return s;
}

static void pipeline_free(struct Pipeline *p)
{
    for (int i = 0; i < p->count; i++)
    {
        struct Stage *s = &p->stages[i];
        for (int j = 0; j < s->argc; j++)
            free(s->argv[j]);
        free(s->argv);
        free(s->in);
        free(s->out);
    }

    free(p->stages);

    p->stages = NULL;
    p->count = 0;
    p->cap = 0;
}

static int glob_match(const char *name, const char *pat)
{
    const char *star = strchr(pat, '*');
    if (!star)
    {
        return strcmp(name, pat) == 0;
    }

    size_t pre = (size_t)(star - pat);
    size_t suf = strlen(star + 1);
    size_t n = strlen(name);

    if (n < pre + suf)
        return 0;
    if (strncmp(name, pat, pre) != 0)
        return 0;
    if (strcmp(name + n - suf, star + 1) != 0)
        return 0;

    return 1;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void add_arg(struct Stage *s, const char *tok)
{
    if (!strchr(tok, '*'))
    {
        stage_add_arg(s, tok);
        return;
    }

    char dirbuf[4096];
    const char *dir = ".";
    const char *pat = tok;
    const char *slash = strrchr(tok, '/');
    if (slash)
    {
        size_t dl = (size_t)(slash - tok);
        if (dl == 0)
        {
            strcpy(dirbuf, "/");
        }
        else if (dl < sizeof dirbuf)
        {
            memcpy(dirbuf, tok, dl);
            dirbuf[dl] = '\0';
        }
        else
        {
            stage_add_arg(s, tok);
            return;
        }
        dir = dirbuf;
        pat = slash + 1;
    }

    DIR *d = opendir(dir);
    if (!d)
    {
        stage_add_arg(s, tok);
        return;
    }

    char **matches = NULL;
    int nm = 0, cm = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_name[0] == '.' && pat[0] != '.')
            continue;
        if (!glob_match(ent->d_name, pat))
            continue;
        char path[8192];
        if (slash)
        {
            if (strcmp(dir, "/") == 0)
                snprintf(path, sizeof path, "/%s", ent->d_name);
            else
                snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
        }
        else
        {
            snprintf(path, sizeof path, "%s", ent->d_name);
        }
        if (nm + 1 > cm)
        {
            cm = cm ? cm * 2 : 8;
            matches = xrealloc(matches, cm * sizeof(char *));
        }
        matches[nm++] = xstrdup(path);
    }
    closedir(d);

    if (nm == 0)
    {
        stage_add_arg(s, tok);
    }
    else
    {
        qsort(matches, nm, sizeof(char *), cmp_str);
        for (int i = 0; i < nm; i++)
        {
            stage_add_arg(s, matches[i]);
            free(matches[i]);
        }
    }
    free(matches);
}

static int parse_tokens(char **tok, int nt, struct Pipeline *p)
{
    struct Stage *cur = pipeline_add(p);
    for (int i = 0; i < nt; i++)
    {
        if (strcmp(tok[i], "|") == 0)
        {
            if (cur->argc == 0)
            {
                return -1;
            }

            cur = pipeline_add(p);
        }
        else if (strcmp(tok[i], "<") == 0 ||
                 strcmp(tok[i], ">") == 0 ||
                 strcmp(tok[i], ">>") == 0)
        {
            if (i + 1 >= nt)
            {
                return -1;
            }

            const char *target = tok[i + 1];

            if (strcmp(target, "|") == 0 || strcmp(target, "<") == 0 ||
                strcmp(target, ">") == 0 || strcmp(target, ">>") == 0)
                return -1;
            if (tok[i][0] == '<')
            {
                free(cur->in);
                cur->in = xstrdup(target);
            }
            else
            {
                free(cur->out);
                cur->out = xstrdup(target);
                cur->append = (tok[i][1] == '>');
            }
            i++;
        }
        else
        {
            add_arg(cur, tok[i]);
        }
    }
    if (cur->argc == 0)
        return -1;
    return 0;
}

static const char *search_dirs[] = {"/usr/local/bin", "/usr/bin", "/bin"};

static char *find_prog(const char *name)
{
    if (strchr(name, '/'))
    {
        return access(name, X_OK) == 0 ? xstrdup(name) : NULL;
    }
    for (size_t i = 0; i < sizeof search_dirs / sizeof *search_dirs; i++)
    {
        char p[4096];
        snprintf(p, sizeof p, "%s/%s", search_dirs[i], name);
        if (access(p, X_OK) == 0)
            return xstrdup(p);
    }
    return NULL;
}

static int is_builtin(const char *name)
{
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 ||
           strcmp(name, "which") == 0 || strcmp(name, "exit") == 0;
}

static int builtin_cd(struct Stage *s)
{
    if (s->argc > 2)
    {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }
    const char *target = s->argc == 1 ? getenv("HOME") : s->argv[1];
    if (!target)
    {
        fprintf(stderr, "cd: HOME not set\n");
        return 1;
    }
    if (chdir(target) != 0)
    {
        perror("cd");
        return 1;
    }
    return 0;
}

static int builtin_pwd(void)
{
    char buf[4096];
    if (!getcwd(buf, sizeof buf))
    {
        perror("pwd");
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

static int builtin_which(struct Stage *s)
{
    if (s->argc < 2)
        return 1;
    int rc = 0;
    for (int i = 1; i < s->argc; i++)
    {
        char *p = find_prog(s->argv[i]);
        if (p)
        {
            printf("%s\n", p);
            free(p);
        }
        else
            rc = 1;
    }
    return rc;
}

static int run_builtin(struct Stage *s)
{
    const char *name = s->argv[0];
    if (strcmp(name, "exit") == 0)
    {
        want_exit = 1;
        return 0;
    }
    if (strcmp(name, "cd") == 0)
        return builtin_cd(s);
    if (strcmp(name, "pwd") == 0)
        return builtin_pwd();
    if (strcmp(name, "which") == 0)
        return builtin_which(s);
    return 1;
}

static int apply_redirs(struct Stage *s)
{
    if (s->in)
    {
        int fd = open(s->in, O_RDONLY);
        if (fd < 0)
        {
            perror(s->in);
            return -1;
        }
        if (dup2(fd, 0) < 0)
        {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    if (s->out)
    {
        int flags = O_WRONLY | O_CREAT | (s->append ? O_APPEND : O_TRUNC);
        int fd = open(s->out, flags, 0640);
        if (fd < 0)
        {
            perror(s->out);
            return -1;
        }
        if (dup2(fd, 1) < 0)
        {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int run_builtin_here(struct Stage *s)
{
    if (!s->in && !s->out)
    {
        int rc = run_builtin(s);
        fflush(stdout);
        return rc;
    }

    int saved_in = dup(0);
    int saved_out = dup(1);
    if (saved_in < 0 || saved_out < 0)
    {
        perror("dup");

        if (saved_in >= 0)
            close(saved_in);
        if (saved_out >= 0)
            close(saved_out);

        return 1;
    }

    int rc;
    if (apply_redirs(s) < 0)
    {
        rc = 1;
    }
    else
    {
        rc = run_builtin(s);
        fflush(stdout);
    }

    if (dup2(saved_in, 0) < 0)
    {
        perror("dup2");
    }

    if (dup2(saved_out, 1) < 0)
    {
        perror("dup2");
    }

    close(saved_in);
    close(saved_out);

    return rc;
}

static int run_pipeline(struct Pipeline *p, int interactive)
{
    int n = p->count;
    if (n == 0)
    {
        return 0;
    }

    if (n == 1 && is_builtin(p->stages[0].argv[0]))
    {
        return run_builtin_here(&p->stages[0]);
    }

    int null_default = !interactive;
    if (interactive)
    {
        if (n > 1)
        {
            null_default = 1;
        }
        for (int i = 0; i < n; i++)
            if (p->stages[i].out)
            {
                null_default = 1;
                break;
            }
    }

    pid_t *pids = xrealloc(NULL, (size_t)n * sizeof(pid_t));
    int prev_read = -1;
    int spawned = 0;
    int failed = 0;

    for (int i = 0; i < n; i++)
    {
        int out_pipe[2] = {-1, -1};
        if (i < n - 1)
        {
            if (pipe(out_pipe) < 0)
            {
                perror("pipe");
                failed = 1;
                break;
            }
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            if (out_pipe[0] >= 0)
                close(out_pipe[0]);
            if (out_pipe[1] >= 0)
                close(out_pipe[1]);
            failed = 1;
            break;
        }

        if (pid == 0)
        {
            if (prev_read >= 0)
            {
                move_fd(prev_read, 0);
            }
            else if (p->stages[i].in)
            {
                int fd = open(p->stages[i].in, O_RDONLY);
                if (fd < 0)
                {
                    perror(p->stages[i].in);
                    exit(1);
                }
                move_fd(fd, 0);
            }
            else if (null_default)
            {
                int fd = open("/dev/null", O_RDONLY);
                if (fd >= 0)
                    move_fd(fd, 0);
            }

            if (i < n - 1)
            {
                move_fd(out_pipe[1], 1);
                close(out_pipe[0]);
            }
            else if (p->stages[i].out)
            {
                int flags = O_WRONLY | O_CREAT |
                            (p->stages[i].append ? O_APPEND : O_TRUNC);
                int fd = open(p->stages[i].out, flags, 0640);
                if (fd < 0)
                {
                    perror(p->stages[i].out);
                    exit(1);
                }
                move_fd(fd, 1);
            }

            if (is_builtin(p->stages[i].argv[0]))
            {
                exit(run_builtin(&p->stages[i]));
            }

            char *prog = find_prog(p->stages[i].argv[0]);
            if (!prog)
            {
                fprintf(stderr, "%s: command not found\n",
                        p->stages[i].argv[0]);
                exit(1);
            }

            execv(prog, p->stages[i].argv);
            perror("execv");
            exit(1);
        }

        pids[spawned++] = pid;
        if (prev_read >= 0)
        {
            close(prev_read);
        }

        if (i < n - 1)
        {
            close(out_pipe[1]);
            prev_read = out_pipe[0];
        }
        else
        {
            prev_read = -1;
        }
    }

    int last_status = 0;
    for (int i = 0; i < spawned; i++)
    {
        int st;
        waitpid(pids[i], &st, 0);
        if (i == spawned - 1)
        {
            last_status = st;
        }
    }

    if (prev_read >= 0)
    {
        close(prev_read);
    }

    free(pids);

    if (failed)
    {
        return 1;
    }

    if (interactive && spawned > 0)
    {
        if (WIFEXITED(last_status))
        {
            int code = WEXITSTATUS(last_status);
            if (code != 0)
                fprintf(stderr, "Exited with status %d\n", code);
        }
        else if (WIFSIGNALED(last_status))
        {
            fprintf(stderr, "Terminated by signal %s\n",
                    strsignal(WTERMSIG(last_status)));
        }
    }
    return 0;
}

static char *read_line(int fd)
{
    size_t cap = 128;
    char *buf = malloc(cap);
    if (!buf)
    {
        perror("malloc");
        exit(1);
    }
    size_t len = 0;

    for (;;)
    {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            free(buf);
            return NULL;
        }

        if (n == 0)
        {
            if (len == 0)
            {
                free(buf);
                return NULL;
            }
            break;
        }

        if (c == '\n')
        {
            break;
        }

        if (len + 1 >= cap)
        {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    return buf;
}

static void print_prompt(void)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd))
    {
        printf("$ ");
        fflush(stdout);
        return;
    }

    const char *home = getenv("HOME");
    size_t hlen = home ? strlen(home) : 0;

    if (home && strncmp(cwd, home, hlen) == 0 &&
        (cwd[hlen] == '\0' || cwd[hlen] == '/'))
    {
        printf("~%s$ ", cwd + hlen);
    }
    else
    {
        printf("%s$ ", cwd);
    }

    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        fprintf(stderr, "usage: %s [script]\n", argv[0]);

        return EXIT_FAILURE;
    }

    int input_fd = STDIN_FILENO;
    if (argc == 2)
    {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0)
        {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
    }

    int interactive = (argc == 1) && isatty(STDIN_FILENO);
    if (interactive)
    {
        printf("Welcome to my shell!\n");
    }

    while (!want_exit)
    {
        if (interactive)
            print_prompt();

        char *line = read_line(input_fd);
        if (!line)
        {
            break;
        }

        char *hash = strchr(line, '#');
        if (hash)
        {
            *hash = '\0';
        }

        char **tokens = NULL;
        int nt = 0, tcap = 0;
        char *save = NULL;
        for (char *t = strtok_r(line, " \t\n", &save); t;
             t = strtok_r(NULL, " \t\n", &save))
        {
            if (nt + 1 > tcap)
            {
                tcap = tcap ? tcap * 2 : 16;
                tokens = xrealloc(tokens, (size_t)tcap * sizeof(char *));
            }
            tokens[nt++] = t;
        }

        if (nt == 0)
        {
            free(tokens);
            free(line);
            continue;
        }

        struct Pipeline p = {NULL, 0, 0};
        if (parse_tokens(tokens, nt, &p) < 0)
        {
            fprintf(stderr, "mysh: syntax error\n");
            pipeline_free(&p);
            free(tokens);
            free(line);
            continue;
        }

        int has_exit = 0;
        for (int i = 0; i < p.count; i++)
        {
            if (strcmp(p.stages[i].argv[0], "exit") == 0)
            {
                has_exit = 1;
            }
        }

        run_pipeline(&p, interactive);

        if (has_exit)
        {
            want_exit = 1;
        }

        pipeline_free(&p);
        free(tokens);
        free(line);
    }

    if (interactive)
    {
        printf("mysh: exiting\n");
    }
    if (input_fd != STDIN_FILENO)
    {
        close(input_fd);
    }

    return EXIT_SUCCESS;
}
