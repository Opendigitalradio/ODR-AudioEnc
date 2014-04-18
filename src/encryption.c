/*
 * Copyright (C) 2014 Matthias P. Braendli
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */

#include "encryption.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

int readkey(const char* keyfile, char* key)
{
    int fd = open(keyfile, O_RDONLY);
    if (fd < 0)
        return fd;
    int ret = read(fd, key, CURVE_KEYLEN);
    if (ret < 0)
        return ret;
    close(fd);

    /* It needs to be zero-terminated */
    key[CURVE_KEYLEN] = '\0';

    return 0;
}

