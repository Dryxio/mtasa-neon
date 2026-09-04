// Standalone checks execute the header used by CCoronasSA, without GTA or a GPU.
#include "../../Client/game_sa/CDistantLightsSA.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

struct Vector
{
    float fX = 0, fY = 0, fZ = 0;
    Vector() = default;
    Vector(float x, float y, float z) : fX(x), fY(y), fZ(z) {}
    Vector operator+(const Vector& v) const { return {fX + v.fX, fY + v.fY, fZ + v.fZ}; }
    Vector operator*(float scale) const { return {fX * scale, fY * scale, fZ * scale}; }
};
struct Rotation
{
    float fX = 0, fY = 0, fZ = 0, fW = 1;
};
struct Source
{
    Vector   position;
    Rotation rotation;
    int      modelId;
};
struct Candidate
{
    float distanceSquared;
    int   id;
};
static int checks = 0;
void       Check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
bool Near(float a, float b)
{
    return std::abs(a - b) < 0.0001f;
}
static bool pointSubmitted = false;
void        PointSpy(int type, float x, float y, float z, float dx, float dy, float dz, float radius, float red, float green, float blue, int fog, int shadow,
                     void* entity)
{
    Check(type == 1 && Near(x, 10) && Near(y, 20) && Near(z, 30), "point ABI position/type");
    Check(Near(dx, 0) && Near(dy, 0) && Near(dz, -1) && Near(radius, 5), "point ABI direction/radius");
    Check(Near(red, 1) && Near(green, 1) && Near(blue, 1) && fog == 0 && shadow == 0 && entity == nullptr, "point ABI color/flags");
    pointSubmitted = true;
}
#ifdef _WIN32
    #pragma warning(push)
    #pragma warning(disable : 4102)
    // Same stack-neutral marker as HookSystem.h, without the game's dependencies.
    #define MTA_VERIFY_HOOK_LOCAL_SIZE \
        __asm { push eax } \
        __asm \
        { \
        _localSize: \
            mov eax, __LOCAL_SIZE \
        } \
        __asm { pop eax }
    #include "../../Client/game_sa/CDistantLightNativeTransitionsSA.h"
    #undef MTA_VERIFY_HOOK_LOCAL_SIZE
    #pragma warning(pop)
static __declspec(naked) void ReturnPlain()
{
    __asm { ret }
}
static __declspec(naked) void ReturnNormal()
{
    __asm { add esp, 8 }
    __asm
    {
        ret
    }
}
static __declspec(naked) void ReturnTraffic()
{
    __asm { pop eax }
    __asm
    {
        ret
    }
}
void TestNativeTransitions()
{
    using namespace DistantLightNativeTransitions;
    firstReturn = updateReturn = reinterpret_cast<std::uintptr_t>(&ReturnPlain);
    normalReturn = reinterpret_cast<std::uintptr_t>(&ReturnNormal);
    trafficReturn = reinterpret_cast<std::uintptr_t>(&ReturnTraffic);
    auto          first = &First;
    auto          normal = &Normal;
    auto          update = &Update;
    auto          traffic = &Traffic;
    std::uint32_t effect[6] = {0, 0, 0, 0, 0, 0x42F60000};  // Native range = 123
    std::uint32_t result = 0, stackBefore = 0, stackAfter = 0;
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        enabled = iteration % 2 != 0;
        __asm { mov stackBefore, esp }
        __asm
        {
            lea esi, effect
        }
        __asm { call first }
        __asm {mov result, eax} Check(result == (enabled ? 0x43960000u : 0x42F60000u), "native first branch range");
        __asm { lea esi, effect }
        __asm
        {
            call normal
        }
        __asm { mov result, ecx }
        Check(result == (enabled ? 0x43960000u : 0x42F60000u), "native normal branch range");
        __asm { lea esi, effect }
        __asm
        {
            call update
        }
        __asm { mov result, ecx }
        Check(result == (enabled ? 0x43960000u : 0x42F60000u), "native update branch range");
        __asm { call traffic }
        __asm
        {
            mov result, eax
        }
        __asm { mov stackAfter, esp }
        Check(result == (enabled ? 0x44098000u : 0x42480000u), "native traffic range");
        Check(stackBefore == stackAfter, "native adapters balance stack");
        Check(effect[5] == 0x42F60000u, "native model data unchanged");
    }
}
#endif

int main(int argc, char** argv)
{
    try
    {
#ifdef _WIN32
        static_assert(sizeof(void*) == 4, "Run the harness as Win32, like Game SA");
#endif
        using namespace DistantLights;
        Definition<Vector> definition;
        Check(Parse("255 128 0 255 1 2 3 0.45 300 02 2 1", definition), "DAT parse");
        Check(definition.noDistance == 2 && definition.drawSearchlight && definition.flashType == 20 && definition.trafficLight, "DAT semantics");
        Check(Parse("255 128 0 255 1 2 3 0.45 02 3 0", definition) && definition.drawDistance == 0 && definition.noDistance == 3, "legacy row");
        const char* bad[] = {"255 000 000 0.091000000000 0.05300000 24.6200000 0.10 300.0 00 2 0",
                             "256 0 0 255 0 0 0 1 300 00 0 0",
                             "255 0 0 255 nan 0 0 1 300 00 0 0",
                             "255 0 0 255 0 0 0 -1 300 00 0 0",
                             "255 0 0 255 0 0 0 1 300 00 9 0",
                             ""};
        for (auto row : bad)
            Check(!Parse(row, definition), "reject invalid DAT");
        Check(argc == 2, "DAT argument required");
        std::ifstream dat(argv[1]);
        Check(bool(dat), "open shipped DAT");
        int         accepted = 0, rejected = 0, points = 0, cones = 0;
        std::string row;
        while (std::getline(dat, row))
        {
            const auto first = row.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || row[first] == '%' || row[first] == '#')
                continue;
            if (!Parse(row.c_str(), definition))
            {
                ++rejected;
                continue;
            }
            ++accepted;
            points += definition.noDistance >= 2;
            cones += definition.drawSearchlight;
        }
        std::cout << "DAT: " << accepted << " accepted, " << rejected << " rejected, " << points << " point rows, " << cones << " cone rows\n";
        Check(accepted == 1050 && rejected == 1 && points == 124 && cones == 24, "shipped DAT inventory");
        Check(GetNightAlpha(0, 0) == 255 && GetNightAlpha(20, 0) == 30, "night clock");
        Check(IsDistantLightOn(20, 0, 999) && !IsDistantLightOn(20, 0, 1000) && IsDistantLightOn(20, 0, 2000), "blink boundaries");
        SDistantLightSettings settings;
        Check(settings.IsValid() && settings.automaticDistance, "default auto settings");
        Check(Near(ResolveRange(true, 600, 2000), 600) && Near(ResolveRange(true, 4500, 2000), 4500), "auto follows resolved game range");
        Check(Near(ResolveRange(false, 600, 2500), 2500), "manual range independent of weather");
        Check(Near(ResolveRange(true, std::numeric_limits<float>::quiet_NaN(), 2000), 2000) && Near(ResolveRange(true, 0, 2000), 2000),
              "invalid game range fallback");
        settings.reachFullAlpha = 0;
        Check(!settings.IsValid(), "reject zero curve denominator");
        settings = {};
        settings.nearAlpha = std::numeric_limits<float>::infinity();
        Check(!settings.IsValid(), "reject nonfinite curve");
        settings = {};
        auto fixture = Evaluate(300, 300, 2000, 1, .25f, 0, 255, 255, true, settings);
        Check(Near(fixture.radius, .49143836f) && Near(fixture.intensity, .6f), "upstream default curve fixture");
        settings.growWithDistance = false;
        fixture = Evaluate(300, 300, 2000, 1, .25f, 0, 255, 255, true, settings);
        Check(Near(fixture.radius, .4375f), "growth disabled");
        settings.nearAlpha = .2f;
        settings.reachFullAlpha = 300;
        fixture = Evaluate(300, 300, 2000, 1, .25f, 0, 255, 255, true, settings);
        Check(Near(fixture.intensity, .28f), "custom near curve");
        settings.boostStart = 0;
        settings.farAlphaBoost = 2;
        fixture = Evaluate(300, 300, 2000, 1, .25f, 0, 255, 255, true, settings);
        Check(Near(fixture.intensity, 1.0333333f), "custom far boost");
        settings = {};
        fixture = Evaluate(290, 300, 300, 1, .25f, 2, 255, 255, true, settings);
        Check(Near(fixture.intensity, .5666667f), "no-distance uses model reference without extra far fade");
        for (float range : {200.0f, 600.0f, 2000.0f, 5000.0f})
        {
            fixture = Evaluate(range, 100, range, 1, .25f, 0, 255, 255, true, settings);
            Check(fixture.alpha == 0, "effective far boundary");
        }
#ifdef _WIN32
        TestNativeTransitions();
#endif
        auto near = Evaluate(269, 300, 2000, 1, .25f, 0, 255, 255, true);
        auto far = Evaluate(2000, 300, 2000, 1, .25f, 0, 255, 255, true);
        auto visible = Evaluate(300, 300, 2000, 1, .25f, 0, 255, 255, true);
        Check(near.alpha == 0 && far.alpha == 0 && visible.alpha > 0, "corona distance window");
        Check(Evaluate(300, 300, 2000, 1, .25f, 0, 255, 255, false).alpha == 0, "blink suppresses output");
        PointLight point;
        Check(MakePointLight(2, 22, 1, 300, 2, 255, 128, 0, point), "point boundary");
        Check(point.type == 0 && Near(point.radius, 10) && Near(point.red, 2), "native point type and unclamped color");
        Check(!MakePointLight(2, 22.01f, 1, 300, 1, 255, 255, 255, point), "point far cutoff");
        Check(!MakePointLight(1, 1, 1, 300, 1, 255, 255, 255, point), "no-distance isn't point");
        Check(MakePointLight(3, 1, 1, 5, 1, 255, 255, 255, point) && point.type == 1 && Near(point.radius, 5), "radius override");
        SubmitPointLight(&PointSpy, Vector{10, 20, 30}, point);
        Check(pointSubmitted, "point native submission");
        for (unsigned type = 2; type <= 7; ++type)
            Check(MakePointLight(type, 1, 1, 300, 1, 255, 255, 255, point) && point.type == int(type - 2), "all native point types");
        std::map<std::uint32_t, void*> states;
        for (std::uint32_t i = 0; i <= 30; ++i)
            states[i] = reinterpret_cast<void*>(std::uintptr_t(i + 100));
        const auto originalStates = states;
        auto       get = [&](std::uint32_t id, void* out)
        {
            *static_cast<void**>(out) = states[id];
            return true;
        };
        auto set = [&](std::uint32_t id, void* value) { states[id] = value; };
        Check(WithConeStates(get, set,
                             [&]()
                             {
                                 for (auto id : {1, 6, 7, 8, 10, 11, 12, 14, 20, 29, 30})
                                     states[id] = nullptr;
                             }),
              "cone render pass");
        Check(states == originalStates, "render states restored");
        bool rendered = false;
        Check(!WithConeStates([](auto, auto) { return false; }, set, [&]() { rendered = true; }) && !rendered && states == originalStates,
              "failed state capture skips renderer");
        Check(!MakePointLight(2, 1, 1, 0, 1, 255, 255, 255, point), "reject zero radius");
        Cone cone;
        Check(!MakeCone(45, 20, 1, true, cone) && !MakeCone(300, 20, 1, true, cone), "cone distance boundaries");
        Check(MakeCone(100, 20, 1.2f, true, cone) && Near(cone.length, 23) && Near(cone.startRadius, .2f) && Near(cone.endRadius, 20.854f) &&
                  Near(cone.intensity, .4f),
              "cone dimensions");
        Check(MakeCone(55, 20, 1, true, cone) && Near(cone.intensity, .2f), "cone fade");
        Check(!MakeCone(100, 0, 1, true, cone) && !MakeCone(100, 20, 1, false, cone), "cone invalid or off");
        Source source{{10, 20, 30}, {0, 0, std::sqrt(.5f), std::sqrt(.5f)}, 42};
        auto   world = Transform(source, Vector{2, 0, 1});
        Check(Near(world.fX, 10) && Near(world.fY, 22) && Near(world.fZ, 31), "quaternion placement");
        source.rotation = {0, 0, 0, 0};
        world = Transform(source, Vector{2, 0, 1});
        Check(Near(world.fX, 12) && Near(world.fZ, 31), "zero quaternion fallback");
        Sources<Source> sources;
        sources.Remember(source);
        sources.Remember(source);
        Check(sources.size() == 1, "streaming deduplication");
        std::vector<Vector> catalogue;
        for (int rebuild = 0; rebuild < 3; ++rebuild)
        {
            catalogue.clear();
            sources.Replay([&](const Source& value) { catalogue.push_back(Transform(value, Vector{float(rebuild), 0, 0})); });
            Check(catalogue.size() == 1 && Near(catalogue[0].fX, 10.0f + rebuild), "replay with updated definitions");
        }
        sources.clear();
        Check(sources.empty(), "world teardown");
        std::vector<Candidate> candidates;
        for (int i = 30000; i > 0; --i)
            candidates.push_back({float(i), i});
        KeepNearest(candidates, 25000);
        Check(candidates.size() == 25000 && std::all_of(candidates.begin(), candidates.end(), [](auto v) { return v.id <= 25000; }),
              "corona budget picks nearest");
        KeepNearest(candidates, 32);
        Check(candidates.size() == 32, "cone budget");
        KeepNearest(candidates, 0);
        Check(candidates.empty(), "zero budget");
        std::cout << "PASS: " << checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
