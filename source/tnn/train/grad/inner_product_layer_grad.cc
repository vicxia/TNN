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

// author: sanerzheng@tencent.com

#include "tnn/device/arm/arm_util.h"
#include "tnn/train/grad/layer_grad.h"
#include "tnn/train/grad/utils.h"
#include "tnn/train/operations/op_builder.h"

namespace TNN_NS {
namespace train {
DECLARE_LAYER_GRAD(InnerProduct, LAYER_INNER_PRODUCT);
Status InnerProductLayerGrad::OnGrad(const BaseLayer *layer, TrainContext &context) {
    auto inputs  = layer->input_blobs_;
    auto outputs = layer->output_blobs_;
    if (inputs.size() != 1 || outputs.size() != 1) {
        return Status(TNN_TRAIN_ERROR, "input size or output size not match in InnerProductLayerGrad");
    }
    auto input0_desc      = inputs[0]->GetBlobDesc();
    auto output_desc      = outputs[0]->GetBlobDesc();
    auto input0_data_type = input0_desc.data_type;
    auto output_data_type = output_desc.data_type;
    auto input0_dims      = input0_desc.dims;
    auto output_dims      = output_desc.dims;
    if ((output_data_type != DATA_TYPE_BFP16 && output_data_type != DATA_TYPE_FLOAT) ||
        input0_data_type != output_data_type) {
        return Status(TNN_TRAIN_ERROR, "output datatype not match in InnerProductLayerGrad");
    }
    auto layer_param = dynamic_cast<InnerProductLayerParam *>(layer->param_);
    auto resource    = dynamic_cast<InnerProductLayerResource *>(layer->resource_);
    if (layer_param == nullptr || resource == nullptr)
        return Status(TNN_TRAIN_ERROR, "InnerProductLayerGrad param or resource missing");
    auto iter_output_grad = context.backward_grads_blob.find(outputs[0]);
    if (iter_output_grad == context.backward_grads_blob.end()) {
        return Status(TNN_TRAIN_ERROR, "InnerProductLayerGrad output grad not found");
    }
    auto input_batch      = input0_dims[0];
    auto input_count      = DimsVectorUtils::Count(input0_dims, 1);
    auto output_count     = layer_param->num_output;
    auto weight_data_type = resource->weight_handle.GetDataType();
    if (weight_data_type != DATA_TYPE_FLOAT) {
        return Status(TNN_TRAIN_ERROR, "InnerProductLayerGrad resource don't support ");
    }
    // converter bugs don't set weight_handle dims
    // auto weight_dims = resource->weight_handle.GetBufferDims();
    DimsVector weight_dims = {output_count, input_count};
    if (output_count * input_count * DataTypeUtils::GetBytesSize(weight_data_type) !=
        resource->weight_handle.GetBytesSize()) {
        return Status(TNN_TRAIN_ERROR, "InnerProductLayerGrad weight dims error");
    }

    std::shared_ptr<RawBuffer> input_grad = std::make_shared<RawBuffer>(
        DimsVectorUtils::Count(input0_dims) * DataTypeUtils::GetBytesSize(input0_data_type), input0_dims);
    std::shared_ptr<RawBuffer> output_grad = iter_output_grad->second;
    std::shared_ptr<RawBuffer> weight_grad =
        std::make_shared<RawBuffer>(resource->weight_handle.GetBytesSize(), weight_dims);
    std::shared_ptr<RawBuffer> bias_grad;

    void *input_ptr =
        static_cast<void *>(static_cast<char *>(inputs[0]->GetHandle().base) + inputs[0]->GetHandle().bytes_offset);
    void *output_ptr =
        static_cast<void *>(static_cast<char *>(outputs[0]->GetHandle().base) + outputs[0]->GetHandle().bytes_offset);
    void *output_grad_ptr = output_grad->force_to<void *>();
    void *weight_ptr      = resource->weight_handle.force_to<void *>();
    // TODO: direct compute on nc4hw4 using batch float compute
    RawBuffer input_buffer;
    ConvertToNCHW(input_ptr, input_buffer, input0_desc);

    RawBuffer output_buffer;
    ConvertToNCHW(output_ptr, output_buffer, output_desc);

    RawBuffer output_grad_buffer;
    ConvertToNCHW(output_grad_ptr, output_grad_buffer, output_grad.get());

    if (layer_param->has_bias) {
        DimsVector bias_dims = {output_count};
        bias_grad            = std::make_shared<RawBuffer>(DimsVectorUtils::Count(bias_dims) *
                                                    DataTypeUtils::GetBytesSize(resource->bias_handle.GetDataType()),
                                                bias_dims);
    }
    // TODO: 如下改造成模板可以处理bft16和float
    // already init with 0.0; see all params as 2 dims;
    // weigt: output_count * input_count
    // input: input_batch * input_count
    // output: input_batch * output_count
    // bias: output_count
    // weight_grad[j, k] = E(0<=i<input_batch) output_grad[i, j] * input[i, k]
    // bias_grad[j] = E(j<=0<output_count) output_grad[i, j]
    // input_grad[i,k] = E(0<=j<output_count) output_grad[i, j] * weight[j, k]
    if (input0_data_type == DATA_TYPE_FLOAT) {
        float *weight_grad_ptr_float = weight_grad->force_to<float *>();
        float *output_grad_ptr_float = static_cast<float *>(output_grad_ptr);
        float *bias_grad_ptr_float   = layer_param->has_bias ? bias_grad->force_to<float *>() : nullptr;
        float *input_grad_ptr_float  = input_grad->force_to<float *>();
        float *input_ptr_float       = static_cast<float *>(input_ptr);
        float *weight_ptr_float      = static_cast<float *>(weight_ptr);
        for (int j = 0; j < output_count; ++j) {
            for (int k = 0; k < input_count; ++k) {
                for (int i = 0; i < input_batch; ++i) {
                    weight_grad_ptr_float[j * input_count + k] +=
                        output_grad_ptr_float[i * output_count + j] * input_ptr_float[i * input_count + k];
                    input_grad_ptr_float[i * input_count + k] +=
                        output_grad_ptr_float[i * output_count + j] * weight_ptr_float[j * input_count + k];
                    if (layer_param->has_bias && k == 0) {
                        bias_grad_ptr_float[j] += output_grad_ptr_float[i * output_count + j];
                    }
                }
            }
        }
    } else /* TODO bfp16*/ {
        return Status(TNN_TRAIN_ERROR, "InnerProductLayerGrad don't support bft16 for now");
    }

    // input grad convert back to nc4hw4; weight in resource must be NCHW, don't need to transform
    // PrintFloatBuffer(input_grad.get(), "input");
    ConvertToNC4HW4(input_grad, input0_desc);
    input_grad->SetDataType(input0_data_type);
    input_grad->SetDataFormat(input0_desc.data_format);
    UpdateGradValue(inputs[0], input_grad, context);
    // PrintFloatBuffer(weight_grad.get(), "weight");
    weight_grad->SetDataType(weight_data_type);
    weight_grad->SetDataFormat(resource->weight_handle.GetDataFormat());
    UpdateGradValue(&resource->weight_handle, weight_grad, context);
    if (layer_param->has_bias) {
        bias_grad->SetDataType(resource->bias_handle.GetDataType());
        bias_grad->SetDataFormat(resource->bias_handle.GetDataFormat());
        // PrintFloatBuffer(bias_grad.get(), "bias");
        UpdateGradValue(&resource->bias_handle, bias_grad, context);
    }
    return Status(TNN_OK);
}
REGISTER_LAYER_GRAD(InnerProduct, LAYER_INNER_PRODUCT);

} // namespace train
} // namespace TNN_NS