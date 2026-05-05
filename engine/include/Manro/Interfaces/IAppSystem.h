#pragma once

namespace Manro {
    enum InitReturnVal_t {
        INIT_OK = 0,
        INIT_FAILED
    };

    class IAppSystem {
    public:
        virtual ~IAppSystem() = default;

        virtual bool Connect(void * (*factory)(const char *, int *)) = 0;

        virtual void Disconnect() = 0;

        virtual InitReturnVal_t Init() = 0;

        virtual void Shutdown() = 0;
    };
}