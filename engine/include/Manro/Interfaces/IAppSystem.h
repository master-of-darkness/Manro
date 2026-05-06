#pragma once

namespace Manro {
    enum InitReturnVal_t {
        INIT_OK = 0,
        INIT_FAILED
    };

    class IAppSystem {
    public:
        virtual ~IAppSystem() = default;

        virtual bool Connect() { return true; }

        virtual void Disconnect() {
        }

        virtual InitReturnVal_t Init() { return INIT_OK; }

        virtual void Shutdown() {
        }
    };
}
