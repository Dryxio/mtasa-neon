#pragma once

#include <string_view>

namespace Neon
{
    // Keep the published label usable as a DNS hostname and a copied game URI.
    // Numeric suffixes are excluded to avoid interpreting an IP literal as a name.
    inline bool IsPublicServerHost(std::string_view host)
    {
        if (host.empty() || host.size() > 253 || host.find('.') == std::string_view::npos)
            return false;
        std::size_t start = 0;
        while (start < host.size())
        {
            const auto end = host.find('.', start);
            const auto label = host.substr(start, end == std::string_view::npos ? host.size() - start : end - start);
            if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-')
                return false;
            for (const char c : label)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
                    return false;
            if (end == std::string_view::npos)
                return label.front() >= 'a' && label.front() <= 'z';
            start = end + 1;
        }
        return false;
    }
}
