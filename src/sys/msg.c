/*
 * Copyright 2026 D'Arcy Smith.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "p101_ipc/sys/p101_msg.h"
#include <p101_env/resource_classes.h>
#include <p101_env/wrapper.h>

int p101_msgctl(const struct p101_env *env, struct p101_error *err, int msqid, int cmd, struct msqid_ds *buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    if(msqid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    errno   = 0;
    ret_val = msgctl(msqid, cmd, buf);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if(cmd == IPC_RMID)
    {
        P101_TRACK_INTEGER_RESOURCE_RELEASE(env, P101_RESOURCE_CLASS_SYSV_MESSAGE_QUEUE, msqid, NULL);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_msgget(const struct p101_env *env, struct p101_error *err, key_t key, int msgflg)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = msgget(key, msgflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if((msgflg & IPC_CREAT) != 0 && (msgflg & IPC_EXCL) != 0)
    {
        P101_TRACK_INTEGER_RESOURCE_ACQUIRE(env, P101_RESOURCE_CLASS_SYSV_MESSAGE_QUEUE, ret_val, 0U, "created-exclusive");
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

ssize_t p101_msgrcv(const struct p101_env *env, struct p101_error *err, int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
    ssize_t ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    if(msqid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    errno   = 0;
    ret_val = msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_msgsnd(const struct p101_env *env, struct p101_error *err, int msqid, const void *msgp, size_t msgsz, int msgflg)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    if(msqid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    errno   = 0;
    ret_val = msgsnd(msqid, msgp, msgsz, msgflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}
