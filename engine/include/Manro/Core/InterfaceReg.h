#pragma once

namespace Manro {
    typedef void * (*InstantiateInterfaceFn)();

    class CInterfaceReg {
    public:
        CInterfaceReg(InstantiateInterfaceFn fn, const char *pName);

        InstantiateInterfaceFn m_CreateFn;
        const char *m_pName;
        CInterfaceReg *m_pNext;
    };

    CInterfaceReg *&GetInterfaceRegs();

    void *Sys_GetFactory(const char *pName, int *pReturnCode);

#define EXPOSE_SINGLE_INTERFACE_GLOBALVAR(className, interfaceName, versionName, globalVarName) \
    static void* __Create##className##_interface() { return static_cast<interfaceName*>(&globalVarName); } \
    static CInterfaceReg __g_Create##className##_reg(__Create##className##_interface, versionName);

#define EXPOSE_SINGLE_INTERFACE(className, interfaceName, versionName) \
    static className __g_##className##_singleton; \
    EXPOSE_SINGLE_INTERFACE_GLOBALVAR(className, interfaceName, versionName, __g_##className##_singleton)
}