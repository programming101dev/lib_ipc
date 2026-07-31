#include "p101_ipc/ipc.h"
#include <p101_env/wrapper.h>

void *p101_shmat(const struct p101_env *env, struct p101_error *err, int shmid, const void *shmaddr, int shmflg)
{
    void *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, (void *)-1);    // NOLINT(performance-no-int-to-ptr)
    errno   = 0;
    ret_val = shmat(shmid, shmaddr, shmflg);

    if(ret_val == (void *)-1)    // NOLINT(performance-no-int-to-ptr)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        P101_TRACK_POINTER_RESOURCE_ACQUIRE(env, "sysv-shared-memory-attachment", ret_val, 0U, NULL);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

int p101_shmctl(const struct p101_env *env, struct p101_error *err, int shmid, int cmd, struct shmid_ds *buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = shmctl(shmid, cmd, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if(cmd == IPC_RMID)
    {
        P101_TRACK_INTEGER_RESOURCE_RELEASE(env, "sysv-shared-memory", shmid, NULL);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

int p101_shmdt(const struct p101_env *env, struct p101_error *err, const void *shmaddr)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = shmdt(shmaddr);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        P101_TRACK_POINTER_RESOURCE_RELEASE(env, "sysv-shared-memory-attachment", shmaddr, NULL);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

int p101_shmget(const struct p101_env *env, struct p101_error *err, key_t key, size_t size, int shmflg)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = shmget(key, size, shmflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if((shmflg & IPC_CREAT) != 0 && (shmflg & IPC_EXCL) != 0)
    {
        P101_TRACK_INTEGER_RESOURCE_ACQUIRE(env, "sysv-shared-memory", ret_val, size, "created-exclusive");
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}
