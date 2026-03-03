#include "SlangCompiler.h"
#include "Core/Logger.h"

namespace Engine {
    SlangCompiler::SlangCompiler() = default;

    SlangCompiler::~SlangCompiler() {
        Shutdown();
    }

    void SlangCompiler::Initialize() {
        if (SLANG_FAILED(slang::createGlobalSession(m_GlobalSession.writeRef()))) {
            LOG_ERROR("Failed to create Slang Global Session!");
            return;
        }
    }

    void SlangCompiler::Shutdown() {
        m_GlobalSession.setNull();
    }

    bool SlangCompiler::CompileShaderToSPIRV(const char *filePath,
                                             const char *entryPointName,
                                             const char *profileName,
                                             std::vector<u8> &outSpirv) {
        if (!m_GlobalSession) return false;

        slang::TargetDesc targetDesc = {};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = m_GlobalSession->findProfile(profileName);

        if (targetDesc.profile == SLANG_PROFILE_UNKNOWN) {
            LOG_ERROR("Unknown Slang profile: {}", profileName);
            return false;
        }

        slang::SessionDesc sessionDesc = {};
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;
        slang::CompilerOptionEntry compilerOptions[] = {
            {
                slang::CompilerOptionName::MatrixLayoutColumn,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
            {
                slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            }
        };
        sessionDesc.compilerOptionEntries = compilerOptions;
        sessionDesc.compilerOptionEntryCount = 2;


        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(m_GlobalSession->createSession(sessionDesc, session.writeRef()))) {
            LOG_ERROR("Failed to create Slang Session!");
            return false;
        }

        Slang::ComPtr<slang::IBlob> diagnosticBlob;
        slang::IModule *module = session->loadModule(filePath, diagnosticBlob.writeRef());

        if (diagnosticBlob != nullptr) {
            LOG_ERROR("Slang Diagnostic:\n{}", (const char *) diagnosticBlob->getBufferPointer());
            if (!module) return false;
        } else if (!module) {
            LOG_ERROR("Failed to load Slang module: {}", filePath);
            return false;
        }

        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        if (SLANG_FAILED(module->findEntryPointByName(entryPointName, entryPoint.writeRef()))) {
            LOG_ERROR("Failed to find entry point: {}", entryPointName);
            return false;
        }

        slang::IComponentType *componentTypes[] = {module, entryPoint.get()};
        Slang::ComPtr<slang::IComponentType> program;
        if (SLANG_FAILED(session->createCompositeComponentType(
            componentTypes, 2, program.writeRef(), diagnosticBlob.writeRef()))) {
            if (diagnosticBlob) {
                LOG_ERROR("Slang Error linking components:\n{}", (const char *) diagnosticBlob->getBufferPointer());
            }
            return false;
        }

        Slang::ComPtr<slang::IBlob> spirvBlob;
        if (SLANG_FAILED(program->getEntryPointCode(0, 0, spirvBlob.writeRef(), diagnosticBlob.writeRef()))) {
            if (diagnosticBlob) {
                LOG_ERROR("Slang Error getting entry point code:\n{}",
                          (const char *) diagnosticBlob->getBufferPointer());
            }
            return false;
        }

        const u8 *data = static_cast<const u8 *>(spirvBlob->getBufferPointer());
        size_t size = spirvBlob->getBufferSize();
        outSpirv.assign(data, data + size);

        return true;
    }
} // namespace Engine