#pragma once

#include <Manro/Core/Types.h>
#include <vector>

namespace Manro {
    struct CpuTopology_t {
        std::vector<u32> m_PerformanceCores;
        std::vector<u32> m_EfficiencyCores;

        [[nodiscard]] bool IsHybrid() const { return !m_EfficiencyCores.empty(); }
    };

    [[nodiscard]] CpuTopology_t QueryCpuTopology();

    // Returns true on success or false if affinity is unsupported or failed
    bool PinThreadToCpu(u32 unLogicalCpu);

    bool SetThreadAffinity(const std::vector<u32> &logicalCpus);
} // namespace Manro
