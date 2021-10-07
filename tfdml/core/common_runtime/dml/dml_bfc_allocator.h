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

#include "dml_buffer_region.h"

namespace tfdml {

class D3D12HeapAllocator;

// The framework "wraps" this allocator inside a BFC allocator and calls Alloc
// when it determines that it needs to grow the allocated memory. Here,
// DmlAllocator is basically a SubAllocator with additional functionalities like
// CreateBufferRegion().
class DmlAllocator {
 public:
  DmlAllocator(D3D12HeapAllocator* heap_allocator, const std::string& name);
  D3D12BufferRegion CreateBufferRegion(const void* ptr, uint64_t size_in_bytes);
  void* Alloc(size_t num_bytes);
  void Free(void* ptr, size_t num_bytes);

 private:
  D3D12HeapAllocator* heap_allocator_;
};

}  // namespace tfdml
