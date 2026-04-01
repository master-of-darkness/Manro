#pragma once

namespace Manro {

    class Interface {
    public:
        Interface() = default;

        Interface(const Interface &) = delete;

        Interface &operator=(const Interface &) = delete;

        Interface(Interface &&) = delete;

        Interface &operator=(Interface &&) = delete;

        virtual ~Interface() = default;
    };

} // namespace Manro