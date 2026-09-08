#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace Hyprcast {
    enum class eKeyboardFilter : std::uint8_t {
        INCLUDE,
        EXCLUDE
    };

    struct SConfig {
        eKeyboardFilter          filter = eKeyboardFilter::EXCLUDE;
        std::vector<std::string> filteredKeyboards;
        bool                     operator==(const SConfig&) const = default;
    };

    // Compositor-thread only. References remain valid until the next successful commit.
    class CConfig {
      public:
        const SConfig& accepted() const noexcept {
            return m_accepted;
        }
        void beginReload() noexcept;
        void finishReload(bool successful) noexcept;
        int  configure(lua_State* L) noexcept;

      private:
        SConfig m_accepted, m_pending;
        bool    m_evaluating = false;
        bool    m_called     = false;
        bool    m_failed     = false;
    };
}
