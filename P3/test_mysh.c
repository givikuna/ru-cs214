#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// this is written weird
// because I use NixOS
// and otherwise it'll just not work (I'm praying it'll work on school system)
// :(
// Nix is still superior tho
// I use Nix btw
static void find_bin(const char *name, char *out, size_t n)
{
    char cmd[128];
    out[0] = 0;
    snprintf(cmd, sizeof cmd, "which %s 2>/dev/null", name);
    FILE *f = popen(cmd, "r");
    if (f)
    {
        if (fgets(out, n, f))
            out[strcspn(out, "\n")] = 0;
        pclose(f);
    }
    if (!out[0])
        snprintf(out, n, "/bin/%s", name);
}

static int mk_script(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        return -1;
    }
    fputs(body, f);
    fclose(f);

    return chmod(path, 0755);
}

static int run_batch(const char *script, char *out, size_t outsz)
{
    FILE *f = fopen(".mysh_input", "w");
    if (!f)
    {
        perror("fopen");
        exit(1);
    }

    fputs(script, f);
    fclose(f);

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        exit(1);
    }

    if (pid == 0)
    {
        int fd = open(".mysh_out", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        execl("./mysh", "./mysh", ".mysh_input", (char *)NULL);
        _exit(127);
    }

    int st;
    waitpid(pid, &st, 0);

    out[0] = 0;

    int fd = open(".mysh_out", O_RDONLY);

    if (fd >= 0)
    {
        ssize_t n = read(fd, out, outsz - 1);
        if (n > 0)
            out[n] = 0;
        close(fd);
    }

    unlink(".mysh_input");
    unlink(".mysh_out");

    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int run_interactive(const char *in, char *out, size_t outsz)
{
    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (mfd < 0)
        return -1;
    if (grantpt(mfd) != 0 || unlockpt(mfd) != 0)
    {
        close(mfd);

        return -1;
    }

    char *sp = ptsname(mfd);
    if (!sp)
    {
        close(mfd);

        return -1;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        close(mfd);
        return -1;
    }
    if (pid == 0)
    {
        setsid();
        int sfd = open(sp, O_RDWR);
        if (sfd < 0)
            _exit(127);
        dup2(sfd, 0);
        dup2(sfd, 1);
        dup2(sfd, 2);
        if (sfd > 2)
            close(sfd);
        close(mfd);
        execl("./mysh", "./mysh", (char *)NULL);
        _exit(127);
    }

    size_t inlen = strlen(in), wrote = 0;
    while (wrote < inlen)
    {
        ssize_t n = write(mfd, in + wrote, inlen - wrote);

        if (n <= 0)
            break;

        wrote += n;
    }

    size_t got = 0;
    while (got < outsz - 1)
    {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(mfd, &r);

        struct timeval tv = {3, 0};

        if (select(mfd + 1, &r, 0, 0, &tv) <= 0)
            break;

        ssize_t n = read(mfd, out + got, outsz - 1 - got);

        if (n <= 0)
            break;

        got += n;
    }
    out[got] = 0;
    close(mfd);

    int st;
    waitpid(pid, &st, 0);

    int j = 0;
    for (int i = 0; out[i]; i++)
        if (out[i] != '\r')
            out[j++] = out[i];
    out[j] = 0;

    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(void)
{
    char out[8192], script[8192];
    char b_echo[256], b_cat[256], b_tr[256];
    char b_mkdir[256], b_rm[256], b_wc[256];

    find_bin("echo", b_echo, sizeof b_echo);
    find_bin("cat", b_cat, sizeof b_cat);
    find_bin("tr", b_tr, sizeof b_tr);
    find_bin("mkdir", b_mkdir, sizeof b_mkdir);
    find_bin("rm", b_rm, sizeof b_rm);
    find_bin("wc", b_wc, sizeof b_wc);

    printf("running tests\n");

    snprintf(script, sizeof script,
             "# just a comment\n"
             "   \n"
             "%s hi # trailing\n",
             b_echo);
    int rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strcmp(out, "hi\n") == 0);
    printf("ok: comments\n");

    snprintf(script, sizeof script,
             "cd /tmp\n"
             "pwd\n"
             "which %s\n"
             "cd /tmp /var\n"
             "%s still here\n",
             b_echo, b_echo);
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strstr(out, "/tmp\n"));
    assert(strstr(out, b_echo));
    assert(strstr(out, "too many"));
    assert(strstr(out, "still here\n"));
    printf("ok: builtins\n");

    unlink(".mysh_redir");
    snprintf(script, sizeof script,
             "%s one > .mysh_redir\n"
             "%s two >> .mysh_redir\n"
             "%s < .mysh_redir\n"
             "%s > .mysh_redir2 < .mysh_redir\n"
             "%s .mysh_redir2\n",
             b_echo, b_echo, b_cat, b_cat, b_cat);
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strcmp(out, "one\ntwo\none\ntwo\n") == 0);
    unlink(".mysh_redir");
    unlink(".mysh_redir2");
    printf("ok: redirection\n");

    snprintf(script, sizeof script,
             "%s -rf .mysh_glob\n"
             "%s .mysh_glob\n"
             "%s a > .mysh_glob/f1b.txt\n"
             "%s b > .mysh_glob/f2b.txt\n"
             "%s c > .mysh_glob/.hidden\n"
             "cd .mysh_glob\n"
             "%s f*b.txt\n"
             "%s *.txt\n"
             "%s nomatch*\n"
             "cd ..\n"
             "%s -rf .mysh_glob\n",
             b_rm, b_mkdir, b_echo, b_echo, b_echo,
             b_echo, b_echo, b_echo, b_rm);
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strcmp(out, "f1b.txt f2b.txt\nf1b.txt f2b.txt\nnomatch*\n") == 0);
    printf("ok: wildcards\n");

    unlink(".mysh_pipe");
    snprintf(script, sizeof script,
             "%s a b c | %s a-z A-Z | %s\n"
             "exit | %s done > .mysh_pipe\n"
             "%s NO\n",
             b_echo, b_tr, b_cat, b_echo, b_echo);
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strcmp(out, "A B C\n") == 0);
    FILE *pf = fopen(".mysh_pipe", "r");
    assert(pf);
    char line[64];
    assert(fgets(line, sizeof line, pf));
    fclose(pf);
    unlink(".mysh_pipe");
    assert(strcmp(line, "done\n") == 0);
    printf("ok: pipelines\n");

    snprintf(script, sizeof script,
             "%s < <\n"
             "%s hi > /no/such/dir/x\n"
             "nonexistent_xyz\n"
             "%s after\n",
             b_echo, b_echo, b_echo);
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strstr(out, "after\n"));
    printf("ok: error recovery\n");

    snprintf(script, sizeof script,
             "%s -l\n"
             "%s done\n",
             b_wc, b_echo);
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strncmp(out, "0\n", 2) == 0);
    assert(strstr(out, "done\n"));
    printf("ok: batch stdin\n");

    unlink(".mysh_pwd");
    snprintf(script, sizeof script, "pwd > .mysh_pwd\n");
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strcmp(out, "") == 0);
    FILE *pf2 = fopen(".mysh_pwd", "r");
    assert(pf2);
    char pwdline[4096];
    assert(fgets(pwdline, sizeof pwdline, pf2));
    fclose(pf2);
    unlink(".mysh_pwd");
    assert(pwdline[0] == '/');
    printf("ok: builtin redir\n");

    snprintf(script, sizeof script,
             "%s -rf .mysh_w\n"
             "%s .mysh_w\n"
             "%s a > .mysh_w/x1.txt\n"
             "%s b > .mysh_w/x2.txt\n"
             "%s c > .mysh_w/y.log\n"
             "%s .mysh_w/*.txt\n"
             "%s -rf .mysh_w\n",
             b_rm, b_mkdir, b_echo, b_echo, b_echo, b_echo, b_rm);
    rc = run_batch(script, out, sizeof out);
    assert(rc == 0);
    assert(strcmp(out, ".mysh_w/x1.txt .mysh_w/x2.txt\n") == 0);
    printf("ok: wildcard in path\n");

    {
        const char *h = "./.mysh_e42";
        assert(mk_script(h, "#!/bin/sh\nexit 42\n") == 0);
        char in[256];
        snprintf(in, sizeof in, "%s\nexit\n", h);
        int irc = run_interactive(in, out, sizeof out);
        unlink(h);
        if (irc < 0)
        {
            printf("skipped: interactive (no pty)\n");
        }
        else
        {
            assert(strstr(out, "Welcome"));
            assert(strstr(out, "status 42"));
            assert(strstr(out, "exiting"));
            printf("ok: interactive\n");
        }
    }

    {
        const char *h = "./.mysh_k9";
        assert(mk_script(h, "#!/bin/sh\nkill -9 $$\n") == 0);
        char in[256];
        snprintf(in, sizeof in, "%s\nexit\n", h);
        int irc = run_interactive(in, out, sizeof out);
        unlink(h);
        if (irc < 0)
        {
            printf("skipped: signal (no pty)\n");
        }
        else
        {
            assert(strstr(out, "Terminated by signal"));
            printf("ok: signal\n");
        }
    }

    printf("all tests passed\n");
    return 0;
}
