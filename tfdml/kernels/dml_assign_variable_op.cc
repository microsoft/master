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

#include "tfdml/core/dml_tracing.h"

namespace tfdml
{
class DmlAssignVariableOp : public OpKernel
{
  public:
    explicit DmlAssignVariableOp(
        OpKernelConstruction* c,
        std::shared_ptr<const NodeDef> node_def)
        : OpKernel(std::move(node_def))
    {
        OP_REQUIRES_OK(c, c->GetAttr("dtype", &dtype_));
        if (!c->GetAttr(
                  "_grappler_relax_allocator_constraints",
                  &relax_constraints_)
                 .ok())
        {
            relax_constraints_ = false;
        }
    }

    void Compute(OpKernelContext* context)
    {
        DmlDevice* dml_device = static_cast<DmlDevice*>(context->device());
        DmlTracing::KernelComputeEventScope event_scope(
            dml_device->GetDeviceOrdinal(),
            context->op_kernel().type_string(),
            context->op_kernel().name());

        OP_REQUIRES(
            context,
            dtype_ == context->input(1).dtype(),
            errors::InvalidArgument(
                "Variable and value dtypes don't match; respectively, ",
                DataTypeString(dtype_),
                " and ",
                DataTypeString(context->input(1).dtype())));
        RefCountPtr<Var> variable;
        const Tensor& value = context->input(1);
        // Note: every resource-variable-manipulating op assumes copy-on-write
        // semantics, and creates a copy of the variable's Tensor if its
        // refcount is bigger than 1 when we try to modify it. This means we
        // never need to copy the original tensor for AssignVariableOp; even if
        // there are other live users of it we know none can modify it so this
        // is always safe (even in esoteric cases where the same tensor is used
        // to initialize multiple variables or the tensor is a constant this is
        // safe, as future writes will trigger copies).

        const Tensor handle_input = context->input(0);

        OP_REQUIRES_OK(
            context,
            LookupOrCreateResource<Var>(
                context,
                handle_input.base<tensorflow::ResourceHandleProto>()[0],
                &variable,
                [this, &value](Var** ptr)
                {
                    *ptr = new Var(dtype_);
                    *(*ptr)->tensor() = value;
                    (*ptr)->is_initialized = true;
                    return Status::OK();
                }));
        std::unique_lock<std::shared_mutex> ml(*variable->mu());
        OP_REQUIRES(
            context,
            variable->tensor()->dtype() == dtype_,
            errors::InvalidArgument(
                "Trying to assign variable with wrong dtype. Expected ",
                DataTypeString(variable->tensor()->dtype()),
                " got ",
                DataTypeString(dtype_)));

        *variable->tensor() = value;
        variable->is_initialized = true;
    }

  private:
    TF_DataType dtype_;
    bool relax_constraints_;
};

void RegisterKernels_AssignVariableOp()
{
    using K = KernelDefinition<ops::AssignVariableOp, DmlAssignVariableOp>::
        WithHostMemoryArgument<ops::AssignVariableOp::Argument::resource>;

    // We deliberately register the same types here that CUDA does.
    constexpr auto T = ops::AssignVariableOp::Attribute::dtype;
    K::WithTypeConstraint<T, TF_BOOL>::Register();
    K::WithTypeConstraint<T, TF_COMPLEX64>::Register();
    K::WithTypeConstraint<T, TF_COMPLEX128>::Register();
    K::WithTypeConstraint<T, TF_HALF>::Register();
    K::WithTypeConstraint<T, TF_FLOAT>::Register();
    K::WithTypeConstraint<T, TF_DOUBLE>::Register();
    K::WithTypeConstraint<T, TF_INT64>::Register();
}

} // namespace tfdml