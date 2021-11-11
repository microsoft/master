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

#include "tfdml/core/device.h"

namespace tfdml
{

Device::Device() { rmgr_ = new ResourceMgr(); }

Device::~Device()
{
    if (rmgr_ != nullptr)
    {
        delete rmgr_;
        rmgr_ = nullptr;
    }
}

} // namespace tfdml
