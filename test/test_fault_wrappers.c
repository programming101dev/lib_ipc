#include <errno.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_ipc/ipc.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define EXPECT(condition)                                                                                                                                                                                                                                          \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        if(!(condition))                                                                                                                                                                                                                                           \
        {                                                                                                                                                                                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                   \
            failures++;                                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                          \
    } while(0)

struct fault_state
{
    int checks;
};

static int fail_next_call(const struct p101_env *env, const char *call_name, void *user_data)
{
    struct fault_state *state;

    (void)env;
    (void)call_name;
    state = user_data;
    state->checks++;
    return EIO;
}

/* P101_TEST_CASE(p101_ftok) */
static void test_p101_ftok(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    key_t result = p101_ftok(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_mkfifo) */
static void test_p101_mkfifo(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_mkfifo(env, err, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_msgctl) */
static void test_p101_msgctl(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_msgctl(env, err, 0, 0, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_msgget) */
static void test_p101_msgget(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_msgget(env, err, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_msgrcv) */
static void test_p101_msgrcv(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    ssize_t result = p101_msgrcv(env, err, 0, NULL, 0, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_msgsnd) */
static void test_p101_msgsnd(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_msgsnd(env, err, 0, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_pipe) */
static void test_p101_pipe(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_pipe(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_semctl) */
static void test_p101_semctl(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_semctl(env, err, 0, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_semctl_arg) */
static void test_p101_semctl_arg(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_semctl_arg(env, err, 0, 0, 0, (union p101_semun){0});
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_semget) */
static void test_p101_semget(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_semget(env, err, 0, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_semop) */
static void test_p101_semop(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_semop(env, err, 0, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_shm_open) */
static void test_p101_shm_open(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_shm_open(env, err, NULL, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_shm_unlink) */
static void test_p101_shm_unlink(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_shm_unlink(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_shmat) */
static void test_p101_shmat(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    void *result = p101_shmat(env, err, 0, NULL, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_shmctl) */
static void test_p101_shmctl(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_shmctl(env, err, 0, 0, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_shmdt) */
static void test_p101_shmdt(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_shmdt(env, err, NULL);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

/* P101_TEST_CASE(p101_shmget) */
static void test_p101_shmget(struct p101_env *env, struct p101_error *err)
{
    struct fault_state state = {0};

    p101_env_set_fault_injector(env, fail_next_call, &state);
    int result = p101_shmget(env, err, 0, 0, 0);
    (void)result;
    EXPECT(state.checks == 1);
    EXPECT(p101_error_has_error(err));
    p101_error_reset(err);
    p101_env_set_fault_injector(env, NULL, NULL);
}

int main(void)
{
    struct p101_error *err;
    struct p101_env   *env;

    err = p101_error_create(false);
    if(err == NULL)
    {
        return EXIT_FAILURE;
    }
    env = p101_env_create(err, NULL);
    if(env == NULL)
    {
        p101_error_destroy(err);
        return EXIT_FAILURE;
    }
    test_p101_ftok(env, err);
    test_p101_mkfifo(env, err);
    test_p101_msgctl(env, err);
    test_p101_msgget(env, err);
    test_p101_msgrcv(env, err);
    test_p101_msgsnd(env, err);
    test_p101_pipe(env, err);
    test_p101_semctl(env, err);
    test_p101_semctl_arg(env, err);
    test_p101_semget(env, err);
    test_p101_semop(env, err);
    test_p101_shm_open(env, err);
    test_p101_shm_unlink(env, err);
    test_p101_shmat(env, err);
    test_p101_shmctl(env, err);
    test_p101_shmdt(env, err);
    test_p101_shmget(env, err);
    p101_env_destroy(env);
    p101_error_destroy(err);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
