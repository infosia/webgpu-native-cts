#pragma once

#include <functional>
#include <vector>

#include "cts/test.h"
#include "cts/webgpu.h"

namespace cts {

class GpuTest : public Fixture {
  public:
    void init() override;
    void finalize() override;

    WGPUDevice device() const;
    WGPUQueue queue() const;
    WGPULimits getLimits() const;
    WGPUBuffer createBufferTracked(const WGPUBufferDescriptor& desc);
    void expectValidationError(const std::function<void()>& body, bool shouldError);

  private:
    std::vector<WGPUBuffer> buffers_;
};

} // namespace cts
