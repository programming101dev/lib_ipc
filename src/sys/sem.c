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

#include "p101_ipc/sys/p101_sem.h"
#include <p101_env/resource_classes.h>
#include <p101_env/wrapper.h>

static bool semctl_uses_arg(int cmd);

static bool semctl_uses_arg(int cmd)
{
    bool uses_argument;

    switch(cmd)
    {
        case GETALL:
        case SETALL:
        case SETVAL:
        case IPC_STAT:
        case IPC_SET:
            uses_argument = true;
            break;
        default:
            uses_argument = false;
            break;
    }

    return uses_argument;
}

int p101_semctl(const struct p101_env *env, struct p101_error *err, int semid, int semnum, int cmd)
{
    int  ret_val;
    bool uses_argument;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);

    if(semid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }

    uses_argument = semctl_uses_arg(cmd);
    if(uses_argument)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }

    errno   = 0;
    ret_val = semctl(semid, semnum, cmd);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if(cmd == IPC_RMID)
    {
        P101_TRACK_INTEGER_RESOURCE_RELEASE(env, P101_RESOURCE_CLASS_SYSV_SEMAPHORE_SET, semid, NULL);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_semctl_arg(const struct p101_env *env, struct p101_error *err, int semid, int semnum, int cmd, union p101_semun arg)
{
    int  ret_val;
    bool uses_argument;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);

    if(semid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }

    uses_argument = semctl_uses_arg(cmd);
    if(!uses_argument)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }

    errno = 0;
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wclass-varargs"
#endif
    ret_val = semctl(semid, semnum, cmd, arg);
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_semget(const struct p101_env *env, struct p101_error *err, key_t key, int nsems, int semflg)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    if(nsems < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    errno   = 0;
    ret_val = semget(key, nsems, semflg);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else if((semflg & IPC_CREAT) != 0 && (semflg & IPC_EXCL) != 0)
    {
        P101_TRACK_INTEGER_RESOURCE_ACQUIRE(env, P101_RESOURCE_CLASS_SYSV_SEMAPHORE_SET, ret_val, 0U, "created-exclusive");
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_semop(const struct p101_env *env, struct p101_error *err, int semid, struct sembuf *sops, size_t nsops)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    if(semid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    errno   = 0;
    ret_val = semop(semid, sops, nsops);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}
