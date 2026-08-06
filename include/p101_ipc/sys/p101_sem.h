#ifndef LIBP101_IPC_SYS_P101_SEM_H
#define LIBP101_IPC_SYS_P101_SEM_H

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

#ifndef LIBP101_IPC_SHARED_DECLARATIONS
    #define LIBP101_IPC_SHARED_DECLARATIONS
    #include <p101_env/env.h>
    #include <p101_error/attributes.h>
    #include <sys/mman.h>
    #include <sys/msg.h>
    #include <sys/sem.h>
    #include <sys/shm.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>

union p101_semun
{
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
};
#endif    // LIBP101_IPC_SHARED_DECLARATIONS

#ifdef __cplusplus
extern "C"
{
#endif

    int p101_semctl(const struct p101_env *env, struct p101_error *err, int semid, int semnum, int cmd);
    int p101_semctl_arg(const struct p101_env *env, struct p101_error *err, int semid, int semnum, int cmd, union p101_semun arg);
    int p101_semget(const struct p101_env *env, struct p101_error *err, key_t key, int nsems, int semflg);
    int p101_semop(const struct p101_env *env, struct p101_error *err, int semid, struct sembuf *sops, size_t nsops);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_IPC_SYS_P101_SEM_H
