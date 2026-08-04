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

int p101_pipe(const struct p101_env *env, struct p101_error *err, int fildes[2])
{
    int p101_single_result_;
    int ret_val;
    int fault;

    P101_TRACE(env);
    fault = p101_env_check_fault(env, "pipe");

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
