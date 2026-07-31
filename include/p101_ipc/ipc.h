#ifndef LIBP101_IPC_IPC_H
#define LIBP101_IPC_IPC_H

/*
 * Copyright 2026 D'Arcy Smith.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 */

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

#ifdef __cplusplus
extern "C"
{
#endif

    key_t   p101_ftok(const struct p101_env *env, struct p101_error *err, const char *path, int id);
    int     p101_mkfifo(const struct p101_env *env, struct p101_error *err, const char *path, mode_t mode);
    int     p101_msgctl(const struct p101_env *env, struct p101_error *err, int msqid, int cmd, struct msqid_ds *buf);
    int     p101_msgget(const struct p101_env *env, struct p101_error *err, key_t key, int msgflg);
    ssize_t p101_msgrcv(const struct p101_env *env, struct p101_error *err, int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
    int     p101_msgsnd(const struct p101_env *env, struct p101_error *err, int msqid, const void *msgp, size_t msgsz, int msgflg);
    int     p101_pipe(const struct p101_env *env, struct p101_error *err, int fildes[2]);
    int     p101_semctl(const struct p101_env *env, struct p101_error *err, int semid, int semnum, int cmd);
    int     p101_semctl_arg(const struct p101_env *env, struct p101_error *err, int semid, int semnum, int cmd, union p101_semun arg);
    int     p101_semget(const struct p101_env *env, struct p101_error *err, key_t key, int nsems, int semflg);
    int     p101_semop(const struct p101_env *env, struct p101_error *err, int semid, struct sembuf *sops, size_t nsops);
    int     p101_shm_open(const struct p101_env *env, struct p101_error *err, const char *name, int oflag, mode_t mode);
    int     p101_shm_unlink(const struct p101_env *env, struct p101_error *err, const char *name);
    void   *p101_shmat(const struct p101_env *env, struct p101_error *err, int shmid, const void *shmaddr, int shmflg) P101_ATTR_WARN_UNUSED_RESULT;
    int     p101_shmctl(const struct p101_env *env, struct p101_error *err, int shmid, int cmd, struct shmid_ds *buf);
    int     p101_shmdt(const struct p101_env *env, struct p101_error *err, const void *shmaddr);
    int     p101_shmget(const struct p101_env *env, struct p101_error *err, key_t key, size_t size, int shmflg);

#ifdef __cplusplus
}
#endif

#endif    // LIBP101_IPC_IPC_H
