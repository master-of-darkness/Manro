#pragma once
#include <Core/Types.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <string>
#include <vector>

namespace Engine {
    class SlangCompiler {
    public:
        SlangCompiler();

        ~SlangCompiler() = default;

        SlangCompiler(const SlangCompiler &) = delete;

        SlangCompiler &operator=(const SlangCompiler &) = delete;

        bool CompileShaderToSPIRV(const char *filePath,
                                  const char *entryPointName,
                                  const char *profileName,
                                  std::vector<u8> &outSpirv);

    private:
        Slang::ComPtr<slang::IGlobalSession> m_GlobalSession;
    };
} // namespace Engine