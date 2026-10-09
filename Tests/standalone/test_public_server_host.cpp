// Standalone: clang++ -std=c++17 -IShared/sdk Tests/standalone/test_public_server_host.cpp -o /tmp/test-public-host
#include <PublicServerHost.h>
#include <cassert>
#include <string>

int main()
{
    for (const auto host : {"server.myroleplay.world", "example.com", "xn--bcher-kva.example"})
        assert(Neon::IsPublicServerHost(host));
    for (const auto host : {"", "localhost", "127.0.0.1", "a.123", "https://example.com", "example.com:22003", "a..com", "-a.com", "a-.com", "example.com/",
                            "example.com.", "Example.com", "user@example.com"})
        assert(!Neon::IsPublicServerHost(host));
    assert(!Neon::IsPublicServerHost(std::string("example.com\0suffix", 18)));
    assert(!Neon::IsPublicServerHost(std::string(64, 'a') + ".com"));
    const auto longest = std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." + std::string(61, 'd');
    assert(longest.size() == 253 && Neon::IsPublicServerHost(longest));
    assert(!Neon::IsPublicServerHost(longest + "d"));
}
