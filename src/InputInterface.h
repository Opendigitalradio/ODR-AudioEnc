/* ------------------------------------------------------------------
 * Copyright (C) 2017 Matthias P. Braendli
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
/*! \section Input intefrace definition
 *
 * This describes the interface all inputs must implement.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>

class InputInterface {
    public:
        /*! Open the input interface. In case of failure, throws a
         * runtime_error.
         */
        virtual void prepare(void) = 0;
};
