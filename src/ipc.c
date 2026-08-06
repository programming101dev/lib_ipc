/*
 * Copyright 2021-2024 D'Arcy Smith.
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

#include "p101_ipc/ipc.h"
#include <p101_env/wrapper.h>

int p101_mkfifo(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = mkfifo(path, mode);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

/*
 * Copyright 2021-2024 D'Arcy Smith.
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

int p101_pipe(const struct p101_env *env, struct p101_error *err, int fildes[2])
{
    int p101_single_result_;
    int ret_val;
    int fault;

    P101_TRACE(env);
    fault = p101_env_check_fault(env, __func__);

    if(fault != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, fault);
        P101_TRACE_EXIT(env);

        p101_single_result_ = -1;
        goto p101_single_exit_;
    }

    errno   = 0;
    ret_val = pipe(fildes);    // NOLINT(android-cloexec-pipe)

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        /* Both ends are descriptors, and both have to be closed -- the
         * half-closed pipe is a favourite student bug. */
        P101_TRACK_OPEN(env, fildes[0]);
        P101_TRACK_OPEN(env, fildes[1]);
    }

    P101_TRACE_EXIT(env);

    p101_single_result_ = ret_val;
    goto p101_single_exit_;

p101_single_exit_:
    return p101_single_result_;
}

/*
 * Copyright 2021-2024 D'Arcy Smith.
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

#include <sys/mman.h>

static bool posix_shared_memory_name_is_invalid(const char *name);

static bool posix_shared_memory_name_is_invalid(const char *name)
{
    bool invalid;

    invalid = (name == NULL || name[0] != '/' || name[1] == '\0') != 0;
    for(size_t index = 1U; !invalid && name[index] != '\0'; index++)
    {
        if(name[index] == '/')
        {
            invalid = true;
        }
    }
    return invalid;
}

int p101_shm_open(const struct p101_env *env, struct p101_error *err, const char *name, int oflag, mode_t mode)
{
    int  ret_val;
    bool invalid_name;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    invalid_name = posix_shared_memory_name_is_invalid(name);
    if(invalid_name)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    errno   = 0;
    ret_val = shm_open(name, oflag, mode);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        P101_TRACK_OPEN(env, ret_val);
    }

    P101_WRAPPER_DONE(env);

    return ret_val;
}

int p101_shm_unlink(const struct p101_env *env, struct p101_error *err, const char *name)
{
    int  ret_val;
    bool invalid_name;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    invalid_name = posix_shared_memory_name_is_invalid(name);
    if(invalid_name)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    errno   = 0;
    ret_val = shm_unlink(name);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

#include <sys/ipc.h>

key_t p101_ftok(const struct p101_env *env, struct p101_error *err, const char *path, int id)
{
    key_t ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    errno   = 0;
    ret_val = ftok(path, id);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

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
        P101_TRACK_INTEGER_RESOURCE_RELEASE(env, "sysv-message-queue", msqid, NULL);
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
        P101_TRACK_INTEGER_RESOURCE_ACQUIRE(env, "sysv-message-queue", ret_val, 0U, "created-exclusive");
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
        P101_TRACK_INTEGER_RESOURCE_RELEASE(env, "sysv-semaphore-set", semid, NULL);
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
        P101_TRACK_INTEGER_RESOURCE_ACQUIRE(env, "sysv-semaphore-set", ret_val, 0U, "created-exclusive");
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

void *p101_shmat(const struct p101_env *env, struct p101_error *err, int shmid, const void *shmaddr, int shmflg)
{
    void *ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, (void *)-1);    // NOLINT(performance-no-int-to-ptr)
    if(shmid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = (void *)-1;    // NOLINT(performance-no-int-to-ptr)
        goto p101_wrapper_done_;
    }
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

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_shmctl(const struct p101_env *env, struct p101_error *err, int shmid, int cmd, struct shmid_ds *buf)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    if(shmid < 0)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
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

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_shmdt(const struct p101_env *env, struct p101_error *err, const void *shmaddr)
{
    char resource_id[P101_ENV_POINTER_RESOURCE_ID_SIZE];
    int  ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
    if(shmaddr == NULL)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        ret_val = -1;
        goto p101_wrapper_done_;
    }
    p101_env_pointer_resource_id(resource_id, sizeof(resource_id), shmaddr);
    errno   = 0;
    ret_val = shmdt(shmaddr);

    if(ret_val == -1)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    else
    {
        P101_TRACK_RESOURCE_RELEASE(env, "sysv-shared-memory-attachment", resource_id, NULL);
    }

    P101_WRAPPER_DONE(env);
    return ret_val;
}

int p101_shmget(const struct p101_env *env, struct p101_error *err, key_t key, size_t size, int shmflg)
{
    int ret_val;

    P101_TRACE(env);
    P101_WRAPPER_FAULT_RETURN(env, err, ret_val, -1);
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

    P101_WRAPPER_DONE(env);
    return ret_val;
}
