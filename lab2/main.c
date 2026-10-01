#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

static void reap_background_processes(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

        if (WIFEXITED(status)) {
            printf(
                "[background pid %d finished, exit status %d]\n",
                pid,
                WEXITSTATUS(status)
            );
        }
        else if (WIFSIGNALED(status)) {
            printf(
                "[background pid %d killed by signal %d]\n",
                pid,
                WTERMSIG(status)
            );
        }
        else {
            printf(
                "[background pid %d finished]\n",
                pid
            );
        }
    }
}

int main(void)
{
    char line[MAX_LINE];
    char *args[MAX_ARGS];

    printf(
        "myshell started: PID=%d PPID=%d\n",
        getpid(),
        getppid()
    );

    while (1) {

        reap_background_processes();

        printf("myshell> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        int argc = 0;

        char *token = strtok(line, " \t\n");

        while (token != NULL && argc < MAX_ARGS - 1) {
            args[argc++] = token;
            token = strtok(NULL, " \t\n");
        }

        args[argc] = NULL;

        if (argc == 0) {
            continue;
        }

        if (strcmp(args[0], "exit") == 0) {
            printf("myshell exiting\n");
            break;
        }

        int background = 0;

        if (argc > 0 &&
            strcmp(args[argc - 1], "&") == 0) {

            background = 1;

            args[argc - 1] = NULL;
            argc--;
        }

        if (argc == 0) {
            continue;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) {

            printf(
                "[child before exec] PID=%d PPID=%d\n",
                getpid(),
                getppid()
            );

            execvp(args[0], args);

            perror("execvp");

            _exit(EXIT_FAILURE);
        }

        if (background) {

            printf("[pid %d]\n", pid);

        } else {

            int status;

            pid_t result = waitpid(pid, &status, 0);

            if (result < 0) {
                perror("waitpid");
                continue;
            }

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
    }

    reap_background_processes();

    return 0;
}
