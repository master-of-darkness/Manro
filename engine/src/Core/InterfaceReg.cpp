#include <Manro/Core/InterfaceReg.h>
#include <cstring>
#include <Manro/Core/Logger.h>

namespace Manro {
    // Use a function to guarantee initialization order
    CInterfaceReg *&GetInterfaceRegs() {
        static CInterfaceReg *s_pInterfaceRegs = nullptr;
        return s_pInterfaceRegs;
    }

    CInterfaceReg::CInterfaceReg(InstantiateInterfaceFn fn, const char *pName)
        : m_CreateFn(fn)
          , m_pName(pName) {
        m_pNext = GetInterfaceRegs();
        GetInterfaceRegs() = this;
    }

    void *Sys_GetFactory(const char *pName, int *pReturnCode) {
        for (CInterfaceReg *pCur = GetInterfaceRegs(); pCur; pCur = pCur->m_pNext) {
            if (strcmp(pCur->m_pName, pName) == 0) {
                if (pReturnCode) *pReturnCode = 0;
                return pCur->m_CreateFn();
            }
        }

        LOG_WARN("Sys_GetFactory failed to find interface: {}", pName);
        if (pReturnCode) *pReturnCode = 1;
        return nullptr;
    }
}
