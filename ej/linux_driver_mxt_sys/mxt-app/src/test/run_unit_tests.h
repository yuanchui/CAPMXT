//------------------------------------------------------------------------------
/// \file   run_tests.h
/// \brief  Test suite for mxt-app.
/// \author Steven Swann
//------------------------------------------------------------------------------
// Copyright 2016 Atmel Corporation. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
//    2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY ATMEL ''AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL ATMEL OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
// OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//------------------------------------------------------------------------------

#include "mxt-app/mxt_app.h"

#define assert_float_equal(x,y) assert_true(abs(x - y) < 0.00001)

/* initialisation functions */
int init_mxt_device_struct(struct mxt_device **mxt);
int init_t37_ctx_struct(struct mxt_device *mxt, struct t37_ctx **f_p);
int init_mxt_touchscreen_info_struct(struct mxt_device *mxt,
    struct mxt_touchscreen_info **mxt_ts_p);

/* test functions */
void mxt_convert_hex_test(void **state);
void validate_sensor_variant_options_test(void **state);
void get_xyline_data_test(void **state);
void sensor_variant_algorithm_test(void **state);
void calculate_poly_test(void **state);
void check_line_test(void **state);
void polyfit_test(void **state);
