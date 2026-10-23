#include "Nbt.h"

#include <cstring>
#include <utility>

namespace Manro::Anvil {
    namespace {
        struct Cursor {
            const u8 *p{nullptr};
            size_t n{0};
            size_t o{0};
            bool ok{true};

            bool Need(size_t k) {
                if (o + k > n) {
                    ok = false;
                    return false;
                }
                return true;
            }

            u8 U8() {
                if (!Need(1))
                    return 0;
                return p[o++];
            }

            u16 U16() {
                if (!Need(2))
                    return 0;
                const u16 v = static_cast<u16>((static_cast<u16>(p[o]) << 8u) | p[o + 1]);
                o += 2;
                return v;
            }

            i16 I16() { return static_cast<i16>(U16()); }

            i32 I32() {
                if (!Need(4))
                    return 0;
                i32 v = 0;
                for (int k = 0; k < 4; ++k)
                    v = (v << 8) | p[o + static_cast<size_t>(k)];
                o += 4;
                return v;
            }

            i64 I64() {
                if (!Need(8))
                    return 0;
                i64 v = 0;
                for (int k = 0; k < 8; ++k)
                    v = (v << 8) | p[o + static_cast<size_t>(k)];
                o += 8;
                return v;
            }

            float F32() {
                const i32 b = I32();
                float f = 0.f;
                std::memcpy(&f, &b, 4);
                return f;
            }

            double F64() {
                const i64 b = I64();
                double v = 0.0;
                std::memcpy(&v, &b, 8);
                return v;
            }

            std::string Str() {
                const u16 len = U16();
                if (!ok)
                    return {};
                if (!Need(len))
                    return {};
                std::string s(reinterpret_cast<const char *>(p + o), len);
                o += len;
                return s;
            }
        };

        bool ParsePayload(Cursor &c, u8 type, NbtNode &out);

        bool ParseCompoundBody(Cursor &c, NbtNode &out) {
            out.type = 10;
            for (;;) {
                if (!c.Need(1))
                    return false;
                const u8 t = c.U8();
                if (t == 0)
                    return c.ok;
                std::string name = c.Str();
                if (!c.ok)
                    return false;
                NbtNode v;
                if (!ParsePayload(c, t, v))
                    return false;
                out.keys.push_back(std::move(name));
                out.items.push_back(std::move(v));
            }
        }

        bool ParsePayload(Cursor &c, u8 type, NbtNode &out) {
            out.type = type;
            switch (type) {
                case 1:
                    out.i = static_cast<i8>(c.U8());
                    return c.ok;
                case 2:
                    out.i = c.I16();
                    return c.ok;
                case 3:
                    out.i = c.I32();
                    return c.ok;
                case 4:
                    out.i = c.I64();
                    return c.ok;
                case 5:
                    out.d = c.F32();
                    return c.ok;
                case 6:
                    out.d = c.F64();
                    return c.ok;
                case 7: {
                    const i32 len = c.I32();
                    if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                        return false;
                    if (!c.Need(static_cast<size_t>(len)))
                        return false;
                    out.s.assign(reinterpret_cast<const char *>(c.p + c.o),
                                 static_cast<size_t>(len));
                    c.o += static_cast<size_t>(len);
                    return true;
                }
                case 8:
                    out.s = c.Str();
                    return c.ok;
                case 9: {
                    if (!c.Need(1))
                        return false;
                    out.listType = c.U8();
                    const i32 len = c.I32();
                    if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                        return false;
                    out.items.reserve(static_cast<size_t>(len));
                    for (i32 k = 0; k < len; ++k) {
                        NbtNode e;
                        if (!ParsePayload(c, out.listType, e))
                            return false;
                        out.items.push_back(std::move(e));
                    }
                    return true;
                }
                case 10:
                    return ParseCompoundBody(c, out);
                case 11: {
                    const i32 len = c.I32();
                    if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                        return false;
                    out.items.reserve(static_cast<size_t>(len));
                    for (i32 k = 0; k < len; ++k) {
                        NbtNode e;
                        e.type = 3;
                        e.i = c.I32();
                        if (!c.ok)
                            return false;
                        out.items.push_back(std::move(e));
                    }
                    return true;
                }
                case 12: {
                    const i32 len = c.I32();
                    if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                        return false;
                    out.items.reserve(static_cast<size_t>(len));
                    for (i32 k = 0; k < len; ++k) {
                        NbtNode e;
                        e.type = 4;
                        e.i = c.I64();
                        if (!c.ok)
                            return false;
                        out.items.push_back(std::move(e));
                    }
                    return true;
                }
                default:
                    return false;
            }
        }
    }

    bool ParseNbt(const std::vector<u8> &buf, NbtNode &root) {
        Cursor c{buf.data(), buf.size(), 0, true};
        if (!c.Need(1))
            return false;
        const u8 t = c.U8();
        if (t != 10)
            return false;
        (void) c.Str();
        if (!c.ok)
            return false;
        return ParseCompoundBody(c, root);
    }

    const NbtNode *Find(const NbtNode &parent, const char *name) {
        if (parent.type != 10)
            return nullptr;
        for (size_t k = 0; k < parent.keys.size(); ++k) {
            if (parent.keys[k] == name)
                return &parent.items[k];
        }
        return nullptr;
    }
}
