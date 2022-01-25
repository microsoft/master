/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.
Portions Copyright (c) Microsoft Corporation.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tfdml/kernels/pch.h"
#include "tfdml/runtime_adapter/bcast.h"

namespace tfdml
{

using Microsoft::WRL::ComPtr;

static absl::InlinedVector<TensorShape, 2> GetCollapsedShapes(
    OpKernelContext* ctx)
{
    if (ctx->num_inputs() == 1)
    {
        return {TensorShape({ctx->input(0).NumElements()})};
    }

    absl::InlinedVector<TensorShape, 2> shapes;

    // Shape collapsing for more than 2 inputs is not implemented
    if (ctx->num_inputs() > 2)
    {
        for (uint32_t i = 0; i < ctx->num_inputs(); ++i)
        {
            shapes.push_back(ctx->input(i).shape());
        }

        return shapes;
    }

    BCast bcast_helper(
        ctx->input(0).shape().dim_sizes(),
        ctx->input(1).shape().dim_sizes());

    shapes.emplace_back(bcast_helper.x_reshape());
    shapes.emplace_back(bcast_helper.y_reshape());

    return shapes;
}

template <uint32_t max_dim_count>
class ElementWiseInitHelper : public GetBroadcastedOutputShapeHelper::InitHelper
{
  public:
    struct Attributes
        : public GetBroadcastedOutputShapeHelper::InitHelper::Attributes
    {
        explicit Attributes(OpKernelConstruction* ctx)
            : GetBroadcastedOutputShapeHelper::InitHelper::Attributes(ctx)
        {
        }
    };

    ElementWiseInitHelper(
        OpKernelContext* ctx,
        std::shared_ptr<const Attributes> attr)
        : GetBroadcastedOutputShapeHelper::InitHelper(ctx, attr)
    {
        collapsed_input_shapes_ = GetCollapsedShapes(ctx);
        collapsed_output_shape_ =
            BroadcastTensorShapes(collapsed_input_shapes_);
            
        OP_REQUIRES(
            ctx,
            collapsed_output_shape_.dims() <= max_dim_count,
            errors::InvalidArgument(
                "DML doesn't support more than ",
                max_dim_count,
                " dimensions for this operator, but ",
                collapsed_output_shape_.dims(),
                " were provided."));
    }

    absl::Span<const TensorShape> GetCollapsedInputShapes() const
    {
        return collapsed_input_shapes_;
    }

    const TensorShape& GetCollapsedOutputShape() const
    {
        return collapsed_output_shape_;
    }

  private:
    absl::InlinedVector<TensorShape, 2> collapsed_input_shapes_;
    TensorShape collapsed_output_shape_;
};

static DmlKernelTensors CreateKernelTensors(
    DmlKernelConstruction* ctx,
    absl::Span<const TensorShape> input_shapes,
    const TensorShape& output_shape)
{
    const auto tensor_layout =
        GetDmlTensorLayout(FORMAT_NCHW, output_shape.dims());

    DmlKernelTensors tensors;

    for (uint32_t i = 0; i < ctx->GetInputCount(); ++i)
    {
        DmlTensorInfo input;
        input.kernel_index = i;
        input.desc = DmlTensorDesc::Create(
            ctx->GetInputDataType(i),
            output_shape,
            input_shapes[i],
            tensor_layout);

        tensors.inputs.push_back(std::move(input));
    }

    DmlTensorInfo output;
    output.kernel_index = 0;
    output.desc = DmlTensorDesc::Create(
        ctx->GetOutputDataType(0),
        output_shape,
        output_shape,
        tensor_layout);

    tensors.outputs = {output};

    return tensors;
}

template <DML_OPERATOR_TYPE op_type, typename DML_OPERATOR_SPECIFIC_DESC>
class DmlBinaryKernel : public DmlKernel
{
  public:
    using InitHelper = ElementWiseInitHelper<kBinaryCwiseOpMaxDimCount>;

    explicit DmlBinaryKernel(
        DmlKernelConstruction* ctx,
        const InitHelper* init_helper)
    {
        CHECK(ctx->GetInputCount() == 2);
        CHECK(ctx->GetOutputCount() == 1);

        // Currently, 64-bit integers in DML are emulated using 32-bit integers
        // using striding to emulate a larger type. Because we can't guarantee
        // that our output tensor's memory is zero'd, we need to do so manually
        // prior to running running gather.
        if (Is64BitIntegerType(ctx->GetOutputDataType(0)))
        {
            zero_outputs_ = true;
        }

        auto input_shapes = init_helper->GetCollapsedInputShapes();
        const TensorShape& output_shape =
            init_helper->GetCollapsedOutputShape();

        DmlKernelTensors tensors =
            CreateKernelTensors(ctx, input_shapes, output_shape);
        auto inputs = GetDmlTensorDescs(tensors.inputs);
        auto outputs = GetDmlTensorDescs(tensors.outputs);

        DML_OPERATOR_SPECIFIC_DESC op_specific_desc = {
            &inputs[0],
            &inputs[1],
            outputs.data(),
        };

        DML_OPERATOR_DESC op_desc = {op_type, &op_specific_desc};
        Initialize(ctx, std::move(tensors), op_desc);
    }

    StatusOr<DmlGpuEvent> Compute(DmlKernelContext* ctx) const
    {

        Tensor& output = ctx->GetOutputTensor(0);

        if (zero_outputs_)
        {
            ctx->GetDmlDeviceContext()->ZeroBuffer(
                ctx->GetDmlDeviceContext()->GetBufferForTensor(output));
        }

        return DmlKernel::Compute(ctx);
    }

  private:
    bool zero_outputs_ = false;
};

template <typename Functor, uint32_t max_dim_count>
class DmlBinaryWithZeroKernel : public DmlKernel
{
  public:
    using InitHelper = ElementWiseInitHelper<max_dim_count>;

    explicit DmlBinaryWithZeroKernel(
        DmlKernelConstruction* ctx,
        const InitHelper* init_helper)
    {
        CHECK(ctx->GetInputCount() == 2);
        CHECK(ctx->GetOutputCount() == 1);

        // Currently, 64-bit integers in DML are emulated using 32-bit integers
        // using striding to emulate a larger type. Because we can't guarantee
        // that our output tensor's memory is zero'd, we need to do so manually
        // prior to running running gather.
        if (Is64BitIntegerType(ctx->GetOutputDataType(0)))
        {
            zero_outputs_ = true;
        }

        auto input_shapes = init_helper->GetCollapsedInputShapes();
        const TensorShape& output_shape =
            init_helper->GetCollapsedOutputShape();

        DmlKernelTensors tensors =
            CreateKernelTensors(ctx, input_shapes, output_shape);
        auto inputs = GetDmlTensorDescs(tensors.inputs);
        auto outputs = GetDmlTensorDescs(tensors.outputs);

        auto scope = dml::Graph(ctx->GetDmlDevice());
        auto x = dml::InputTensor(scope, 0, inputs[0]);
        auto y = dml::InputTensor(scope, 1, inputs[1]);
        auto zero = dml::ZeroTensor(
            scope,
            x.GetOutputDesc().dataType,
            x.GetOutputDesc().sizes);

        Functor f;
        auto result = f(zero, x, y);

        ComPtr<IDMLCompiledOperator> compiled_op =
            scope.Compile(DML_EXECUTION_FLAG_NONE, {result});

        Initialize(ctx, std::move(tensors), compiled_op.Get());
    }

    StatusOr<DmlGpuEvent> Compute(DmlKernelContext* ctx) const
    {
        Tensor& output = ctx->GetOutputTensor(0);
        if (zero_outputs_)
        {
            ctx->GetDmlDeviceContext()->ZeroBuffer(
                ctx->GetDmlDeviceContext()->GetBufferForTensor(output));
        }
        return DmlKernel::Compute(ctx);
    }

  private:
    bool zero_outputs_ = false;
};

template <typename ExpressionFunctor, uint32_t max_dim_count>
class DmlCompositeBinaryKernel : public DmlKernel
{
  public:
    using InitHelper = ElementWiseInitHelper<max_dim_count>;

    explicit DmlCompositeBinaryKernel(
        DmlKernelConstruction* ctx,
        const InitHelper* init_helper)
    {
        CHECK(ctx->GetInputCount() == 2);
        CHECK(ctx->GetOutputCount() == 1);

        // Currently, 64-bit integers in DML are emulated using 32-bit integers
        // using striding to emulate a larger type. Because we can't guarantee
        // that our output tensor's memory is zero'd, we need to do so manually
        // prior to running running gather.
        if (Is64BitIntegerType(ctx->GetOutputDataType(0)))
        {
            zero_outputs_ = true;
        }

        auto input_shapes = init_helper->GetCollapsedInputShapes();
        const TensorShape& output_shape =
            init_helper->GetCollapsedOutputShape();

        DmlKernelTensors tensors =
            CreateKernelTensors(ctx, input_shapes, output_shape);
        auto inputs = GetDmlTensorDescs(tensors.inputs);
        auto outputs = GetDmlTensorDescs(tensors.outputs);

        auto scope = dml::Graph(ctx->GetDmlDevice());
        auto x = dml::InputTensor(scope, 0, inputs[0]);
        auto y = dml::InputTensor(scope, 1, inputs[1]);

        ExpressionFunctor expression;
        auto result = expression(x, y);

        ComPtr<IDMLCompiledOperator> compiled_op =
            scope.Compile(DML_EXECUTION_FLAG_NONE, {result});

        Initialize(ctx, std::move(tensors), compiled_op.Get());
    }

    StatusOr<DmlGpuEvent> Compute(DmlKernelContext* ctx) const
    {
        Tensor& output = ctx->GetOutputTensor(0);
        if (zero_outputs_)
        {
            ctx->GetDmlDeviceContext()->ZeroBuffer(
                ctx->GetDmlDeviceContext()->GetBufferForTensor(output));
        }
        return DmlKernel::Compute(ctx);
    }

  private:
    bool zero_outputs_ = false;
};

template <
    DML_OPERATOR_TYPE op_type,
    typename DML_OPERATOR_SPECIFIC_DESC,
    int... constants>
class DmlUnaryKernel : public DmlKernel
{
  public:
    using InitHelper = ElementWiseInitHelper<UINT32_MAX>;

    explicit DmlUnaryKernel(
        DmlKernelConstruction* ctx,
        const InitHelper* init_helper)
    {
        CHECK(ctx->GetInputCount() == 1);
        CHECK(ctx->GetOutputCount() == 1);

        // Currently, 64-bit integers in DML are emulated using 32-bit integers
        // using striding to emulate a larger type. Because we can't guarantee
        // that our output tensor's memory is zero'd, we need to do so manually
        // prior to running running gather.
        if (Is64BitIntegerType(ctx->GetOutputDataType(0)))
        {
            zero_outputs_ = true;
        }

        TensorShape tensor_shape({ctx->GetOutputTensorShape(0).num_elements()});
        DmlKernelTensors tensors =
            CreateKernelTensors(ctx, {tensor_shape}, tensor_shape);
        auto inputs = GetDmlTensorDescs(tensors.inputs);
        auto outputs = GetDmlTensorDescs(tensors.outputs);

        DML_OPERATOR_SPECIFIC_DESC op_specific_desc = {
            &inputs[0],
            outputs.data(),
            constants...};

        DML_OPERATOR_DESC op_desc = {op_type, &op_specific_desc};
        Initialize(ctx, std::move(tensors), op_desc);
    }

    StatusOr<DmlGpuEvent> Compute(DmlKernelContext* ctx) const
    {
        Tensor& output = ctx->GetOutputTensor(0);
        if (zero_outputs_)
        {
            ctx->GetDmlDeviceContext()->ZeroBuffer(
                ctx->GetDmlDeviceContext()->GetBufferForTensor(output));
        }
        return DmlKernel::Compute(ctx);
    }

  private:
    bool zero_outputs_ = false;
};

template <DML_OPERATOR_TYPE op_type, typename DML_OPERATOR_SPECIFIC_DESC>
class DmlMaxActivationKernel : public DmlKernel
{
  public:
    using InitHelper = ElementWiseInitHelper<UINT32_MAX>;

    explicit DmlMaxActivationKernel(
        DmlKernelConstruction* ctx,
        const InitHelper* init_helper)
    {
        CHECK(ctx->GetInputCount() == 1);
        CHECK(ctx->GetOutputCount() == 1);

        const TensorShape& input_shape = ctx->GetInputTensorShape(0);

        int batch_size = 1;
        int logits_size = input_shape.dim_size(input_shape.dims() - 1);

        // DML doesn't support tensors with rank > 2 for the max activation
        // functions, so collapse all the batch dimensions together
        for (int i = 0; i < input_shape.dims() - 1; ++i)
        {
            batch_size *= input_shape.dim_size(i);
        }

        TensorShape dml_tensor_shape;
        dml_tensor_shape.AddDim(batch_size);
        dml_tensor_shape.AddDim(logits_size);

        const auto tensor_layout =
            GetDmlTensorLayout(FORMAT_NCHW, dml_tensor_shape.dims());

        DmlTensorInfo input;
        input.kernel_index = 0;
        input.desc = DmlTensorDesc::Create(
            ctx->GetInputDataType(0),
            dml_tensor_shape,
            dml_tensor_shape,
            tensor_layout);

        DmlTensorInfo output;
        output.kernel_index = 0;
        output.desc = DmlTensorDesc::Create(
            ctx->GetOutputDataType(0),
            dml_tensor_shape,
            dml_tensor_shape,
            tensor_layout);

        DmlKernelTensors tensors;
        tensors.inputs = {input};
        tensors.outputs = {output};

        auto input_descs = GetDmlTensorDescs(tensors.inputs);
        auto output_descs = GetDmlTensorDescs(tensors.outputs);

        DML_OPERATOR_SPECIFIC_DESC op_specific_desc = {};
        op_specific_desc.InputTensor = input_descs.data();
        op_specific_desc.OutputTensor = output_descs.data();

        DML_OPERATOR_DESC op_desc = {op_type, &op_specific_desc};
        Initialize(ctx, std::move(tensors), op_desc);
    }
};

template <typename ExpressionFunctor, uint32_t max_dim_count>
class DmlCompositeUnaryKernel : public DmlKernel
{
  public:
    using InitHelper = ElementWiseInitHelper<UINT32_MAX>;

    explicit DmlCompositeUnaryKernel(
        DmlKernelConstruction* ctx,
        const InitHelper* init_helper)
    {
        CHECK(ctx->GetInputCount() == 1);
        CHECK(ctx->GetOutputCount() == 1);

        // Currently, 64-bit integers in DML are emulated using 32-bit integers
        // using striding to emulate a larger type. Because we can't guarantee
        // that our output tensor's memory is zero'd, we need to do so manually
        // prior to running running gather.
        if (Is64BitIntegerType(ctx->GetOutputDataType(0)))
        {
            zero_outputs_ = true;
        }

        TensorShape tensor_shape({ctx->GetOutputTensorShape(0).num_elements()});
        DmlKernelTensors tensors =
            CreateKernelTensors(ctx, {tensor_shape}, tensor_shape);

        auto inputs = GetDmlTensorDescs(tensors.inputs);
        auto outputs = GetDmlTensorDescs(tensors.outputs);

        auto scope = dml::Graph(ctx->GetDmlDevice());
        auto x = dml::InputTensor(scope, 0, inputs[0]);

        ExpressionFunctor expression;
        auto result = expression(x);

        ComPtr<IDMLCompiledOperator> compiled_op =
            scope.Compile(DML_EXECUTION_FLAG_NONE, {result});

        Initialize(ctx, std::move(tensors), compiled_op.Get());
    }

    StatusOr<DmlGpuEvent> Compute(DmlKernelContext* ctx) const
    {
        Tensor& output = ctx->GetOutputTensor(0);
        if (zero_outputs_)
        {
            ctx->GetDmlDeviceContext()->ZeroBuffer(
                ctx->GetDmlDeviceContext()->GetBufferForTensor(output));
        }
        return DmlKernel::Compute(ctx);
    }

  private:
    bool zero_outputs_ = false;
};

template <
    DML_OPERATOR_TYPE op_type,
    typename DML_OPERATOR_SPECIFIC_DESC,
    int scale = 1,
    int bias = 0,
    int... constants>
class DmlUnaryScaleBiasKernel : public DmlKernel
{
  public:
    using InitHelper = ElementWiseInitHelper<UINT32_MAX>;

    explicit DmlUnaryScaleBiasKernel(
        DmlKernelConstruction* ctx,
        const InitHelper* init_helper)
    {
        CHECK(ctx->GetInputCount() == 1);
        CHECK(ctx->GetOutputCount() == 1);

        // Currently, 64-bit integers in DML are emulated using 32-bit integers
        // using striding to emulate a larger type. Because we can't guarantee
        // that our output tensor's memory is zero'd, we need to do so manually
        // prior to running running gather.
        if (Is64BitIntegerType(ctx->GetOutputDataType(0)))
        {
            zero_outputs_ = true;
        }

        TensorShape tensor_shape({ctx->GetOutputTensorShape(0).num_elements()});
        DmlKernelTensors tensors =
            CreateKernelTensors(ctx, {tensor_shape}, tensor_shape);
        auto inputs = GetDmlTensorDescs(tensors.inputs);
        auto outputs = GetDmlTensorDescs(tensors.outputs);

        DML_SCALE_BIAS scale_bias = {scale, bias};
        DML_OPERATOR_SPECIFIC_DESC op_specific_desc = {
            &inputs[0],
            &outputs[0],
            &scale_bias,
            constants...,
        };

        DML_OPERATOR_DESC op_desc = {op_type, &op_specific_desc};
        Initialize(ctx, std::move(tensors), op_desc);
    }

    StatusOr<DmlGpuEvent> Compute(DmlKernelContext* ctx) const
    {
        Tensor& output = ctx->GetOutputTensor(0);
        if (zero_outputs_)
        {
            ctx->GetDmlDeviceContext()->ZeroBuffer(
                ctx->GetDmlDeviceContext()->GetBufferForTensor(output));
        }
        return DmlKernel::Compute(ctx);
    }

  private:
    bool zero_outputs_ = false;
};

struct DmlDivNoNanFunctor
{
    dml::Expression operator()(
        dml::Expression zero,
        dml::Expression x,
        dml::Expression y)
    {
        return dml::If(y == zero, zero, x / y);
    }
};

struct DmlErfcFunctor
{
    dml::Expression operator()(dml::Expression x)
    {
        return (1.0f - dml::Erf(x));
    }
};

struct DmlExpm1Functor
{
    dml::Expression operator()(dml::Expression x)
    {
        return (dml::Exp(x) - 1.0f);
    }
};

struct DmlIsFiniteFunctor
{
    dml::Expression operator()(dml::Expression x)
    {
        return (!(dml::IsNaN(x) || dml::IsInfinity(x)));
    }
};

struct DmlFloorDivFunctor
{
    dml::Expression operator()(dml::Expression x, dml::Expression y)
    {
        return (dml::Floor(x / y));
    }
};

struct DmlGreaterEqualFunctor
{
    dml::Expression operator()(dml::Expression x, dml::Expression y)
    {
        return (x >= y);
    }
};

struct DmlLessEqualFunctor
{
    dml::Expression operator()(dml::Expression x, dml::Expression y)
    {
        return (x <= y);
    }
};

struct DmlNotEqualFunctor
{
    dml::Expression operator()(dml::Expression x, dml::Expression y)
    {
        return (x != y);
    }
};

struct DmlReciprocalGradFunctor
{
    dml::Expression operator()(dml::Expression x, dml::Expression y)
    {
        return (-y * x * x);
    }
};

struct DmlRsqrtFunctor
{
    dml::Expression operator()(dml::Expression x)
    {
        return (1.0f / dml::Sqrt(x));
    }
};

struct DmlSigmoidGradFunctor
{
    dml::Expression operator()(dml::Expression x, dml::Expression y)
    {
        return (y * x * (1 - x));
    }
};

struct DmlTanhGradFunctor
{
    dml::Expression operator()(dml::Expression x, dml::Expression y)
    {
        return (y * (1 - x * x));
    }
};

static void RegisterAbs()
{
    using K = KernelDefinition<
        ops::Abs,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ABS,
                DML_ELEMENT_WISE_ABS_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Abs::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16>();
}

static void RegisterAcos()
{
    using K = KernelDefinition<
        ops::Acos,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ACOS,
                DML_ELEMENT_WISE_ACOS_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Acos::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterAcosh()
{
    using K = KernelDefinition<
        ops::Acosh,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ACOSH,
                DML_ELEMENT_WISE_ACOSH_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Acosh::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterAdd()
{
    using K = KernelDefinition<
        ops::Add,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_ADD,
                DML_ELEMENT_WISE_ADD_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Add::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT64,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterAddV2()
{
    using K = KernelDefinition<
        ops::AddV2,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_ADD,
                DML_ELEMENT_WISE_ADD_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::AddV2::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT64,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterAsin()
{
    using K = KernelDefinition<
        ops::Asin,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ASIN,
                DML_ELEMENT_WISE_ASIN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Asin::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterAsinh()
{
    using K = KernelDefinition<
        ops::Asinh,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ASINH,
                DML_ELEMENT_WISE_ASINH_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Asinh::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterAtan()
{
    using K = KernelDefinition<
        ops::Atan,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ATAN,
                DML_ELEMENT_WISE_ATAN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Atan::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterAtanh()
{
    using K = KernelDefinition<
        ops::Atanh,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ATANH,
                DML_ELEMENT_WISE_ATANH_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Atanh::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterCeil()
{
    using K = KernelDefinition<
        ops::Ceil,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_CEIL,
                DML_ELEMENT_WISE_CEIL_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Ceil::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterCos()
{
    using K = KernelDefinition<
        ops::Cos,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_COS,
                DML_ELEMENT_WISE_COS_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Cos::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterCosh()
{
    using K = KernelDefinition<
        ops::Cosh,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_COSH,
                DML_ELEMENT_WISE_COSH_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Cosh::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterDiv()
{
    using K = KernelDefinition<
        ops::Div,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_DIVIDE,
                DML_ELEMENT_WISE_DIVIDE_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Div::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT64,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterDivNoNan()
{
    using K = KernelDefinition<
        ops::DivNoNan,
        DmlKernelWrapper<
            DmlBinaryWithZeroKernel<DmlDivNoNanFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::DivNoNan::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT32,
        TF_INT64,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterElu()
{
    using K = KernelDefinition<
        ops::Elu,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ACTIVATION_ELU,
                DML_ACTIVATION_ELU_OPERATOR_DESC,
                1>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Elu::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterEqual()
{
    using K = KernelDefinition<
        ops::Equal,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_LOGICAL_EQUALS,
                DML_ELEMENT_WISE_LOGICAL_EQUALS_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Equal::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterErf()
{
    using K = KernelDefinition<
        ops::Erf,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_ERF,
                DML_ELEMENT_WISE_ERF_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Erf::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterErfc()
{
    using K = KernelDefinition<
        ops::Erfc,
        DmlKernelWrapper<
            DmlCompositeUnaryKernel<DmlErfcFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Erfc::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterExp()
{
    using K = KernelDefinition<
        ops::Exp,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_EXP,
                DML_ELEMENT_WISE_EXP_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Exp::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterExpm1()
{
    using K = KernelDefinition<
        ops::Expm1,
        DmlKernelWrapper<
            DmlCompositeUnaryKernel<DmlExpm1Functor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Expm1::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterFloor()
{
    using K = KernelDefinition<
        ops::Floor,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_FLOOR,
                DML_ELEMENT_WISE_FLOOR_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Floor::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterFloorDiv()
{
    using K = KernelDefinition<
        ops::FloorDiv,
        DmlKernelWrapper<
            DmlCompositeBinaryKernel<DmlFloorDivFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::FloorDiv::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterFloorMod()
{
    using K = KernelDefinition<
        ops::FloorMod,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_MODULUS_FLOOR,
                DML_ELEMENT_WISE_MODULUS_FLOOR_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::FloorMod::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterGreater()
{
    using K = KernelDefinition<
        ops::Greater,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_LOGICAL_GREATER_THAN,
                DML_ELEMENT_WISE_LOGICAL_GREATER_THAN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Greater::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterGreaterEqual()
{
    using K = KernelDefinition<
        ops::GreaterEqual,
        DmlKernelWrapper<
            DmlCompositeBinaryKernel<
                DmlGreaterEqualFunctor,
                kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::GreaterEqual::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterInv()
{
    using K = KernelDefinition<
        ops::Inv,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_RECIP,
                DML_ELEMENT_WISE_RECIP_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Inv::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterIsFinite()
{
    using K = KernelDefinition<
        ops::IsFinite,
        DmlKernelWrapper<
            DmlCompositeUnaryKernel<DmlIsFiniteFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::IsFinite::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterIsInf()
{
    using K = KernelDefinition<
        ops::IsInf,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ELEMENT_WISE_IS_INFINITY,
                DML_ELEMENT_WISE_IS_INFINITY_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::IsInf::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterIsNan()
{
    using K = KernelDefinition<
        ops::IsNan,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ELEMENT_WISE_IS_NAN,
                DML_ELEMENT_WISE_IS_NAN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::IsNan::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterLess()
{
    using K = KernelDefinition<
        ops::Less,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_LOGICAL_LESS_THAN,
                DML_ELEMENT_WISE_LOGICAL_LESS_THAN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Less::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterLessEqual()
{
    using K = KernelDefinition<
        ops::LessEqual,
        DmlKernelWrapper<
            DmlCompositeBinaryKernel<DmlLessEqualFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::LessEqual::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterLog()
{
    using K = KernelDefinition<
        ops::Log,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_LOG,
                DML_ELEMENT_WISE_LOG_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Log::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterLog1p()
{
    using K = KernelDefinition<
        ops::Log1p,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_LOG,
                DML_ELEMENT_WISE_LOG_OPERATOR_DESC,
                1,
                1>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Log1p::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterLogicalAnd()
{
    using K = KernelDefinition<
        ops::LogicalAnd,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_LOGICAL_AND,
                DML_ELEMENT_WISE_LOGICAL_AND_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    K::Register();
}

static void RegisterLogicalNot()
{
    using K = KernelDefinition<
        ops::LogicalNot,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ELEMENT_WISE_LOGICAL_NOT,
                DML_ELEMENT_WISE_LOGICAL_NOT_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    K::Register();
}

static void RegisterLogicalOr()
{
    using K = KernelDefinition<
        ops::LogicalOr,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_LOGICAL_OR,
                DML_ELEMENT_WISE_LOGICAL_OR_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    K::Register();
}

static void RegisterLogSoftmax()
{
    using K = KernelDefinition<
        ops::LogSoftmax,
        DmlKernelWrapper<
            DmlMaxActivationKernel<
                DML_OPERATOR_ACTIVATION_LOG_SOFTMAX,
                DML_ACTIVATION_LOG_SOFTMAX_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::LogSoftmax::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterMaximum()
{
    using K = KernelDefinition<
        ops::Maximum,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_MAX,
                DML_ELEMENT_WISE_MAX_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Maximum::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterMinimum()
{
    using K = KernelDefinition<
        ops::Minimum,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_MIN,
                DML_ELEMENT_WISE_MIN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Minimum::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterMod()
{
    using K = KernelDefinition<
        ops::Mod,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_MODULUS_TRUNCATE,
                DML_ELEMENT_WISE_MODULUS_TRUNCATE_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Mod::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterMul()
{
    using K = KernelDefinition<
        ops::Mul,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_MULTIPLY,
                DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Mul::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT64,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterNeg()
{
    using K = KernelDefinition<
        ops::Neg,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_IDENTITY,
                DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC,
                -1,
                0>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Neg::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16>();
}

static void RegisterNotEqual()
{
    using K = KernelDefinition<
        ops::NotEqual,
        DmlKernelWrapper<
            DmlCompositeBinaryKernel<DmlNotEqualFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::NotEqual::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterPow()
{
    using K = KernelDefinition<
        ops::Pow,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_POW,
                DML_ELEMENT_WISE_POW_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Pow::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterRealDiv()
{
    using K = KernelDefinition<
        ops::RealDiv,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_DIVIDE,
                DML_ELEMENT_WISE_DIVIDE_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::RealDiv::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT32,
        TF_INT64,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterReciprocal()
{
    using K = KernelDefinition<
        ops::Reciprocal,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_RECIP,
                DML_ELEMENT_WISE_RECIP_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Reciprocal::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterReciprocalGrad()
{
    using K = KernelDefinition<
        ops::ReciprocalGrad,
        DmlKernelWrapper<
            DmlCompositeBinaryKernel<
                DmlReciprocalGradFunctor,
                kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::ReciprocalGrad::Attribute::T,
        TF_FLOAT,
        TF_HALF>();
}

static void RegisterRelu6()
{
    using K = KernelDefinition<
        ops::Relu6,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_CLIP,
                DML_ELEMENT_WISE_CLIP_OPERATOR_DESC,
                1,
                0,
                0,
                6>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Relu6::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT32,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterRound()
{
    using K = KernelDefinition<
        ops::Round,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ELEMENT_WISE_ROUND,
                DML_ELEMENT_WISE_ROUND_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Round::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterRsqrt()
{
    using K = KernelDefinition<
        ops::Rsqrt,
        DmlKernelWrapper<
            DmlCompositeUnaryKernel<DmlRsqrtFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Rsqrt::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSigmoid()
{
    using K = KernelDefinition<
        ops::Sigmoid,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ACTIVATION_SIGMOID,
                DML_ACTIVATION_SIGMOID_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Sigmoid::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSigmoidGrad()
{
    using K = KernelDefinition<
        ops::SigmoidGrad,
        DmlKernelWrapper<
            DmlCompositeBinaryKernel<
                DmlSigmoidGradFunctor,
                kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::SigmoidGrad::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSign()
{
    using K = KernelDefinition<
        ops::Sign,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ELEMENT_WISE_SIGN,
                DML_ELEMENT_WISE_SIGN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Sign::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16>();
}

static void RegisterSin()
{
    using K = KernelDefinition<
        ops::Sin,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_SIN,
                DML_ELEMENT_WISE_SIN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Sin::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSinh()
{
    using K = KernelDefinition<
        ops::Sinh,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_SINH,
                DML_ELEMENT_WISE_SINH_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Sinh::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSoftmax()
{
    using K = KernelDefinition<
        ops::Softmax,
        DmlKernelWrapper<
            DmlMaxActivationKernel<
                DML_OPERATOR_ACTIVATION_SOFTMAX,
                DML_ACTIVATION_SOFTMAX_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Softmax::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSoftplus()
{
    using K = KernelDefinition<
        ops::Softplus,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ACTIVATION_SOFTPLUS,
                DML_ACTIVATION_SOFTPLUS_OPERATOR_DESC,
                1>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Softplus::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSoftsign()
{
    using K = KernelDefinition<
        ops::Softsign,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ACTIVATION_SOFTSIGN,
                DML_ACTIVATION_SOFTSIGN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Softsign::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSqrt()
{
    using K = KernelDefinition<
        ops::Sqrt,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_SQRT,
                DML_ELEMENT_WISE_SQRT_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Sqrt::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSquare()
{
    using K = KernelDefinition<
        ops::Square,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_CONSTANT_POW,
                DML_ELEMENT_WISE_CONSTANT_POW_OPERATOR_DESC,
                1,
                0,
                2>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Square::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterSub()
{
    using K = KernelDefinition<
        ops::Sub,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_SUBTRACT,
                DML_ELEMENT_WISE_SUBTRACT_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::Sub::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT64,
        TF_UINT32,
        TF_UINT64>();
}

static void RegisterTan()
{
    using K = KernelDefinition<
        ops::Tan,
        DmlKernelWrapper<
            DmlUnaryScaleBiasKernel<
                DML_OPERATOR_ELEMENT_WISE_TAN,
                DML_ELEMENT_WISE_TAN_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Tan::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterTanh()
{
    using K = KernelDefinition<
        ops::Tanh,
        DmlKernelWrapper<
            DmlUnaryKernel<
                DML_OPERATOR_ELEMENT_WISE_TANH,
                DML_ELEMENT_WISE_TANH_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::Tanh::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterTanhGrad()
{
    using K = KernelDefinition<
        ops::TanhGrad,
        DmlKernelWrapper<
            DmlCompositeBinaryKernel<DmlTanhGradFunctor, kNchwDimensionCount>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<K, ops::TanhGrad::Attribute::T, TF_FLOAT, TF_HALF>();
}

static void RegisterTruncateMod()
{
    using K = KernelDefinition<
        ops::TruncateMod,
        DmlKernelWrapper<
            DmlBinaryKernel<
                DML_OPERATOR_ELEMENT_WISE_MODULUS_TRUNCATE,
                DML_ELEMENT_WISE_MODULUS_TRUNCATE_OPERATOR_DESC>,
            GetBroadcastedOutputShapeHelper>>;

    RegisterWithTypes<
        K,
        ops::TruncateMod::Attribute::T,
        TF_FLOAT,
        TF_HALF,
        TF_INT8,
        TF_INT16,
        TF_INT64,
        TF_UINT8,
        TF_UINT16,
        TF_UINT32,
        TF_UINT64>();
}

void RegisterKernels_Cwise()
{
    RegisterAbs();
    RegisterAcos();
    RegisterAcosh();
    RegisterAdd();
    RegisterAddV2();
    RegisterAsin();
    RegisterAsinh();
    RegisterAtan();
    RegisterAtanh();
    RegisterCeil();
    RegisterCos();
    RegisterCosh();
    RegisterDiv();
    RegisterDivNoNan();
    RegisterElu();
    RegisterEqual();
    RegisterErf();
    RegisterErfc();
    RegisterExp();
    RegisterExpm1();
    RegisterFloor();
    RegisterFloorDiv();
    RegisterFloorMod();
    RegisterGreater();
    RegisterGreaterEqual();
    RegisterInv();
    RegisterIsFinite();
    RegisterIsInf();
    RegisterIsNan();
    RegisterLess();
    RegisterLessEqual();
    RegisterLog();
    RegisterLog1p();
    RegisterLogicalAnd();
    RegisterLogicalNot();
    RegisterLogicalOr();
    RegisterLogSoftmax();
    RegisterMaximum();
    RegisterMinimum();
    RegisterMod();
    RegisterMul();
    RegisterNeg();
    RegisterNotEqual();
    RegisterPow();
    RegisterReciprocal();
    RegisterRealDiv();
    RegisterReciprocalGrad();
    RegisterRelu6();
    RegisterRound();
    RegisterRsqrt();
    RegisterSigmoid();
    RegisterSigmoidGrad();
    RegisterSign();
    RegisterSin();
    RegisterSinh();
    RegisterSoftmax();
    RegisterSoftplus();
    RegisterSoftsign();
    RegisterSqrt();
    RegisterSquare();
    RegisterSub();
    RegisterTan();
    RegisterTanh();
    RegisterTanhGrad();
    RegisterTruncateMod();
}

} // namespace tfdml