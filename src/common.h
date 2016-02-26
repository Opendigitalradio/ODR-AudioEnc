/* ------------------------------------------------------------------
 * Copyright (C) 2016 Matthias P. Braendli
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

#ifndef __COMMON_H_
#define __COMMON_H_

// 16 bits per sample is fine for now
#define BYTES_PER_SAMPLE 2

// How many samples we insert into the queue each call
#define NUM_SAMPLES_PER_CALL 10 // 10 samples @ 32kHz = 3.125ms

#endif // __COMMON_H_

