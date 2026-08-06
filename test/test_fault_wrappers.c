#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fmtmsg.h>
#include <fnmatch.h>
#include <ftw.h>
#include <limits.h>
#include <math.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_ipc/ipc.h>
#include <pthread.h>
#include <search.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmpx.h>

static int    failures;
static size_t fault_resource_events;
static FILE  *outcome_stream;
static bool   native_child_process;
static int    native_child_status = EXIT_SUCCESS;

#define P101_TEST_ERRNO_SENTINEL 0x5A5A

#ifdef __linux__
    #define P101_TEST_PLATFORM "linux"
#elif defined(__APPLE__)
    #define P101_TEST_PLATFORM "macos"
#elif defined(__FreeBSD__)
    #define P101_TEST_PLATFORM "freebsd"
#else
    #define P101_TEST_PLATFORM "posix"
#endif

#define EXPECT(condition)                                                                                                                                                                                                                                          \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        if(!(condition))                                                                                                                                                                                                                                           \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                   \
            failures++;                                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_CLEANUP_ERRNO(expression)                                                                                                                                                                                                                      \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        if((expression) != 0)                                                                                                                                                                                                                                      \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "native cleanup failed: %s: %s\n", #expression, strerror(errno));                                                                                                                                                                      \
            native_passed = false;                                                                                                                                                                                                                                 \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_CLEANUP_STATUS(expression)                                                                                                                                                                                                                     \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        int p101_cleanup_status_ = (expression);                                                                                                                                                                                                                   \
        if(p101_cleanup_status_ != 0)                                                                                                                                                                                                                              \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "native cleanup failed: %s: status %d\n", #expression, p101_cleanup_status_);                                                                                                                                                          \
            native_passed = false;                                                                                                                                                                                                                                 \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_CLEANUP_UNLINK_IF_PRESENT(path)                                                                                                                                                                                                                \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        bool p101_cleanup_ok_;                                                                                                                                                                                                                                     \
                                                                                                                                                                                                                                                                   \
        p101_cleanup_ok_ = native_unlink_if_present(path);                                                                                                                                                                                                         \
        if(!p101_cleanup_ok_)                                                                                                                                                                                                                                      \
        {                                                                                                                                                                                                                                                          \
            native_passed = false;                                                                                                                                                                                                                                 \
        }                                                                                                                                                                                                                                                          \
    } while(0)

#define P101_NATIVE_FORMAT_PID_PATH_OR_SKIP(buffer, format)                                                                                                                                                                                                        \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        int p101_format_length_;                                                                                                                                                                                                                                   \
                                                                                                                                                                                                                                                                   \
        p101_format_length_ = snprintf((buffer), sizeof(buffer), (format), (long)getpid());                                                                                                                                                                        \
        if(p101_format_length_ < 0 || (size_t)p101_format_length_ >= sizeof(buffer))                                                                                                                                                                               \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "native setup failed: path formatting\n");                                                                                                                                                                                             \
            native_child_status = 77;                                                                                                                                                                                                                              \
            goto native_child_done_;                                                                                                                                                                                                                               \
        }                                                                                                                                                                                                                                                          \
    } while(0)

struct fault_state
{
    int checks;
    int code;
};

static pid_t native_waitpid_nointr(pid_t pid, int *status) P101_ATTR_SEMANTIC_ROLE("p101:test:eintr-safe-wait-adapter")
{
    pid_t result;

    do
    {
        result = waitpid(pid, status, 0);
    } while(result < 0 && errno == EINTR);
    return result;
}

static void write_outcome(const char *wrapper, const char *domain, const char *symbol, int code, int passed)
{
    int written;

    if(outcome_stream != NULL)
    {
        written = fprintf(outcome_stream, "P101WRAPPER\t1\tFAULT\t%s\tlib_ipc\t%s\t%s\t%s\t%d\t%s\n", P101_TEST_PLATFORM, wrapper, domain, symbol, code, passed ? "PASS" : "FAIL");
        if(written < 0 || fflush(outcome_stream) != 0)
        {
            fprintf(stderr, "FAIL: cannot write wrapper outcome receipt\n");
            failures++;
        }
    }
}

static int fail_next_call(const struct p101_env *env, const char *call_name, void *user_data)
{
    struct fault_state *state;

    (void)env;
    (void)call_name;
    state = user_data;
    state->checks++;
    return state->code;
}

static void count_fd_event(const struct p101_env *env, p101_env_fd_event event, int fd, const char *file_name, const char *function_name, int line_number, void *user_data)
{
    (void)env;
    (void)event;
    (void)fd;
    (void)file_name;
    (void)function_name;
    (void)line_number;
    (void)user_data;
    fault_resource_events++;
}

static void count_alloc_event(const struct p101_env *env, p101_env_alloc_event event, const void *ptr, const void *new_ptr, size_t size, const char *file_name, const char *function_name, int line_number, void *user_data)
{
    (void)env;
    (void)event;
    (void)ptr;
    (void)new_ptr;
    (void)size;
    (void)file_name;
    (void)function_name;
    (void)line_number;
    (void)user_data;
    fault_resource_events++;
}

static void count_resource_event(const struct p101_env *env, p101_env_resource_kind event, const char *resource_class, const char *resource_id, const char *related_id, size_t size, const char *metadata, const char *file_name, const char *function_name,
                                 int line_number, void *user_data)
{
    (void)env;
    (void)event;
    (void)resource_class;
    (void)resource_id;
    (void)related_id;
    (void)size;
    (void)metadata;
    (void)file_name;
    (void)function_name;
    (void)line_number;
    (void)user_data;
    fault_resource_events++;
}

/* P101_TEST_CASE(p101_ftok) */
static void test_p101_ftok(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#else
    static const int         errors[]      = {EACCES, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOTDIR};
    static const char *const error_names[] = {"EACCES", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOTDIR"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        key_t result = p101_ftok(env, err, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_ftok", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            key_t native_result = p101_ftok(native_env, native_err, ".", 1);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_ftok: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_ftok: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_ftok: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_mkfifo) */
static void test_p101_mkfifo(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EILSEQ, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, ENOTSUP, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EILSEQ", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "ENOTSUP", "EROFS"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EBADF, EDQUOT, EEXIST, EFAULT, EIO, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, ENOTSUP, EPERM, EROFS};
    static const char *const error_names[] = {"EACCES", "EBADF", "EDQUOT", "EEXIST", "EFAULT", "EIO", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "ENOTSUP", "EPERM", "EROFS"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EILSEQ, ELOOP, ENAMETOOLONG, ENOENT, ENOSPC, ENOTDIR, EROFS};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EILSEQ", "ELOOP", "ENAMETOOLONG", "ENOENT", "ENOSPC", "ENOTDIR", "EROFS"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_mkfifo(env, err, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_mkfifo", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char native_argument_2[96];
            P101_NATIVE_FORMAT_PID_PATH_OR_SKIP(native_argument_2, "/tmp/p101-wrapper-fifo-%ld");
            errno = 0;
            if(unlink(native_argument_2) != 0 && errno != ENOENT)
            {
                fprintf(stderr, "native setup failed: p101_mkfifo: unlink: %s\n", strerror(errno));
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_mkfifo(native_env, native_err, native_argument_2, 0600);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_mkfifo: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            P101_NATIVE_CLEANUP_ERRNO(unlink(native_argument_2));
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_mkfifo: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_mkfifo: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_msgctl) */
static void test_p101_msgctl(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EIDRM, EINVAL, EPERM};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIDRM", "EINVAL", "EPERM"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EINVAL, EPERM};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EFAULT, EINVAL, EPERM};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EINVAL", "EPERM"};
#else
    static const int         errors[]      = {EACCES, EINVAL, EPERM};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_msgctl(env, err, 0, 0, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_msgctl", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_msgctl(env, err, -1, 0, NULL);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_msgctl: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_msgctl(native_env, native_err, native_argument_2, IPC_RMID, NULL);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_msgctl: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result != 0)
            {
                if(msgctl(native_argument_2, IPC_RMID, NULL) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_msgctl: msgctl(IPC_RMID): %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_msgctl: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_msgctl: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_msgget) */
static void test_p101_msgget(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EEXIST, ENOENT, ENOMEM, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "ENOENT", "ENOMEM", "ENOSPC"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EEXIST, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "ENOENT", "ENOSPC"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EEXIST, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "ENOENT", "ENOSPC"};
#else
    static const int         errors[]      = {EACCES, EEXIST, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "ENOENT", "ENOSPC"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_msgget(env, err, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_msgget", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_msgget(native_env, native_err, IPC_PRIVATE, IPC_CREAT | 0600);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_msgget: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result >= 0)
            {
                if(msgctl(native_result, IPC_RMID, NULL) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_msgget: msgctl(IPC_RMID): %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_msgget: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_msgget: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_msgrcv) */
static void test_p101_msgrcv(struct p101_env *env, struct p101_error *err)
{
    unsigned char argument_3[64];
    unsigned char argument_3_before[sizeof(argument_3)];
    memset(argument_3, 0xA5, sizeof(argument_3));
    memcpy(argument_3_before, argument_3, sizeof(argument_3));
#ifdef __linux__
    static const int         errors[]      = {E2BIG, EACCES, EFAULT, EIDRM, EINTR, EINVAL, ENOMSG, ENOSYS};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EFAULT", "EIDRM", "EINTR", "EINVAL", "ENOMSG", "ENOSYS"};
#elif defined(__APPLE__)
    static const int         errors[]      = {E2BIG, EACCES, EIDRM, EINTR, EINVAL, ENOMSG};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EIDRM", "EINTR", "EINVAL", "ENOMSG"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {E2BIG, EACCES, EFAULT, EINTR, EINVAL, ENOMSG};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EFAULT", "EINTR", "EINVAL", "ENOMSG"};
#else
    static const int         errors[]      = {E2BIG, EACCES, EIDRM, EINTR, EINVAL, ENOMSG};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EIDRM", "EINTR", "EINVAL", "ENOMSG"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        ssize_t result = p101_msgrcv(env, err, 0, argument_3, 0, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(memcmp(argument_3, argument_3_before, sizeof(argument_3)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_msgrcv", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events   = 0U;
        errno                   = P101_TEST_ERRNO_SENTINEL;
        ssize_t portable_result = p101_msgrcv(env, err, -1, argument_3, 0, 0, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_msgrcv: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }

            struct
            {
                long type;
            } native_argument_2_message = {1L};

            if(msgsnd(native_argument_2, &native_argument_2_message, 0U, 0) != 0)
            {
                if(msgctl(native_argument_2, IPC_RMID, NULL) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_msgrcv: msgctl(IPC_RMID): %s\n", strerror(errno));
                    native_child_status = EXIT_FAILURE;
                    goto native_child_done_;
                }
                native_child_status = 77;
                goto native_child_done_;
            }

            struct
            {
                long type;
            } native_argument_3 = {1L};

            ssize_t native_result = p101_msgrcv(native_env, native_err, native_argument_2, &native_argument_3, 0U, 0, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_msgrcv: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(msgctl(native_argument_2, IPC_RMID, NULL) != 0)
            {
                fprintf(stderr, "native cleanup failed: p101_msgrcv: msgctl(IPC_RMID): %s\n", strerror(errno));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_msgrcv: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_msgrcv: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_msgsnd) */
static void test_p101_msgsnd(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EAGAIN, EFAULT, EIDRM, EINTR, EINVAL, ENOMEM};
    static const char *const error_names[] = {"EACCES", "EAGAIN", "EFAULT", "EIDRM", "EINTR", "EINVAL", "ENOMEM"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EAGAIN, EIDRM, EINTR, EINVAL};
    static const char *const error_names[] = {"EACCES", "EAGAIN", "EIDRM", "EINTR", "EINVAL"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EAGAIN, EFAULT, EINTR, EINVAL};
    static const char *const error_names[] = {"EACCES", "EAGAIN", "EFAULT", "EINTR", "EINVAL"};
#else
    static const int         errors[]      = {EACCES, EAGAIN, EIDRM, EINTR, EINVAL};
    static const char *const error_names[] = {"EACCES", "EAGAIN", "EIDRM", "EINTR", "EINVAL"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_msgsnd(env, err, 0, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_msgsnd", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_msgsnd(env, err, -1, NULL, 0, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_msgsnd: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }

            struct
            {
                long type;
            } native_argument_3 = {1L};

            int native_result = p101_msgsnd(native_env, native_err, native_argument_2, &native_argument_3, 0U, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_msgsnd: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(msgctl(native_argument_2, IPC_RMID, NULL) != 0)
            {
                fprintf(stderr, "native cleanup failed: p101_msgsnd: msgctl(IPC_RMID): %s\n", strerror(errno));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_msgsnd: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_msgsnd: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_pipe) */
static void test_p101_pipe(struct p101_env *env, struct p101_error *err)
{
    int           argument_2[4];
    unsigned char argument_2_before[sizeof(argument_2)];
    memset(argument_2, 0xA5, sizeof(argument_2));
    memcpy(argument_2_before, argument_2, sizeof(argument_2));
#ifdef __linux__
    static const int         errors[]      = {EFAULT, EMFILE, ENFILE};
    static const char *const error_names[] = {"EFAULT", "EMFILE", "ENFILE"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EFAULT, EMFILE, ENFILE};
    static const char *const error_names[] = {"EFAULT", "EMFILE", "ENFILE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EFAULT, EMFILE, ENFILE, ENOMEM};
    static const char *const error_names[] = {"EFAULT", "EMFILE", "ENFILE", "ENOMEM"};
#else
    static const int         errors[]      = {EMFILE, ENFILE};
    static const char *const error_names[] = {"EMFILE", "ENFILE"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_pipe(env, err, argument_2);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(memcmp(argument_2, argument_2_before, sizeof(argument_2)) == 0);
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_pipe", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2[2] = {-1, -1};
            int native_result        = p101_pipe(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_pipe: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_argument_2[0] >= 0)
            {
                P101_NATIVE_CLEANUP_ERRNO(close(native_argument_2[0]));
            }
            if(native_argument_2[1] >= 0)
            {
                P101_NATIVE_CLEANUP_ERRNO(close(native_argument_2[1]));
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_pipe: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_pipe: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_semctl) */
static void test_p101_semctl(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EIDRM, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIDRM", "EINVAL", "EPERM", "ERANGE"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM", "ERANGE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM", "ERANGE"};
#else
    static const int         errors[]      = {EACCES, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM", "ERANGE"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_semctl(env, err, 0, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_semctl", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_semctl(env, err, -1, 0, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_semctl: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_semctl(native_env, native_err, native_argument_2, 0, IPC_RMID);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_semctl: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result != 0)
            {
                if(semctl(native_argument_2, 0, IPC_RMID) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_semctl: semctl(IPC_RMID): %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_semctl: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_semctl: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_semctl_arg) */
static void test_p101_semctl_arg(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EIDRM, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIDRM", "EINVAL", "EPERM", "ERANGE"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM", "ERANGE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM", "ERANGE"};
#else
    static const int         errors[]      = {EACCES, EINVAL, EPERM, ERANGE};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM", "ERANGE"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_semctl_arg(env, err, 0, 0, 0, (union p101_semun){0});
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_semctl_arg", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_semctl_arg(env, err, -1, 0, 0, (union p101_semun){0});
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_semctl_arg: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_semctl_arg(native_env, native_err, native_argument_2, 0, SETVAL, (union p101_semun){.val = 1});
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_semctl_arg: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(semctl(native_argument_2, 0, IPC_RMID) != 0)
            {
                fprintf(stderr, "native cleanup failed: p101_semctl_arg: semctl(IPC_RMID): %s\n", strerror(errno));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_semctl_arg: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_semctl_arg: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_semget) */
static void test_p101_semget(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, ENOENT, ENOMEM, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "ENOENT", "ENOMEM", "ENOSPC"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "ENOENT", "ENOSPC"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "ENOENT", "ENOSPC"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "ENOENT", "ENOSPC"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_semget(env, err, 0, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_semget", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_semget(env, err, 0, -1, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_semget: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_semget(native_env, native_err, IPC_PRIVATE, 1, IPC_CREAT | 0600);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_semget: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result >= 0)
            {
                if(semctl(native_result, 0, IPC_RMID) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_semget: semctl(IPC_RMID): %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_semget: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_semget: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_semop) */
static void test_p101_semop(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {E2BIG, EACCES, EAGAIN, EFAULT, EFBIG, EIDRM, EINTR, EINVAL, ENOMEM, ERANGE};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EAGAIN", "EFAULT", "EFBIG", "EIDRM", "EINTR", "EINVAL", "ENOMEM", "ERANGE"};
#elif defined(__APPLE__)
    static const int         errors[]      = {E2BIG, EACCES, EAGAIN, EFBIG, EIDRM, EINTR, EINVAL, ENOSPC, ERANGE};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EAGAIN", "EFBIG", "EIDRM", "EINTR", "EINVAL", "ENOSPC", "ERANGE"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {E2BIG, EACCES, EAGAIN, EFBIG, EIDRM, EINTR, EINVAL, ENOSPC, ERANGE};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EAGAIN", "EFBIG", "EIDRM", "EINTR", "EINVAL", "ENOSPC", "ERANGE"};
#else
    static const int         errors[]      = {E2BIG, EACCES, EAGAIN, EFBIG, EIDRM, EINTR, EINVAL, ENOSPC, ERANGE};
    static const char *const error_names[] = {"E2BIG", "EACCES", "EAGAIN", "EFBIG", "EIDRM", "EINTR", "EINVAL", "ENOSPC", "ERANGE"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_semop(env, err, 0, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_semop", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_semop(env, err, -1, NULL, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_semop: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            struct sembuf native_argument_3 = {0U, 0, 0};
            int           native_result     = p101_semop(native_env, native_err, native_argument_2, &native_argument_3, 1U);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_semop: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(semctl(native_argument_2, 0, IPC_RMID) != 0)
            {
                fprintf(stderr, "native cleanup failed: p101_semop: semctl(IPC_RMID): %s\n", strerror(errno));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_semop: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_semop: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_shm_open) */
static void test_p101_shm_open(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, EMFILE, ENAMETOOLONG, ENFILE, ENOENT};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EEXIST, EINTR, EINVAL, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINTR", "EINVAL", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOSPC"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EEXIST, EFAULT, EINVAL, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOSYS, EOPNOTSUPP};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EFAULT", "EINVAL", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOSYS", "EOPNOTSUPP"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EINTR, EINVAL, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINTR", "EINVAL", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOSPC"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_shm_open(env, err, NULL, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_shm_open", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_open(env, err, "p101", 0, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_open: text-without-leading-slash\n");
        }
        p101_error_reset(err);
    }
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_open(env, err, "/", 0, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_open: text-root-only\n");
        }
        p101_error_reset(err);
    }
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_open(env, err, "/p101/invalid", 0, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_open: text-with-extra-slash\n");
        }
        p101_error_reset(err);
    }
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_open(env, err, NULL, 0, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_open: null\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char native_argument_2[96];
            P101_NATIVE_FORMAT_PID_PATH_OR_SKIP(native_argument_2, "/p101-wrapper-shm-%ld");
            errno = 0;
            if(shm_unlink(native_argument_2) != 0 && errno != ENOENT)
            {
                fprintf(stderr, "native setup failed: p101_shm_open: shm_unlink: %s\n", strerror(errno));
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_shm_open(native_env, native_err, native_argument_2, O_CREAT | O_EXCL | O_RDWR, 0600);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_shm_open: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result >= 0)
            {
                if(close(native_result) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_shm_open: close: %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            if(shm_unlink(native_argument_2) != 0)
            {
                fprintf(stderr, "native cleanup failed: p101_shm_open: shm_unlink: %s\n", strerror(errno));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_shm_open: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_shm_open: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_shm_unlink) */
static void test_p101_shm_unlink(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EMFILE, ENAMETOOLONG, ENFILE, ENOENT};
    static const char *const error_names[] = {"EACCES", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, ENAMETOOLONG, ENOENT};
    static const char *const error_names[] = {"EACCES", "ENAMETOOLONG", "ENOENT"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EFAULT, EINVAL, EMFILE, ENAMETOOLONG, ENFILE, ENOENT, ENOSYS, EOPNOTSUPP};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EINVAL", "EMFILE", "ENAMETOOLONG", "ENFILE", "ENOENT", "ENOSYS", "EOPNOTSUPP"};
#else
    static const int         errors[]      = {EACCES, ENAMETOOLONG, ENOENT};
    static const char *const error_names[] = {"EACCES", "ENAMETOOLONG", "ENOENT"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_shm_unlink(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_shm_unlink", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_unlink(env, err, "p101");
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_unlink: text-without-leading-slash\n");
        }
        p101_error_reset(err);
    }
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_unlink(env, err, "/");
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_unlink: text-root-only\n");
        }
        p101_error_reset(err);
    }
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_unlink(env, err, "/p101/invalid");
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_unlink: text-with-extra-slash\n");
        }
        p101_error_reset(err);
    }
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shm_unlink(env, err, NULL);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shm_unlink: null\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            char native_argument_2[96];
            P101_NATIVE_FORMAT_PID_PATH_OR_SKIP(native_argument_2, "/p101-wrapper-shm-%ld");
            errno = 0;
            if(shm_unlink(native_argument_2) != 0 && errno != ENOENT)
            {
                fprintf(stderr, "native setup failed: p101_shm_unlink: shm_unlink: %s\n", strerror(errno));
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2_fd = shm_open(native_argument_2, O_CREAT | O_EXCL | O_RDWR, 0600);
            if(native_argument_2_fd < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            if(close(native_argument_2_fd) != 0)
            {
                if(shm_unlink(native_argument_2) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_shm_unlink: shm_unlink: %s\n", strerror(errno));
                }
                native_child_status = EXIT_FAILURE;
                goto native_child_done_;
            }
            int native_result = p101_shm_unlink(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_shm_unlink: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result != 0)
            {
                P101_NATIVE_CLEANUP_ERRNO(shm_unlink(native_argument_2));
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_shm_unlink: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_shm_unlink: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_shmat) */
static void test_p101_shmat(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EIDRM, EINVAL, ENOMEM};
    static const char *const error_names[] = {"EACCES", "EIDRM", "EINVAL", "ENOMEM"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EINVAL, EMFILE, ENOMEM};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EMFILE", "ENOMEM"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EINVAL, EMFILE, ENOMEM};
    static const char *const error_names[] = {"EINVAL", "EMFILE", "ENOMEM"};
#else
    static const int         errors[]      = {EACCES, EINVAL, EMFILE, ENOMEM};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EMFILE", "ENOMEM"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        void *result = p101_shmat(env, err, 0, NULL, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == ((void *)-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_shmat", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        void *portable_result = p101_shmat(env, err, -1, NULL, 0);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == (void *)-1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shmat: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = shmget(IPC_PRIVATE, 1U, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            void *native_result = p101_shmat(native_env, native_err, native_argument_2, NULL, 0);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_shmat: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result != (void *)-1)
            {
                if(shmdt(native_result) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_shmat: shmdt: %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            if(shmctl(native_argument_2, IPC_RMID, NULL) != 0)
            {
                fprintf(stderr, "native cleanup failed: p101_shmat: shmctl(IPC_RMID): %s\n", strerror(errno));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_shmat: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_shmat: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_shmctl) */
static void test_p101_shmctl(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EFAULT, EIDRM, EINVAL, ENOMEM, EOVERFLOW, EPERM};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EIDRM", "EINVAL", "ENOMEM", "EOVERFLOW", "EPERM"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EFAULT, EINVAL, EPERM};
    static const char *const error_names[] = {"EACCES", "EFAULT", "EINVAL", "EPERM"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EACCES, EINVAL, EPERM};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EPERM"};
#else
    static const int         errors[]      = {EACCES, EINVAL, EOVERFLOW, EPERM};
    static const char *const error_names[] = {"EACCES", "EINVAL", "EOVERFLOW", "EPERM"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_shmctl(env, err, 0, 0, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_shmctl", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shmctl(env, err, -1, 0, NULL);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shmctl: negative-one\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_argument_2 = shmget(IPC_PRIVATE, 1U, IPC_CREAT | 0600);
            if(native_argument_2 < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_shmctl(native_env, native_err, native_argument_2, IPC_RMID, NULL);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_shmctl: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result != 0)
            {
                if(shmctl(native_argument_2, IPC_RMID, NULL) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_shmctl: shmctl(IPC_RMID): %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_shmctl: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_shmctl: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_shmdt) */
static void test_p101_shmdt(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#else
    static const int         errors[]      = {EINVAL};
    static const char *const error_names[] = {"EINVAL"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_shmdt(env, err, NULL);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_shmdt", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int failures_before = failures;

        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        int portable_result   = p101_shmdt(env, err, NULL);
        (void)portable_result;
        EXPECT(p101_error_is_errno(err, EINVAL));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(portable_result == -1);
        EXPECT(fault_resource_events == 0U);
        if(failures != failures_before)
        {
            fprintf(stderr, "portable rejection failed: p101_shmdt: null\n");
        }
        p101_error_reset(err);
    }
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int   native_argument_2_id = shmget(IPC_PRIVATE, 1U, IPC_CREAT | 0600);
            void *native_argument_2;
            if(native_argument_2_id < 0)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_argument_2 = shmat(native_argument_2_id, NULL, 0);
            if(native_argument_2 == (void *)-1)
            {
                if(shmctl(native_argument_2_id, IPC_RMID, NULL) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_shmdt: shmctl(IPC_RMID): %s\n", strerror(errno));
                    native_child_status = EXIT_FAILURE;
                    goto native_child_done_;
                }
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_shmdt(native_env, native_err, native_argument_2);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_shmdt: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result != 0)
            {
                if(shmdt(native_argument_2) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_shmdt: shmdt: %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            if(shmctl(native_argument_2_id, IPC_RMID, NULL) != 0)
            {
                fprintf(stderr, "native cleanup failed: p101_shmdt: shmctl(IPC_RMID): %s\n", strerror(errno));
                native_passed = false;
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_shmdt: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_shmdt: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

/* P101_TEST_CASE(p101_shmget) */
static void test_p101_shmget(struct p101_env *env, struct p101_error *err)
{
#ifdef __linux__
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, ENFILE, ENOENT, ENOMEM, ENOSPC, EPERM};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "ENFILE", "ENOENT", "ENOMEM", "ENOSPC", "EPERM"};
#elif defined(__APPLE__)
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, ENOENT, ENOMEM, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "ENOENT", "ENOMEM", "ENOSPC"};
#elif defined(__FreeBSD__)
    static const int         errors[]      = {EEXIST, EINVAL, ENOENT, ENOSPC};
    static const char *const error_names[] = {"EEXIST", "EINVAL", "ENOENT", "ENOSPC"};
#else
    static const int         errors[]      = {EACCES, EEXIST, EINVAL, ENOENT, ENOMEM, ENOSPC};
    static const char *const error_names[] = {"EACCES", "EEXIST", "EINVAL", "ENOENT", "ENOMEM", "ENOSPC"};
#endif

    for(size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        struct fault_state state = {0, errors[index]};
        int                failures_before;

        failures_before = failures;
        EXPECT(p101_error_has_no_error(err));
        fault_resource_events = 0U;
        errno                 = P101_TEST_ERRNO_SENTINEL;
        p101_env_set_fault_injector(env, fail_next_call, &state);
        int result = p101_shmget(env, err, 0, 0, 0);
        (void)result;
        EXPECT(state.checks == 1);
        EXPECT(p101_error_is_errno(err, state.code));
        EXPECT(errno == P101_TEST_ERRNO_SENTINEL);
        EXPECT(result == (-1));
        EXPECT(fault_resource_events == 0U);
        write_outcome("p101_shmget", "errno", error_names[index], state.code, failures == failures_before);
        p101_error_reset(err);
    }
    p101_env_set_fault_injector(env, NULL, NULL);
    {
        int   native_status = 0;
        pid_t native_pid    = fork();

        EXPECT(native_pid >= 0);
        if(native_pid == 0)
        {
            bool               native_passed = true;
            struct p101_error *native_err    = NULL;
            struct p101_env   *native_env    = NULL;

            native_child_process = true;
            failures             = 0;
            (void)alarm(2U);
            if(unsetenv("P101_CALL_LOG") != 0 || unsetenv("P101_RESOURCE_LOG") != 0)
            {
                fprintf(stderr, "native setup failed: cannot clear p101 logging environment\n");
                native_child_status = 77;
                goto native_child_done_;
            }
            native_err = p101_error_create(false);
            if(native_err == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            native_env = p101_env_create(native_err, NULL);
            if(native_env == NULL)
            {
                native_child_status = 77;
                goto native_child_done_;
            }
            int native_result = p101_shmget(native_env, native_err, IPC_PRIVATE, 1U, IPC_CREAT | 0600);
            (void)native_result;
            if(p101_error_has_error(native_err))
            {
                fprintf(stderr, "native smoke failed: p101_shmget: %s\n", p101_error_get_message(native_err));
                native_passed = false;
            }
            if(native_result >= 0)
            {
                if(shmctl(native_result, IPC_RMID, NULL) != 0)
                {
                    fprintf(stderr, "native cleanup failed: p101_shmget: shmctl(IPC_RMID): %s\n", strerror(errno));
                    native_passed = false;
                }
            }
            native_child_status = native_passed ? EXIT_SUCCESS : EXIT_FAILURE;
        native_child_done_:
            p101_env_destroy(native_env);
            p101_error_destroy(native_err);
        }
        if(native_pid > 0)
        {
            EXPECT(native_waitpid_nointr(native_pid, &native_status) == native_pid);
            if(WIFSIGNALED(native_status))
            {
                fprintf(stderr, "native smoke terminated by signal: p101_shmget: %d\n", WTERMSIG(native_status));
            }
            EXPECT(WIFEXITED(native_status));
            if(WIFEXITED(native_status))
            {
                if(WEXITSTATUS(native_status) != EXIT_SUCCESS)
                {
                    fprintf(stderr, "native smoke exited unsuccessfully: p101_shmget: %d\n", WEXITSTATUS(native_status));
                }
                EXPECT(WEXITSTATUS(native_status) == EXIT_SUCCESS);
            }
        }
        p101_error_reset(err);
    }
}

int main(void)
{
    const char        *outcome_path;
    struct p101_error *err = NULL;
    struct p101_env   *env = NULL;
    int                status;

    outcome_path = getenv("P101_WRAPPER_OUTCOME_LOG");
    if(outcome_path != NULL && outcome_path[0] != '\0')
    {
        outcome_stream = fopen(outcome_path, "a");
        if(outcome_stream == NULL)
        {
            fprintf(stderr, "FAIL: cannot open wrapper outcome receipt\n");
            failures++;
        }
    }
    if(failures == 0)
    {
        err = p101_error_create(false);
    }
    if(err != NULL)
    {
        env = p101_env_create(err, NULL);
    }
    if(env == NULL)
    {
        failures++;
    }
    else
    {
        p101_env_set_fd_observer(env, count_fd_event, NULL);
        p101_env_set_alloc_observer(env, count_alloc_event, NULL);
        p101_env_set_resource_observer(env, count_resource_event, NULL);
        if(!native_child_process)
        {
            test_p101_ftok(env, err);
        }
        if(!native_child_process)
        {
            test_p101_mkfifo(env, err);
        }
        if(!native_child_process)
        {
            test_p101_msgctl(env, err);
        }
        if(!native_child_process)
        {
            test_p101_msgget(env, err);
        }
        if(!native_child_process)
        {
            test_p101_msgrcv(env, err);
        }
        if(!native_child_process)
        {
            test_p101_msgsnd(env, err);
        }
        if(!native_child_process)
        {
            test_p101_pipe(env, err);
        }
        if(!native_child_process)
        {
            test_p101_semctl(env, err);
        }
        if(!native_child_process)
        {
            test_p101_semctl_arg(env, err);
        }
        if(!native_child_process)
        {
            test_p101_semget(env, err);
        }
        if(!native_child_process)
        {
            test_p101_semop(env, err);
        }
        if(!native_child_process)
        {
            test_p101_shm_open(env, err);
        }
        if(!native_child_process)
        {
            test_p101_shm_unlink(env, err);
        }
        if(!native_child_process)
        {
            test_p101_shmat(env, err);
        }
        if(!native_child_process)
        {
            test_p101_shmctl(env, err);
        }
        if(!native_child_process)
        {
            test_p101_shmdt(env, err);
        }
        if(!native_child_process)
        {
            test_p101_shmget(env, err);
        }
    }
    p101_env_destroy(env);
    p101_error_destroy(err);
    if(outcome_stream != NULL && fclose(outcome_stream) != 0)
    {
        fprintf(stderr, "FAIL: cannot close wrapper outcome receipt\n");
        failures++;
    }
    if(native_child_process)
    {
        status = native_child_status;
        if(status == EXIT_SUCCESS && failures != 0)
        {
            status = EXIT_FAILURE;
        }
    }
    else
    {
        status = failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    return status;
}
