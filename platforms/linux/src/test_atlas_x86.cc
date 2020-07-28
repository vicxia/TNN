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

/*
 * This is a demo for the huawei atlas devices.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <fstream>
#include <memory>
#include <string>

#include "test_common.h"
#include "tnn/core/instance.h"
#include "tnn/core/tnn.h"
#include "tnn/utils/mat_utils.h"

using namespace TNN_NS;
TNN net_;

int main(int argc, char* argv[]) {
    printf("Run Atlas test ...\n");
    if (argc == 1) {
        printf("./AtlasTest <om_file> <input_filename>\n");
        return 0;
    } else {
        if (argc < 3) {
            printf("invalid args\n");
            return 0;
        }
        for (int i = 1; i < argc; i++) {
            printf("arg%d: %s\n", i - 1, argv[i]);
        }
    }

    NetworkConfig network_config;
    network_config.network_type = NETWORK_TYPE_ATLAS;
    network_config.device_type  = DEVICE_ATLAS;
    network_config.device_id    = 0;

    struct timezone zone;
    struct timeval time1;
    struct timeval time2;
    struct timeval time_begin, time_end;
    float delta = 0;

    Status error;
    int ret;
    gettimeofday(&time1, NULL);
    ModelConfig config;
    config.model_type = MODEL_TYPE_ATLAS;
    config.params.push_back(argv[1]);

    error = net_.Init(config);  // init the net
    if (TNN_OK != error) {
        printf("TNN init failed\n");
        return -1;
    }
    gettimeofday(&time2, NULL);
    delta = (time2.tv_sec - time1.tv_sec) * 1000.0 + (time2.tv_usec - time1.tv_usec) / 1000.0;
    printf("init tnn time cost: %g ms\n", delta);

    gettimeofday(&time1, NULL);
    auto instance_ = net_.CreateInst(network_config, error);
    if (CheckResult("create instance", error) != true) {
        printf("error info: %s\n", error.description().c_str());
        return -1;
    }
    gettimeofday(&time2, NULL);
    delta = (time2.tv_sec - time1.tv_sec) * 1000.0 + (time2.tv_usec - time1.tv_usec) / 1000.0;
    printf("tnn create instance time cost: %g ms\n", delta);

    // Get command queue
    void* command_queue;
    instance_->GetCommandQueue(&command_queue);

    // Reshape
    BlobMap input_blobs_temp;
    error = instance_->GetAllInputBlobs(input_blobs_temp);
    InputShapesMap input_shapemap;
    input_shapemap[input_blobs_temp.begin()->first]    = input_blobs_temp.begin()->second->GetBlobDesc().dims;
    input_shapemap[input_blobs_temp.begin()->first][0] = 1;
    error                                              = instance_->Reshape(input_shapemap);

    // Get input/output blobs
    BlobMap input_blobs, output_blobs;
    error       = instance_->GetAllInputBlobs(input_blobs);
    Blob* input = input_blobs.begin()->second;
    for (auto it = input_blobs.begin(); it != input_blobs.end(); ++it) {
        printf("input(%s) data shape [ %d %d %d %d ]\n", it->first.c_str(), it->second->GetBlobDesc().dims[0],
               it->second->GetBlobDesc().dims[1], it->second->GetBlobDesc().dims[2], it->second->GetBlobDesc().dims[3]);
    }
    instance_->GetAllOutputBlobs(output_blobs);

    for (auto it = output_blobs.begin(); it != output_blobs.end(); ++it) {
        printf("output(%s) data shape [ %d %d %d %d ]\n", it->first.c_str(), it->second->GetBlobDesc().dims[0],
               it->second->GetBlobDesc().dims[1], it->second->GetBlobDesc().dims[2], it->second->GetBlobDesc().dims[3]);
    }

    // load input
    //float* input_data_ptr = nullptr;
    unsigned char* input_data_ptr = nullptr;
    auto input_dims               = input->GetBlobDesc().dims;
    auto input_format             = input->GetBlobDesc().data_format;
    if (DATA_FORMAT_NCHW == input_format) {
        printf("input format is NCHW\n");
        //ret = ReadFromTxtToBatch(input_data_ptr, argv[2], input_dims, false);
        ret = ReadFromNchwtoNhwcU8FromTxt(input_data_ptr, argv[2], input_dims);
        //ret = ReadFromTxtToNHWCU8_Batch(input_data_ptr, argv[2], input_dims);
    } else if (DATA_FORMAT_NHWC == input_format) {
        printf("input format is NHWC\n");
        //ret = ReadFromTxtToBatch(input_data_ptr, argv[2], {input_dims[0], input_dims[3], input_dims[1], input_dims[2]}, false);
        //ret = ReadFromNchwtoNhwcU8FromTxt(input_data_ptr, argv[2], {input_dims[0], input_dims[3], input_dims[1], input_dims[2]});
        ret = ReadFromNchwtoNhwcU8FromTxt(input_data_ptr, argv[2], input_dims);
        //ret = ReadFromTxtToNHWCU8_Batch(input_data_ptr, argv[2], {input_dims[0], input_dims[3], input_dims[1], input_dims[2]});
    } else {
        printf("invalid model input format\n");
        return -1;
    }
    if (CheckResult("load input data", ret) != true)
        return -1;
    int index = 10;
    printf("input_data_ptr[%d] = %f\n", index, (float)input_data_ptr[index]);

    // BlobConvert
    std::shared_ptr<BlobConverter> input_cvt;
    std::map<std::string, std::shared_ptr<BlobConverter>> output_cvt_map;
    input_cvt.reset(new BlobConverter(input));
    for (auto item : output_blobs) {
        output_cvt_map[item.first].reset(new BlobConverter(item.second));
    }

    Status tnn_ret;
    // copy input data into atlas
    //Mat input_mat(DEVICE_NAIVE, NCHW_FLOAT, input_dims, input_data_ptr);
    //Mat input_mat(DEVICE_NAIVE, N8UC3, {input_dims[0], input_dims[3], input_dims[1], input_dims[2]}, input_data_ptr);
    Mat input_mat(DEVICE_NAIVE, N8UC3, input_dims, input_data_ptr);
    MatConvertParam input_param;
    input_param.scale = {0.00392156862745, 0.00392156862745, 0.00392156862745, 0.00392156862745};
    input_param.reverse_channel = true;
    tnn_ret = input_cvt->ConvertFromMat(input_mat, input_param, command_queue);
    if (tnn_ret != TNN_OK) {
        printf("ConvertFromMat falied (%s)\n", tnn_ret.description().c_str());
        return -1;
    }

    // Forward on atlas device.
    // Also check the running time.
    srand(102);
    std::vector<float> costs;
    const int loopcnt = 10;
    gettimeofday(&time1, &zone);
    for (int i = 0; i < loopcnt; ++i) {
        gettimeofday(&time_begin, NULL);
        tnn_ret = instance_->Forward();
        if (tnn_ret != TNN_OK) {
            printf("instance Forward falied (%s)\n", tnn_ret.description().c_str());
        }
        gettimeofday(&time_end, NULL);
        costs.push_back((time_end.tv_sec - time_begin.tv_sec) * 1000.0 +
                        (time_end.tv_usec - time_begin.tv_usec) / 1000.0);
    }
    gettimeofday(&time2, &zone);
    delta = (time2.tv_sec - time1.tv_sec) * 1000.0 + (time2.tv_usec - time1.tv_usec) / 1000.0;
    printf("time cost: %g ms\n", delta / (float)loopcnt);
    DisplayStats("", costs);

    // copy data from atlas buffer
    // then dump to files
    for (auto output : output_blobs) {
        Mat output_mat(DEVICE_NAIVE, NCHW_FLOAT, output.second->GetBlobDesc().dims);
        MatConvertParam output_param;
        tnn_ret = output_cvt_map[output.first]->ConvertToMat(output_mat, output_param, command_queue);
        if (tnn_ret != TNN_OK) {
            printf("ConvertToMat falied (%s)\n", tnn_ret.description().c_str());
            continue;
        }

        std::string name_temp = ReplaceString(output.second->GetBlobDesc().name);
        DumpDataToTxt((float*)output_mat.GetData(), output_mat.GetDims(), "dump_" + name_temp + ".txt");
        // DumpDataToBin(output_mat.GetData(), output_mat.GetDims(), "../dump_data/dump_" +
        // output.second->GetBlobDesc().name + ".bin");
    }

    if (input_data_ptr != nullptr)
        free(input_data_ptr);

    instance_.reset();
    net_.DeInit();
    return 0;
}
