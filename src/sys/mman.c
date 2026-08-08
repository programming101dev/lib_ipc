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

#include "p101_ipc/sys/p101_mman.h"
#include <p101_env/wrapper.h>

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

#include <string.h>
#include <sys/mman.h>

static bool posix_shared_memory_name_is_invalid(const char *name);

static bool posix_shared_memory_name_is_invalid(const char *name)
{
    bool invalid;

#ifdef SHM_ANON
    /*
     * On FreeBSD SHM_ANON is a sentinel pointer value, not a readable string,
     * so it has to be recognised before anything dereferences name. An
     * anonymous mapping has no name to validate: pass it straight through.
     */
    if(name == SHM_ANON)
    {
        invalid = false;
    }
    else
#endif
    {
        /*
         * Deliberately strict: a leading '/' is required, the name may not be
         * '/' alone, and no interior '/' is accepted.
         */
        invalid = (name == NULL || name[0] != '/' || name[1] == '\0') != 0;

        if(!invalid && strchr(name + 1, '/') != NULL)
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
