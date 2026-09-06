/*****************************************************************************
 * PROJECT: Multi Theft Auto - resource-owned native GTA interfaces
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class CControllerState;
using NativeUIHandle = std::uint32_t;

enum class ENativeUIKind
{
    Text,
    Clock,
    Counter,
    Menu,
    Grid,
    DrawText,
    Window,
    Rectangle,
    Card
};

// Values cross the Game SA ABI, never native pointers, SCM offsets or globals.
struct SNativeUIOptions
{
    NativeUIHandle              text{};
    std::string                 name;
    std::string                 content;
    bool                        gxt{};
    float                       x{40}, y{100}, width{260}, height{80};
    float                       scaleX{0.48f}, scaleY{1.12f};
    std::uint32_t               color{0xFFFFFFFF}, background{0x000000BE}, dropColor{0x000000FF};  // RGBA
    int                         font{1}, alignment{}, shadow{2}, outline{}, style{}, selected{};
    double                      value{}, maximum{100};
    bool                        visible{true}, beforeFade{true}, paused{}, countdown{true}, flash{}, proportional{true};
    unsigned int                beepSeconds{}, columns{1}, card{53};
    std::array<int, 2>          numbers{{-1, -1}};
    std::vector<NativeUIHandle> cells;  // row-major; every column has the same row count
    std::vector<NativeUIHandle> headers;
    std::vector<bool>           enabled;
    std::vector<float>          widths;
    std::vector<int>            alignments;
};

struct SNativeUIState
{
    ENativeUIKind kind{};
    double        value{};
    int           selected{-1}, accepted{-1}, color{-1};
    bool          cancelled{}, visible{}, paused{}, finished{}, available{true};
};

struct SNativeUIEvent
{
    void*          owner{};
    NativeUIHandle handle{};
    std::string    action;
    int            selection{-1}, color{-1};
};

class CNativeUI
{
public:
    virtual ~CNativeUI() = default;
    virtual NativeUIHandle Create(void* owner, ENativeUIKind kind, const SNativeUIOptions& options, std::string& error) = 0;
    virtual bool           Update(void* owner, NativeUIHandle handle, const SNativeUIOptions& options, std::string& error) = 0;
    virtual bool           GetOptions(void* owner, NativeUIHandle handle, SNativeUIOptions& options, ENativeUIKind& kind) const = 0;
    virtual bool           GetState(void* owner, NativeUIHandle handle, SNativeUIState& state) = 0;
    virtual bool           Destroy(void* owner, NativeUIHandle handle, std::string& error) = 0;
    virtual void           Release(void* owner) = 0;
    virtual bool Show(void* owner, NativeUIHandle text, const std::string& channel, unsigned int duration, int style, const std::array<int, 2>& numbers,
                      std::string& error) = 0;
    virtual bool Clear(void* owner, const std::string& channel) = 0;
    virtual bool HasMessages() const = 0;
    virtual void Pulse(bool inputBlocked) = 0;
    // Consume a frame's effective binds, without changing saved preferences or
    // retaining a control-disable flag across resource/error lifetimes.
    virtual bool PollEvent(SNativeUIEvent& event) = 0;
    virtual bool CaptureInput(const CControllerState& state, bool blocked) = 0;
};
