#include "p101_ipc/ipc.h"
#include <p101_env/wrapper.h>

int p101_msgctl(const struct p101_env *env, struct p101_error *err, int msqid, int cmd, struct msqid_ds *buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = msgctl(msqid, cmd, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if(cmd == IPC_RMID)
    {
        P101_TRACK_INTEGER_RESOURCE_RELEASE(env, "sysv-message-queue", msqid, NULL);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

int p101_msgget(const struct p101_env *env, struct p101_error *err, key_t key, int msgflg)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = msgget(key, msgflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if((msgflg & IPC_CREAT) != 0 && (msgflg & IPC_EXCL) != 0)
    {
        P101_TRACK_INTEGER_RESOURCE_ACQUIRE(env, "sysv-message-queue", ret_val, 0U, "created-exclusive");
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

ssize_t p101_msgrcv(const struct p101_env *env, struct p101_error *err, int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
    ssize_t ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}

int p101_msgsnd(const struct p101_env *env, struct p101_error *err, int msqid, const void *msgp, size_t msgsz, int msgflg)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, -1);
    errno   = 0;
    ret_val = msgsnd(msqid, msgp, msgsz, msgflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_TRACE_EXIT(env);
    return ret_val;
}
