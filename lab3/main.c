#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#define MAX_LINE   1024
#define MAX_TOKENS 128
#define MAX_ARGS   64

typedef struct {
    char *argv[MAX_ARGS];
    int argc;

    char *input_file;
    char *output_file;
} Command;

static int is_operator_char(char c)
{
    return c == '|' ||
           c == '<' ||
           c == '>' ||
           c == '&';
}

static void free_tokens(char *tokens[], int count)
{
    for (int i = 0; i < count; i++) {
        free(tokens[i]);
    }
}

static int tokenize(
    const char *line,
    char *tokens[],
    int max_tokens
)
{
    int count = 0;
    size_t i = 0;

    while (line[i] != '\0') {

        while (isspace((unsigned char)line[i])) {
            i++;
        }

        if (line[i] == '\0') {
            break;
        }

        if (count >= max_tokens) {
            fprintf(stderr, "too many tokens\n");
            return -1;
        }

        if (is_operator_char(line[i])) {

            char *token = malloc(2);

            if (token == NULL) {
                perror("malloc");
                return -1;
            }

            token[0] = line[i];
            token[1] = '\0';

            tokens[count++] = token;

            i++;
            continue;
        }

        char buffer[MAX_LINE];
        size_t len = 0;
        int started = 0;

        while (
            line[i] != '\0' &&
            !isspace((unsigned char)line[i]) &&
            !is_operator_char(line[i])
        ) {
            started = 1;

            if (line[i] == '\'' || line[i] == '"') {

                char quote = line[i];
                i++;

                while (
                    line[i] != '\0' &&
                    line[i] != quote
                ) {

                    if (
                        quote == '"' &&
                        line[i] == '\\' &&
                        line[i + 1] != '\0'
                    ) {
                        i++;
                    }

                    if (len + 1 >= sizeof(buffer)) {
                        fprintf(stderr, "token too long\n");
                        return -1;
                    }

                    buffer[len++] = line[i++];
                }

                if (line[i] != quote) {
                    fprintf(stderr, "unclosed quote\n");
                    return -1;
                }

                i++;

                continue;
            }

            if (
                line[i] == '\\' &&
                line[i + 1] != '\0'
            ) {
                i++;
            }

            if (len + 1 >= sizeof(buffer)) {
                fprintf(stderr, "token too long\n");
                return -1;
            }

            buffer[len++] = line[i++];
        }

        if (!started) {
            continue;
        }

        buffer[len] = '\0';

        tokens[count] = strdup(buffer);

        if (tokens[count] == NULL) {
            perror("strdup");
            return -1;
        }

        count++;
    }

    return count;
}

static void init_command(Command *cmd)
{
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;

    for (int i = 0; i < MAX_ARGS; i++) {
        cmd->argv[i] = NULL;
    }
}

static int parse_command(
    char *tokens[],
    int start,
    int end,
    Command *cmd
)
{
    init_command(cmd);

    for (int i = start; i < end; i++) {

        if (strcmp(tokens[i], "<") == 0) {

            if (cmd->input_file != NULL) {
                fprintf(
                    stderr,
                    "multiple input redirections\n"
                );
                return -1;
            }

            if (i + 1 >= end) {
                fprintf(
                    stderr,
                    "expected file after <\n"
                );
                return -1;
            }

            cmd->input_file = tokens[++i];
            continue;
        }

        if (strcmp(tokens[i], ">") == 0) {

            if (cmd->output_file != NULL) {
                fprintf(
                    stderr,
                    "multiple output redirections\n"
                );
                return -1;
            }

            if (i + 1 >= end) {
                fprintf(
                    stderr,
                    "expected file after >\n"
                );
                return -1;
            }

            cmd->output_file = tokens[++i];
            continue;
        }

        if (
            strcmp(tokens[i], "|") == 0 ||
            strcmp(tokens[i], "&") == 0
        ) {
            fprintf(
                stderr,
                "unexpected token: %s\n",
                tokens[i]
            );

            return -1;
        }

        if (cmd->argc >= MAX_ARGS - 1) {
            fprintf(stderr, "too many arguments\n");
            return -1;
        }

        cmd->argv[cmd->argc++] = tokens[i];
    }

    cmd->argv[cmd->argc] = NULL;

    if (cmd->argc == 0) {
        fprintf(stderr, "empty command\n");
        return -1;
    }

    return 0;
}

static int redirect_input(const char *filename)
{
    int fd = open(filename, O_RDONLY);

    if (fd < 0) {
        perror(filename);
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) < 0) {
        perror("dup2 stdin");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}

static int redirect_output(const char *filename)
{
    int fd = open(
        filename,
        O_WRONLY | O_CREAT | O_TRUNC,
        0644
    );

    if (fd < 0) {
        perror(filename);
        return -1;
    }

    if (dup2(fd, STDOUT_FILENO) < 0) {
        perror("dup2 stdout");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}

static void print_status(pid_t pid, int status)
{
    if (WIFEXITED(status)) {

        printf(
            "[process %d exited with status %d]\n",
            pid,
            WEXITSTATUS(status)
        );

    } else if (WIFSIGNALED(status)) {

        printf(
            "[process %d terminated by signal %d]\n",
            pid,
            WTERMSIG(status)
        );
    }
}

static void wait_for_pid(pid_t pid)
{
    int status;

    while (1) {

        pid_t result = waitpid(pid, &status, 0);

        if (result > 0) {
            print_status(pid, status);
            return;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result < 0) {
            perror("waitpid");
        }

        return;
    }
}

static void reap_background_processes(void)
{
    int status;
    pid_t pid;

    while (
        (pid = waitpid(-1, &status, WNOHANG)) > 0
    ) {

        if (WIFEXITED(status)) {

            printf(
                "[background pid %d finished, status %d]\n",
                pid,
                WEXITSTATUS(status)
            );

        } else if (WIFSIGNALED(status)) {

            printf(
                "[background pid %d killed by signal %d]\n",
                pid,
                WTERMSIG(status)
            );
        }
    }
}

static void run_single_command(
    const Command *cmd,
    int background
)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {

        if (cmd->input_file != NULL) {

            if (
                redirect_input(cmd->input_file) < 0
            ) {
                _exit(EXIT_FAILURE);
            }
        }

        if (cmd->output_file != NULL) {

            if (
                redirect_output(cmd->output_file) < 0
            ) {
                _exit(EXIT_FAILURE);
            }
        }

        execvp(cmd->argv[0], cmd->argv);

        perror("execvp");
        _exit(EXIT_FAILURE);
    }

    if (background) {

        printf("[pid %d]\n", pid);

    } else {

        wait_for_pid(pid);
    }
}

static void run_pipeline(
    const Command *left,
    const Command *right,
    int background
)
{
    int pipefd[2];

    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    pid_t left_pid = fork();

    if (left_pid < 0) {
        perror("fork");

        close(pipefd[0]);
        close(pipefd[1]);

        return;
    }

    if (left_pid == 0) {

        if (left->input_file != NULL) {

            if (
                redirect_input(left->input_file) < 0
            ) {
                _exit(EXIT_FAILURE);
            }
        }

        if (
            dup2(
                pipefd[1],
                STDOUT_FILENO
            ) < 0
        ) {
            perror("dup2 pipe stdout");
            _exit(EXIT_FAILURE);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        execvp(
            left->argv[0],
            left->argv
        );

        perror("execvp left");
        _exit(EXIT_FAILURE);
    }

    pid_t right_pid = fork();

    if (right_pid < 0) {

        perror("fork");

        close(pipefd[0]);
        close(pipefd[1]);

        wait_for_pid(left_pid);

        return;
    }

    if (right_pid == 0) {

        if (
            dup2(
                pipefd[0],
                STDIN_FILENO
            ) < 0
        ) {
            perror("dup2 pipe stdin");
            _exit(EXIT_FAILURE);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        if (right->output_file != NULL) {

            if (
                redirect_output(
                    right->output_file
                ) < 0
            ) {
                _exit(EXIT_FAILURE);
            }
        }

        execvp(
            right->argv[0],
            right->argv
        );

        perror("execvp right");
        _exit(EXIT_FAILURE);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    if (background) {

        printf(
            "[pipeline pids %d %d]\n",
            left_pid,
            right_pid
        );

    } else {

        wait_for_pid(left_pid);
        wait_for_pid(right_pid);
    }
}

int main(void)
{
    char line[MAX_LINE];

    printf(
        "myshell started: PID=%d PPID=%d\n",
        getpid(),
        getppid()
    );

    while (1) {

        reap_background_processes();

        printf("myshell> ");
        fflush(stdout);

        if (
            fgets(
                line,
                sizeof(line),
                stdin
            ) == NULL
        ) {
            printf("\n");
            break;
        }

        char *tokens[MAX_TOKENS];

        int token_count = tokenize(
            line,
            tokens,
            MAX_TOKENS
        );

        if (token_count < 0) {
            continue;
        }

        if (token_count == 0) {
            continue;
        }

        int background = 0;
        int effective_count = token_count;

        if (
            strcmp(
                tokens[token_count - 1],
                "&"
            ) == 0
        ) {
            background = 1;
            effective_count--;
        }

        int syntax_error = 0;

        for (
            int i = 0;
            i < effective_count;
            i++
        ) {
            if (
                strcmp(tokens[i], "&") == 0
            ) {
                fprintf(
                    stderr,
                    "& is only supported at the end\n"
                );

                syntax_error = 1;
                break;
            }
        }

        if (syntax_error) {
            free_tokens(tokens, token_count);
            continue;
        }

        if (effective_count == 0) {
            free_tokens(tokens, token_count);
            continue;
        }

        if (
            effective_count == 1 &&
            strcmp(tokens[0], "exit") == 0
        ) {
            free_tokens(tokens, token_count);
            break;
        }

        int pipe_index = -1;

        for (
            int i = 0;
            i < effective_count;
            i++
        ) {

            if (
                strcmp(tokens[i], "|") == 0
            ) {

                if (pipe_index != -1) {

                    fprintf(
                        stderr,
                        "only one pipe is supported\n"
                    );

                    syntax_error = 1;
                    break;
                }

                pipe_index = i;
            }
        }

        if (syntax_error) {
            free_tokens(tokens, token_count);
            continue;
        }

        if (pipe_index == -1) {

            Command cmd;

            if (
                parse_command(
                    tokens,
                    0,
                    effective_count,
                    &cmd
                ) < 0
            ) {
                free_tokens(
                    tokens,
                    token_count
                );

                continue;
            }

            run_single_command(
                &cmd,
                background
            );

            free_tokens(
                tokens,
                token_count
            );

            continue;
        }

        if (
            pipe_index == 0 ||
            pipe_index == effective_count - 1
        ) {
            fprintf(
                stderr,
                "invalid pipe syntax\n"
            );

            free_tokens(
                tokens,
                token_count
            );

            continue;
        }

        Command left;
        Command right;

        if (
            parse_command(
                tokens,
                0,
                pipe_index,
                &left
            ) < 0
        ) {
            free_tokens(
                tokens,
                token_count
            );

            continue;
        }

        if (
            parse_command(
                tokens,
                pipe_index + 1,
                effective_count,
                &right
            ) < 0
        ) {
            free_tokens(
                tokens,
                token_count
            );

            continue;
        }

        if (left.output_file != NULL) {

            fprintf(
                stderr,
                "output redirection on left side "
                "of pipe is not supported\n"
            );

            free_tokens(
                tokens,
                token_count
            );

            continue;
        }

        if (right.input_file != NULL) {

            fprintf(
                stderr,
                "input redirection on right side "
                "of pipe is not supported\n"
            );

            free_tokens(
                tokens,
                token_count
            );

            continue;
        }

        run_pipeline(
            &left,
            &right,
            background
        );

        free_tokens(
            tokens,
            token_count
        );
    }

    reap_background_processes();

    printf("myshell exiting\n");

    return 0;
}
