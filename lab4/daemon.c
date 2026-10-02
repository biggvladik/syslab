#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define LOG_FILE    "/tmp/lab4-daemon.log"
#define PID_FILE    "/tmp/lab4-daemon.pid"
#define CONFIG_FILE "/tmp/lab4-daemon.conf"
#define INTERVAL    5


static volatile sig_atomic_t got_sigterm = 0;
static volatile sig_atomic_t got_sighup  = 0;
static volatile sig_atomic_t got_sigusr1 = 0;
static volatile sig_atomic_t got_sigalrm = 0;


struct config {
    char message[256];
};

static struct config current_config = {
    .message = "daemon is alive"
};


static unsigned long tick_count   = 0;
static unsigned long reload_count = 0;
static unsigned long usr1_count   = 0;

static time_t start_time;


static void log_message(const char *fmt, ...)
{
    FILE *file;
    time_t now;
    struct tm tm_now;
    char time_buffer[64];

    file = fopen(LOG_FILE, "a");

    if (file == NULL) {
        return;
    }

    now = time(NULL);

    localtime_r(&now, &tm_now);

    strftime(
        time_buffer,
        sizeof(time_buffer),
        "%Y-%m-%d %H:%M:%S",
        &tm_now
    );

    fprintf(file, "[%s] ", time_buffer);

    va_list args;
    va_start(args, fmt);

    vfprintf(file, fmt, args);

    va_end(args);

    fprintf(file, "\n");

    fclose(file);
}


static void signal_handler(int sig)
{
    switch (sig) {
        case SIGTERM:
            got_sigterm = 1;
            break;

        case SIGHUP:
            got_sighup = 1;
            break;

        case SIGUSR1:
            got_sigusr1 = 1;
            break;

        case SIGALRM:
            got_sigalrm = 1;
            break;
    }
}


static int install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = signal_handler;


    sigemptyset(&sa.sa_mask);

    sigaddset(&sa.sa_mask, SIGTERM);
    sigaddset(&sa.sa_mask, SIGHUP);
    sigaddset(&sa.sa_mask, SIGUSR1);
    sigaddset(&sa.sa_mask, SIGALRM);

    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        return -1;
    }

    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        return -1;
    }

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        return -1;
    }

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        return -1;
    }

    return 0;
}


static int load_config(void)
{
    FILE *file;
    char line[512];

    file = fopen(CONFIG_FILE, "r");

    if (file == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {

        if (strncmp(line, "message=", 8) == 0) {

            char *value = line + 8;


            value[strcspn(value, "\r\n")] = '\0';

            snprintf(
                current_config.message,
                sizeof(current_config.message),
                "%s",
                value
            );
        }
    }

    fclose(file);

    return 0;
}


static int write_pid_file(void)
{
    FILE *file;

    file = fopen(PID_FILE, "w");

    if (file == NULL) {
        return -1;
    }

    fprintf(file, "%ld\n", (long)getpid());

    fclose(file);

    return 0;
}


static pid_t read_pid_file(void)
{
    FILE *file;
    long pid;

    file = fopen(PID_FILE, "r");

    if (file == NULL) {
        return -1;
    }

    if (fscanf(file, "%ld", &pid) != 1) {
        fclose(file);
        return -1;
    }

    fclose(file);

    if (pid <= 1) {
        return -1;
    }

    return (pid_t)pid;
}


static int daemon_is_running(void)
{
    pid_t pid = read_pid_file();

    if (pid == -1) {
        return 0;
    }

    if (kill(pid, 0) == 0) {
        return 1;
    }

    if (errno == EPERM) {


        return 1;
    }


    unlink(PID_FILE);

    return 0;
}


static int daemonize(void)
{
    pid_t pid;
    int null_fd;


    pid = fork();

    if (pid < 0) {
        return -1;
    }


    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }


    if (setsid() == -1) {
        return -1;
    }


    pid = fork();

    if (pid < 0) {
        return -1;
    }

    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }


    if (chdir("/") == -1) {
        return -1;
    }


    umask(0);


    null_fd = open("/dev/null", O_RDWR);

    if (null_fd == -1) {
        return -1;
    }

    if (dup2(null_fd, STDIN_FILENO) == -1) {
        return -1;
    }

    if (dup2(null_fd, STDOUT_FILENO) == -1) {
        return -1;
    }

    if (dup2(null_fd, STDERR_FILENO) == -1) {
        return -1;
    }

    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }

    return 0;
}


static void print_statistics(void)
{
    time_t now = time(NULL);

    long uptime = (long)difftime(now, start_time);

    log_message(
        "statistics: pid=%ld uptime=%ld sec ticks=%lu reloads=%lu SIGUSR1=%lu",
        (long)getpid(),
        uptime,
        tick_count,
        reload_count,
        usr1_count
    );
}


static int send_signal_to_daemon(int sig)
{
    pid_t pid = read_pid_file();

    if (pid == -1) {
        fprintf(stderr, "Cannot read PID file: %s\n", PID_FILE);
        return EXIT_FAILURE;
    }

    if (kill(pid, sig) == -1) {
        perror("kill");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


int main(int argc, char *argv[])
{
    sigset_t blocked_signals;
    sigset_t old_mask;
    sigset_t wait_mask;


    if (argc == 2) {

        if (strcmp(argv[1], "--stop") == 0) {
            return send_signal_to_daemon(SIGTERM);
        }

        if (strcmp(argv[1], "--reload") == 0) {
            return send_signal_to_daemon(SIGHUP);
        }

        if (strcmp(argv[1], "--stats") == 0) {
            return send_signal_to_daemon(SIGUSR1);
        }

        fprintf(
            stderr,
            "Usage: %s [--stop | --reload | --stats]\n",
            argv[0]
        );

        return EXIT_FAILURE;
    }


    if (daemon_is_running()) {
        fprintf(stderr, "Daemon is already running\n");
        return EXIT_FAILURE;
    }


    sigemptyset(&blocked_signals);

    sigaddset(&blocked_signals, SIGTERM);
    sigaddset(&blocked_signals, SIGHUP);
    sigaddset(&blocked_signals, SIGUSR1);
    sigaddset(&blocked_signals, SIGALRM);


    if (sigprocmask(
            SIG_BLOCK,
            &blocked_signals,
            &old_mask
        ) == -1) {

        perror("sigprocmask");
        return EXIT_FAILURE;
    }


    if (install_signal_handlers() == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }


    if (daemonize() == -1) {
        return EXIT_FAILURE;
    }


    if (write_pid_file() == -1) {
        return EXIT_FAILURE;
    }


    start_time = time(NULL);


    if (load_config() == 0) {
        log_message(
            "configuration loaded: message=\"%s\"",
            current_config.message
        );
    } else {
        log_message(
            "cannot read %s, using default configuration",
            CONFIG_FILE
        );
    }


    log_message(
        "daemon started, pid=%ld",
        (long)getpid()
    );


    alarm(INTERVAL);


    wait_mask = old_mask;

    sigdelset(&wait_mask, SIGTERM);
    sigdelset(&wait_mask, SIGHUP);
    sigdelset(&wait_mask, SIGUSR1);
    sigdelset(&wait_mask, SIGALRM);


    for (;;) {


        sigsuspend(&wait_mask);


        if (got_sigterm) {

            got_sigterm = 0;

            log_message(
                "SIGTERM received, shutting down"
            );

            break;
        }


        if (got_sighup) {

            got_sighup = 0;

            reload_count++;

            if (load_config() == 0) {

                log_message(
                    "SIGHUP: configuration reloaded, message=\"%s\"",
                    current_config.message
                );

            } else {

                log_message(
                    "SIGHUP: cannot reload configuration from %s",
                    CONFIG_FILE
                );
            }
        }


        if (got_sigusr1) {

            got_sigusr1 = 0;

            usr1_count++;

            print_statistics();
        }


        if (got_sigalrm) {

            got_sigalrm = 0;

            tick_count++;

            log_message(
                "tick #%lu: %s",
                tick_count,
                current_config.message
            );


            alarm(INTERVAL);
        }
    }


    alarm(0);


    unlink(PID_FILE);


    log_message(
        "daemon stopped"
    );


    sigprocmask(
        SIG_SETMASK,
        &old_mask,
        NULL
    );


    return EXIT_SUCCESS;
}
