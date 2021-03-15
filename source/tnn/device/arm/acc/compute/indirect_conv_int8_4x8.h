// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#ifndef TNN_CONV_INT8_4X8_H
#define TNN_CONV_INT8_4X8_H
#include "tnn/utils/naive_compute.h"

namespace TNN_NS {
void IndirectConvInt8Unit4x8(long mr, long nr, long input_channel, long kernel_size, const int32_t * indirect,
                             const void* weight, int8_t* output, long channel_stride, const float* scales, long relu,
                             const int8_t* add_input, const float* add_scale, const int8_t* zero, const int8_t* real_input);
}

#endif  // TNN_CONV_INT8_4X8_H