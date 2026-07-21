#include <Manro/Core/CpuAffinity.h>

#include <algorithm>
#include <thread>

#ifdef _WIN32

#include <windows.h>

#else

#include <pthread.h>
#include <sched.h>
#include <fstream>
#include <map>
#include <string>

#endif

namespace Manro {
#ifdef _WIN32
    namespace {
        CpuTopology_t QueryTopologyWindows() {
            CpuTopology_t topo;

            DWORD len = 0;
            GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
            if (len == 0) {
                return topo;
            }

            std::vector<u8> buffer(len);
            auto *info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buffer.data());
            if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &len)) {
                return topo;
            }

            struct Core_t {
                BYTE efficiencyClass;
                u32 logicalCpu;
            };
            std::vector<Core_t> cores;
            BYTE maxClass = 0;

            auto *cursor = reinterpret_cast<u8 *>(info);
            const u8 *end = cursor + len;
            while (cursor < end) {
                auto *entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(cursor);
                if (entry->Relationship == RelationProcessorCore && entry->Processor.GroupCount > 0) {
                    const GROUP_AFFINITY &group = entry->Processor.GroupMask[0];
                    // Use the first logical CPU of the core (its lowest lane).
                    for (u32 bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit) {
                        if (group.Mask & (static_cast<KAFFINITY>(1) << bit)) {
                            const BYTE cls = entry->Processor.EfficiencyClass;
                            maxClass = std::max(maxClass, cls);
                            cores.push_back({cls, static_cast<u32>(bit)});
                            break;
                        }
                    }
                }
                cursor += entry->Size;
            }

            for (const Core_t &c: cores) {
                if (c.efficiencyClass == maxClass) {
                    topo.m_PerformanceCores.push_back(c.logicalCpu);
                } else {
                    topo.m_EfficiencyCores.push_back(c.logicalCpu);
                }
            }

            return topo;
        }
    } // namespace

    CpuTopology_t QueryCpuTopology() {
        CpuTopology_t topo = QueryTopologyWindows();
        if (topo.m_PerformanceCores.empty()) {
            const u32 count = std::max(1u, std::thread::hardware_concurrency());
            topo.m_PerformanceCores.clear();
            topo.m_EfficiencyCores.clear();
            for (u32 i = 0; i < count; ++i) {
                topo.m_PerformanceCores.push_back(i);
            }
        }
        std::sort(topo.m_PerformanceCores.begin(), topo.m_PerformanceCores.end());
        std::sort(topo.m_EfficiencyCores.begin(), topo.m_EfficiencyCores.end());
        return topo;
    }

    bool PinThreadToCpu(u32 unLogicalCpu) {
        if (unLogicalCpu >= sizeof(DWORD_PTR) * 8) {
            return false;
        }
        const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << unLogicalCpu;
        return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
    }

    bool SetThreadAffinity(const std::vector<u32> &logicalCpus) {
        DWORD_PTR mask = 0;
        for (u32 cpu: logicalCpus) {
            if (cpu < sizeof(DWORD_PTR) * 8) {
                mask |= static_cast<DWORD_PTR>(1) << cpu;
            }
        }
        if (mask == 0) {
            return false;
        }
        return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
    }

#else // Linux

    namespace {
        u64 ReadCpuMaxFreq(u32 cpu) {
            std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
                               "/cpufreq/cpuinfo_max_freq";
            std::ifstream file(path);
            if (!file.is_open()) {
                return 0;
            }
            u64 freq = 0;
            file >> freq;
            return freq;
        }
    } // namespace

    CpuTopology_t QueryCpuTopology() {
        CpuTopology_t topo;

        const u32 count = std::max(1u, std::thread::hardware_concurrency());

        // Group logical CPUs by their advertised max frequency
        std::map<u64, std::vector<u32> > byFreq;
        bool haveFreqInfo = true;
        for (u32 cpu = 0; cpu < count; ++cpu) {
            const u64 freq = ReadCpuMaxFreq(cpu);
            if (freq == 0) {
                haveFreqInfo = false;
                break;
            }
            byFreq[freq].push_back(cpu);
        }

        if (haveFreqInfo && byFreq.size() > 1) {
            // Highest frequency bucket are performance cores, the rest is efficiency cores
            const u64 topFreq = byFreq.rbegin()->first;
            for (const auto &[freq, cpus]: byFreq) {
                auto &dst = (freq == topFreq) ? topo.m_PerformanceCores : topo.m_EfficiencyCores;
                dst.insert(dst.end(), cpus.begin(), cpus.end());
            }
        } else {
            // If no CPU freq info then treat everything as performance cores
            for (u32 cpu = 0; cpu < count; ++cpu) {
                topo.m_PerformanceCores.push_back(cpu);
            }
        }

        std::sort(topo.m_PerformanceCores.begin(), topo.m_PerformanceCores.end());
        std::sort(topo.m_EfficiencyCores.begin(), topo.m_EfficiencyCores.end());
        return topo;
    }

    bool PinThreadToCpu(u32 unLogicalCpu) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<int>(unLogicalCpu), &set);
        return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
    }

    bool SetThreadAffinity(const std::vector<u32> &logicalCpus) {
        if (logicalCpus.empty()) {
            return false;
        }
        cpu_set_t set;
        CPU_ZERO(&set);
        for (u32 cpu: logicalCpus) {
            CPU_SET(static_cast<int>(cpu), &set);
        }
        return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
    }

#endif
} // namespace Manro
