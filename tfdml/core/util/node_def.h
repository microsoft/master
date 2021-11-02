/* Copyright (c) Microsoft Corporation.

Use of this source code is governed by an MIT-style
license that can be found in the LICENSE file or at
https://opensource.org/licenses/MIT.

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include "absl/container/inlined_vector.h"
#include "tensorflow/c/kernels.h"
#include "tfdml/core/util/op_defs.h"
#include "tfdml/core/util/types.h"

namespace tfdml
{

struct NodeDef
{
    std::string_view op_name;
    std::string_view op_type_string;
    absl::InlinedVector<MemoryType, 4> input_tensor_memory_types;
    absl::InlinedVector<MemoryType, 4> output_tensor_memory_types;
    // TODO: attributes and values

    static NodeDef Create(
        TF_OpKernelConstruction* ctx,
        std::string_view op_type_name,
        absl::Span<const ArgumentDesc> input_arg_descs,
        absl::Span<const ArgumentDesc> output_arg_descs,
        absl::Span<const AttributeDesc> attribute_descs);
};

} // namespace tfdml
