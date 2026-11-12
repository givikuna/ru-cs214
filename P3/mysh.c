#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

// to know when to quit
int time_to_exit = 0;

typedef struct
{
    char **array;
    int size;
    int capacity;
} ArrayList;

void init_list(ArrayList *list)
{
    list->capacity = 10;
    list->size = 0;
    list->array = malloc(list->capacity * sizeof(char *));
    list->array[0] = NULL;
}

void add_list(ArrayList *list, char *item)
{
    if (list->size >= list->capacity - 1)
    {
        list->capacity *= 2;
        list->array = realloc(list->array, list->capacity * sizeof(char *));
    }

    list->array[list->size] = strdup(item);
    list->size++;
    list->array[list->size] = NULL;
}

void free_list(ArrayList *list)
{
    for (int i = 0; i < list->size; i++)
    {
        free(list->array[i]);
    }
    free(list->array);
    list->size = 0;
}

typedef struct
{
    ArrayList args;
    char *in_file;
    char *out_file;
    int append_out;
} Command;

void init_command(Command *cmd)
{
    init_list(&cmd->args);
    cmd->in_file = NULL;
    cmd->out_file = NULL;
    cmd->append_out = 0;
}

void free_command(Command *cmd)
{
    free_list(&cmd->args);
    if (cmd->in_file)
        free(cmd->in_file);
    if (cmd->out_file)
        free(cmd->out_file);
}

int matches_wildcard(char *filename, char *pattern)
{
    char *star = strchr(pattern, '*');
    if (star == NULL)
    {
        return strcmp(filename, pattern) == 0;
    }

    int prefix_len = star - pattern;
    char *suffix = star + 1;
    int suffix_len = strlen(suffix);
    int name_len = strlen(filename);

    if (name_len < prefix_len + suffix_len)
    {
        return 0;
    }
    if (strncmp(filename, pattern, prefix_len) != 0)
    {
        return 0;
    }
    if (strcmp(filename + name_len - suffix_len, suffix) != 0)
    {
        return 0;
    }

    return 1;
}

void expand_and_add(ArrayList *args, char *token)
{
    if (strchr(token, '*') == NULL)
    {
        add_list(args, token);
        return;
    }

    char dir_path[1024] = ".";
    char pattern[1024];

    char *last_slash = strrchr(token, '/');
    if (last_slash != NULL)
    {
        int dir_len = last_slash - token;
        if (dir_len == 0)
        {
            strcpy(dir_path, "/");
        }
        else
        {
            strncpy(dir_path, token, dir_len);
            dir_path[dir_len] = '\0';
        }
        strcpy(pattern, last_slash + 1);
    }
    else
    {
        strcpy(pattern, token);
    }

    DIR *d = opendir(dir_path);
    if (d == NULL)
    {
        add_list(args, token);
        return;
    }

    ArrayList matches;
    init_list(&matches);

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL)
    {
        char *name = dir->d_name;
        if (name[0] == '.' && pattern[0] != '.')
        {
            continue;
        }
        if (matches_wildcard(name, pattern))
        {
            if (last_slash != NULL)
            {
                char full_path[1024];

                if (strcmp(dir_path, "/") == 0)
                {
                    snprintf(full_path, sizeof(full_path), "/%s", name);
                }
                else
                {
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);
                }
                add_list(&matches, full_path);
            }
            else
            {
                add_list(&matches, name);
            }
        }
    }
    closedir(d);

    if (matches.size == 0)
    {
        add_list(args, token);
    }
    else
    {
        for (int i = 0; i < matches.size - 1; i++)
        {
            for (int j = i + 1; j < matches.size; j++)
            {
                if (strcmp(matches.array[i], matches.array[j]) > 0)
                {
                    char *temp = matches.array[i];
                    matches.array[i] = matches.array[j];
                    matches.array[j] = temp;
                }
            }
        }
        for (int i = 0; i < matches.size; i++)
        {
            add_list(args, matches.array[i]);
        }
    }
    free_list(&matches);
}

char *find_executable(char *cmd_name)
{
    if (strchr(cmd_name, '/') != NULL)
    {
        if (access(cmd_name, X_OK) == 0)
        {
            return strdup(cmd_name);
        }
        return NULL;
    }

    char *directories[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    char path_buffer[1024];

    for (int i = 0; i < 3; i++)
    {
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", directories[i], cmd_name);

        if (access(path_buffer, X_OK) == 0)
        {
            return strdup(path_buffer);
        }
    }
    return NULL;
}

int is_builtin(char *cmd_name)
{
    return (strcmp(cmd_name, "cd") == 0 || strcmp(cmd_name, "pwd") == 0 ||
            strcmp(cmd_name, "which") == 0 || strcmp(cmd_name, "exit") == 0);
}

int execute_builtin(Command *cmd)
{
    char *cmd_name = cmd->args.array[0];

    if (strcmp(cmd_name, "exit") == 0)
    {
        time_to_exit = 1;

        return 0;
    }

    if (strcmp(cmd_name, "cd") == 0)
    {
        if (cmd->args.size > 2)
        {
            fprintf(stderr, "cd: too many arguments\n");
            return 1;
        }

        char *target = (cmd->args.size == 1) ? getenv("HOME") : cmd->args.array[1];

        if (chdir(target) != 0)
        {
            perror("cd");

            return 1;
        }

        return 0;
    }

    if (strcmp(cmd_name, "pwd") == 0)
    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            printf("%s\n", cwd);

            return 0;
        }
        else
        {
            perror("pwd");

            return 1;
        }
    }

    if (strcmp(cmd_name, "which") == 0)
    {
        if (cmd->args.size < 2)
            return 1;
        int result = 0;
        for (int i = 1; i < cmd->args.size; i++)
        {
            char *path = find_executable(cmd->args.array[i]);
            if (path)
            {
                printf("%s\n", path);

                free(path);
            }
            else
            {
                result = 1;
            }
        }
        return result;
    }

    return 1;
}

char *read_line_unbuffered(int fd)
{
    int capacity = 64;
    char *buffer = malloc(capacity);
    int length = 0;
    char c;

    while (1)
    {
        int bytes_read = read(fd, &c, 1);

        if (bytes_read < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            free(buffer);
            return NULL;
        }

        if (bytes_read == 0)
        {
            if (length == 0)
            {
                free(buffer);
                return NULL;
            }
            break;
        }

        if (c == '\n')
            break;

        buffer[length++] = c;

        if (length >= capacity)
        {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }
    }

    buffer[length] = '\0';
    return buffer;
}

int main(int argc, char **argv)
{
    int input_fd = STDIN_FILENO;

    if (argc > 2)
    {
        fprintf(stderr, "usage: %s [script]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2)
    {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0)
        {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
    }

    int interactive = isatty(input_fd);

    if (interactive)
    {
        printf("Welcome to my shell!\n");
    }

    char *home_dir = getenv("HOME");

    while (!time_to_exit)
    {
        if (interactive)
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
            {
                if (home_dir && strncmp(cwd, home_dir, strlen(home_dir)) == 0)
                {
                    printf("~%s$ ", cwd + strlen(home_dir));
                }
                else
                {
                    printf("%s$ ", cwd);
                }
            }
            else
            {
                printf("$ ");
            }
            fflush(stdout);
        }

        char *line = read_line_unbuffered(input_fd);
        if (line == NULL)
        {
            break;
        }

        char *hash = strchr(line, '#');
        if (hash)
        {
            *hash = '\0';
        }

        ArrayList tokens;
        init_list(&tokens);
        char *token = strtok(line, " \t\n");
        while (token != NULL)
        {
            add_list(&tokens, token);
            token = strtok(NULL, " \t\n");
        }

        if (tokens.size == 0)
        {
            free_list(&tokens);
            free(line);
            continue;
        }

        int num_cmds = 1;
        for (int i = 0; i < tokens.size; i++)
        {
            if (strcmp(tokens.array[i], "|") == 0)
            {
                num_cmds++;
            }
        }

        Command *cmds = malloc(num_cmds * sizeof(Command));
        for (int i = 0; i < num_cmds; i++)
        {
            init_command(&cmds[i]);
        }

        int curr_cmd = 0;
        int syntax_error = 0;

        for (int i = 0; i < tokens.size; i++)
        {
            char *tok = tokens.array[i];

            if (strcmp(tok, "|") == 0)
            {
                if (cmds[curr_cmd].args.size == 0)
                    syntax_error = 1;
                curr_cmd++;
            }
            else if (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0)
            {
                if (i + 1 >= tokens.size || strcmp(tokens.array[i + 1], "|") == 0 || strcmp(tokens.array[i + 1], "<") == 0)
                {
                    syntax_error = 1;
                    break;
                }
                char *target = tokens.array[i + 1];
                if (strcmp(tok, "<") == 0)
                {
                    if (cmds[curr_cmd].in_file)
                        free(cmds[curr_cmd].in_file);
                    cmds[curr_cmd].in_file = strdup(target);
                }
                else
                {
                    if (cmds[curr_cmd].out_file)
                        free(cmds[curr_cmd].out_file);
                    cmds[curr_cmd].out_file = strdup(target);
                    cmds[curr_cmd].append_out = (strcmp(tok, ">>") == 0) ? 1 : 0;
                }
                i++;
            }
            else
            {
                expand_and_add(&cmds[curr_cmd].args, tok);
            }
        }

        if (cmds[num_cmds - 1].args.size == 0)
        {
            syntax_error = 1;
        }

        if (syntax_error)
        {
            fprintf(stderr, "mysh: syntax error\n");
            for (int i = 0; i < num_cmds; i++)
                free_command(&cmds[i]);
            free(cmds);
            free_list(&tokens);
            free(line);
            continue;
        }

        for (int i = 0; i < num_cmds; i++)
        {
            if (strcmp(cmds[i].args.array[0], "exit") == 0)
            {
                time_to_exit = 1;
            }
        }

        if (num_cmds == 1 && is_builtin(cmds[0].args.array[0]))
        {
            int saved_stdin = dup(STDIN_FILENO);
            int saved_stdout = dup(STDOUT_FILENO);

            if (cmds[0].in_file)
            {
                int fd = open(cmds[0].in_file, O_RDONLY);
                if (fd >= 0)
                {
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            }
            if (cmds[0].out_file)
            {
                int flags = O_WRONLY | O_CREAT | (cmds[0].append_out ? O_APPEND : O_TRUNC);
                int fd = open(cmds[0].out_file, flags, 0640);
                if (fd >= 0)
                {
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            }

            execute_builtin(&cmds[0]);
            fflush(stdout);

            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdin);
            close(saved_stdout);
        }
        else
        {
            int prev_pipe_fd = -1;
            pid_t *pids = malloc(num_cmds * sizeof(pid_t));

            for (int i = 0; i < num_cmds; i++)
            {
                int pfd[2];
                if (i < num_cmds - 1)
                {
                    if (pipe(pfd) < 0)
                    {
                        perror("pipe failed");
                        exit(1);
                    }
                }

                pids[i] = fork();

                if (pids[i] == 0)
                {
                    if (prev_pipe_fd != -1)
                    {
                        dup2(prev_pipe_fd, STDIN_FILENO);
                        close(prev_pipe_fd);
                    }
                    else if (!interactive && cmds[i].in_file == NULL)
                    {
                        int null_fd = open("/dev/null", O_RDONLY);
                        if (null_fd >= 0)
                        {
                            dup2(null_fd, STDIN_FILENO);
                            close(null_fd);
                        }
                    }

                    if (i < num_cmds - 1)
                    {
                        dup2(pfd[1], STDOUT_FILENO);
                        close(pfd[1]);
                        close(pfd[0]);
                    }

                    if (cmds[i].in_file)
                    {
                        int fd = open(cmds[i].in_file, O_RDONLY);
                        if (fd < 0)
                        {
                            perror(cmds[i].in_file);
                            exit(1);
                        }
                        dup2(fd, STDIN_FILENO);
                        close(fd);
                    }

                    if (cmds[i].out_file)
                    {
                        int flags = O_WRONLY | O_CREAT | (cmds[i].append_out ? O_APPEND : O_TRUNC);
                        int fd = open(cmds[i].out_file, flags, 0640);
                        if (fd < 0)
                        {
                            perror(cmds[i].out_file);
                            exit(1);
                        }
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }

                    if (is_builtin(cmds[i].args.array[0]))
                    {
                        int rc = execute_builtin(&cmds[i]);
                        exit(rc);
                    }
                    else
                    {
                        char *exec_path = find_executable(cmds[i].args.array[0]);
                        if (exec_path == NULL)
                        {
                            fprintf(stderr, "%s: command not found\n", cmds[i].args.array[0]);
                            exit(1);
                        }
                        execv(exec_path, cmds[i].args.array);
                        perror("execv failed");
                        exit(1);
                    }
                }
                else
                {
                    if (prev_pipe_fd != -1)
                    {
                        close(prev_pipe_fd);
                    }
                    if (i < num_cmds - 1)
                    {
                        close(pfd[1]);
                        prev_pipe_fd = pfd[0];
                    }
                }
            }

            int last_status = 0;
            for (int i = 0; i < num_cmds; i++)
            {
                int status;
                waitpid(pids[i], &status, 0);
                if (i == num_cmds - 1)
                {
                    last_status = status;
                }
            }

            if (interactive)
            {
                if (WIFEXITED(last_status))
                {
                    int code = WEXITSTATUS(last_status);
                    if (code != 0)
                    {
                        fprintf(stderr, "Exited with status %d\n", code);
                    }
                }
                else if (WIFSIGNALED(last_status))
                {
                    fprintf(stderr, "Terminated by signal %s\n", strsignal(WTERMSIG(last_status)));
                }
            }
            free(pids);
        }

        for (int i = 0; i < num_cmds; i++)
            free_command(&cmds[i]);
        free(cmds);
        free_list(&tokens);
        free(line);
    }

    if (interactive)
    {
        printf("mysh: exiting\n");
    }

    if (input_fd != STDIN_FILENO)
        close(input_fd);

    return EXIT_SUCCESS;
}
