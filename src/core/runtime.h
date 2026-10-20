// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
namespace muse
{
class BaseApplication;
}
namespace jianpu
{
class EngineRuntime
{
  public:
    EngineRuntime();
    ~EngineRuntime();
    EngineRuntime(const EngineRuntime &) = delete;
    EngineRuntime &operator=(const EngineRuntime &) = delete;

  private:
    std::shared_ptr<muse::BaseApplication> m_app;
};
} // namespace jianpu
