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

#include "p101_ipc/p101_unistd.h"
#include "p101_ipc/sys/p101_ipc.h"
#include "p101_ipc/sys/p101_mman.h"
#include "p101_ipc/sys/p101_msg.h"
#include "p101_ipc/sys/p101_sem.h"
#include "p101_ipc/sys/p101_shm.h"
#include "p101_ipc/sys/p101_stat.h"
#include <p101_env/wrapper.h>
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
