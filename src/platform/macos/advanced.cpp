#include <NeFlux/advanced.hpp>
#include "../../extras.hpp"
namespace neforce::neflux {
    struct advanced::impl {};
    advanced::advanced() :
    impl_(make_unique<impl>()) {}
    advanced::~advanced() = default;
    void advanced::collect(snapshot& value, const config& cfg) {
        static_cast<void>(value);
        static_cast<void>(cfg);
    }
    json discover_platform_extras(const config& cfg) { return discover_config_extras(cfg); }
} // namespace neforce::neflux
