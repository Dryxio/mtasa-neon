/*****************************************************************************
 * PROJECT: Multi Theft Auto - bounded resource-owned GTA UI adapters
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#include "StdInc.h"
#include "CNativeUISA.h"
#include "CGameSA.h"
#include "CFontSA.h"
#include "CRenderWareSA.h"
#include <game/CHud.h>
#include <cstring>
#include <cstdio>

extern CGameSA* pGame;

namespace
{
    CNativeUISA* instance{};
    bool         customDraw{};

    template <typename T>
    T& At(std::uintptr_t p)
    {
        return *reinterpret_cast<T*>(p);
    }
    struct RenderStateScope
    {
        static constexpr int           states[]{1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12};
        std::array<std::uintptr_t, 11> values{};
        std::array<bool, 11>           valid{};
        std::uintptr_t                 engine{At<std::uintptr_t>(0xC97B24)};
        RenderStateScope()
        {
            // Retail CSprite2d calls the RW device at +0x20; +0x24 is its
            // paired getter. Restore texture/blend/depth states as well as font.
            const auto get = At<int(__cdecl*)(int, void*)>(engine + 0x24);
            for (std::size_t i = 0; i < values.size(); ++i)
                valid[i] = get(states[i], &values[i]) != 0;
        }
        ~RenderStateScope()
        {
            const auto set = At<int(__cdecl*)(int, void*)>(engine + 0x20);
            for (std::size_t i = 0; i < values.size(); ++i)
                if (valid[i])
                    set(states[i], reinterpret_cast<void*>(values[i]));
        }
    };
    struct FontScope
    {
        RenderStateScope                renderer;
        std::array<unsigned char, 0x3C> saved;
        FontScope()
        {
            reinterpret_cast<void(__cdecl*)()>(0x719840)();
            std::memcpy(saved.data(), reinterpret_cast<void*>(0xC71A60), saved.size());
        }
        ~FontScope()
        {
            reinterpret_cast<void(__cdecl*)()>(0x719840)();
            std::memcpy(reinterpret_cast<void*>(0xC71A60), saved.data(), saved.size());
        }
    };
    struct Menu
    {
        unsigned char colors[64], type;
        char          cells[4][12][10];
        int           number1[4][12], number2[4][12];
        char          headers[4][10], title[10];
        bool          enabled[12];
        unsigned char bought[12];
        signed char   alignment[4], headerAlignment[4];
        unsigned char rows, columns;
        bool          interactive[4];
        float         widths[4], x, y;
        bool          background;
        signed char   selected, accepted;
    };
    static_assert(sizeof(Menu) == 0x418, "retail menu layout");
    struct MenuGlobals
    {
        Menu*         pointers[2];
        bool          used[2];
        signed char   current;
        unsigned char count;
    };
    static_assert(sizeof(MenuGlobals) == 12, "retail menu globals");
    struct MenuScope
    {
        MenuGlobals saved;
        MenuScope() : saved(At<MenuGlobals>(0xBA82D8)) { At<MenuGlobals>(0xBA82D8) = {}; }
        ~MenuScope() { At<MenuGlobals>(0xBA82D8) = saved; }
        void Set(Menu* menu, int slot)
        {
            auto& g = At<MenuGlobals>(0xBA82D8);
            g.pointers[slot] = menu;
            g.used[slot] = true;
            g.current = static_cast<signed char>(slot);
            g.count = 1;
        }
    };
    struct ScriptText
    {
        float         sx, sy;
        std::uint32_t color;
        bool          justify, center, background, textOnly;
        float         wrap, centerSize;
        std::uint32_t bg;
        bool          proportional;
        unsigned char drop[4];
        signed char   shadow, edge;
        bool          before, right;
        int           font;
        float         x, y;
        char          key[8];
        int           n1, n2;
    };
    static_assert(sizeof(ScriptText) == 0x44, "retail script text layout");
    struct Clock
    {
        std::uint32_t var;
        char          key[10], display[42];
        bool          enabled;
        unsigned char direction;
        std::uint32_t beep;
    };
    struct Counter
    {
        std::uint32_t var, maxVar;
        char          key[10];
        std::uint16_t type;
        char          display[42];
        bool          enabled, flash;
        std::uint32_t color;
    };
    struct Timers
    {
        Clock   clock;
        Counter counters[4];
        bool    display, paused;
    };
    static_assert(sizeof(Clock) == 0x40 && sizeof(Counter) == 0x44 && sizeof(Timers) == 0x154, "retail timers layout");
    struct MessageRecord
    {
        const char*    text;
        unsigned short flags;
        unsigned int   duration, created;
        int            numbers[6];
        const char*    string;
        unsigned char  previous;
    };
    struct Brief
    {
        const char* text;
        int         numbers[6];
        const char* string;
    };
    static_assert(sizeof(MessageRecord) == 0x30 && sizeof(Brief) == 0x20, "retail message pointers");

    std::uint32_t NativeColor(std::uint32_t c)
    {
        return ((c & 255) << 24) | ((c & 0xFF00) << 8) | ((c & 0xFF0000) >> 8) | ((c >> 24) & 255);
    }
    std::string Key(NativeUIHandle id)
    {
        char key[8]{};
        if (id)
            snprintf(key, sizeof(key), "N%06X", id);
        return key;
    }
    const char* __fastcall TextHook(void* self, void*, const char* key)
    {
        if (instance)
            if (const char* value = instance->Resolve(key))
                return value;
        return reinterpret_cast<const char*(__thiscall*)(void*, const char*)>(0x6A0050)(self, key);
    }
    void __cdecl ScriptRectHook(bool before)
    {
        if (!customDraw)
            reinterpret_cast<void(__cdecl*)(bool)>(0x464980)(before);
    }
    void __cdecl ScriptHook(bool before)
    {
        reinterpret_cast<void(__cdecl*)(bool)>(0x58C080)(before);
        if (instance)
            instance->Draw(before);
    }
    void __cdecl TimerHook()
    {
        if (instance)
            instance->DrawTimers();
        else
            reinterpret_cast<void(__cdecl*)()>(0x58B180)();
    }

    std::string BoundedText(const std::string& source, const std::array<int, 2>& numbers)
    {
        const auto numbered = NativeUI::Numbers(source, numbers);
        if (numbered.size() > 300)
            return {};
        std::array<char, 2048> scratch{};
        std::strcpy(scratch.data(), numbered.c_str());
        reinterpret_cast<void(__cdecl*)(char*)>(0x69E160)(scratch.data());
        return std::strlen(scratch.data()) <= 300 ? std::string(scratch.data()) : std::string{};
    }

    bool IsCall(std::uintptr_t site, std::uintptr_t target)
    {
        return At<unsigned char>(site) == 0xE8 && site + 5 + At<int>(site + 1) == target;
    }
}

CNativeUISA::CNativeUISA()
{
    // Validate every callsite before installing any hook: a changed executable
    // must fail closed instead of partly enabling a different native ABI.
    const std::pair<unsigned int, unsigned int> calls[] = {{0x58FCE4, 0x58C080}, {0x58D552, 0x58C080}, {0x58FBEE, 0x58B180}, {0x58C092, 0x464980},
                                                           {0x5810BD, 0x6A0050}, {0x581122, 0x6A0050}, {0x581191, 0x6A0050}, {0x58143E, 0x6A0050},
                                                           {0x58B3F1, 0x6A0050}, {0x58B673, 0x6A0050}, {0x573FC7, 0x6A0050}, {0x58C1B3, 0x6A0050}};
    for (const auto& call : calls)
        if (!IsCall(call.first, call.second))
            return;
    instance = this;
    for (const auto& call : calls)
    {
        const auto target = call.second == 0x58C080   ? reinterpret_cast<DWORD>(&ScriptHook)
                            : call.second == 0x58B180 ? reinterpret_cast<DWORD>(&TimerHook)
                            : call.second == 0x464980 ? reinterpret_cast<DWORD>(&ScriptRectHook)
                                                      : reinterpret_cast<DWORD>(&TextHook);
        HookInstallCall(call.first, target);
    }
    m_ready = true;
}
CNativeUISA::~CNativeUISA()
{
    while (!m_model.entries.empty())
        Release(m_model.entries.begin()->second.owner);
    UnloadCards();
    instance = nullptr;  // Hooks retain their stock fallback during Game SA teardown.
}

bool CNativeUISA::LoadText(NativeUI::Entry& e, std::string& error)
{
    if (!e.options.gxt)
        return NativeUI::Encode(e.options.content, e.encoded);
    const char* text = reinterpret_cast<const char*(__thiscall*)(void*, const char*)>(0x6A0050)(reinterpret_cast<void*>(0xC1B340), e.options.content.c_str());
    const auto  size = text ? strnlen(text, NativeUI::MaxTextBytes + 1) : 0;
    if (!size || size > NativeUI::MaxTextBytes)
    {
        error = "gxt-missing-or-too-long";
        return false;
    }
    e.encoded.assign(text, size);
    if (!NativeUI::Tokens(e.encoded))
    {
        error = "unsupported-gxt-token";
        return false;
    }
    return true;
}
NativeUIHandle CNativeUISA::Create(void* owner, ENativeUIKind kind, const SNativeUIOptions& o, std::string& error)
{
    if (!m_ready)
    {
        error = "unsupported-native-hooks";
        return 0;
    }
    const auto id = m_model.Insert(owner, kind, o, GetTickCount64_(), error);
    if (!id)
        return 0;
    auto& e = m_model.entries.at(id);
    if (kind == ENativeUIKind::Card && !LoadCards(error))
    {
        m_model.entries.erase(id);
        return 0;
    }
    if (kind == ENativeUIKind::Text && !LoadText(e, error))
    {
        m_model.entries.erase(id);
        return 0;
    }
    if (kind == ENativeUIKind::Clock || kind == ENativeUIKind::Counter)
    {
        const auto& native = At<Timers>(0xBA1788);
        for (int slot = 0; slot < (kind == ENativeUIKind::Clock ? 1 : 4); ++slot)
        {
            if (kind == ENativeUIKind::Clock ? native.clock.var != 0 : native.counters[slot].var != 0)
                continue;
            bool used = false;
            for (const auto& item : m_model.entries)
                if (item.first != id && item.second.kind == kind && item.second.slot == slot)
                    used = true;
            if (!used)
            {
                e.slot = slot;
                break;
            }
        }
        if (e.slot < 0)
        {
            error = "native-capacity";
            m_model.entries.erase(id);
            return 0;
        }
    }
    if ((kind == ENativeUIKind::Menu || kind == ENativeUIKind::Grid) && !MakeMenu(id, e, error))
    {
        m_model.entries.erase(id);
        return 0;
    }
    return id;
}
bool CNativeUISA::GetOptions(void* owner, NativeUIHandle id, SNativeUIOptions& o, ENativeUIKind& kind) const
{
    const auto i = m_model.entries.find(id);
    if (i == m_model.entries.end() || i->second.owner != owner)
        return false;
    o = i->second.options;
    kind = i->second.kind;
    return true;
}
bool CNativeUISA::Update(void* owner, NativeUIHandle id, const SNativeUIOptions& o, std::string& error)
{
    auto* e = m_model.Find(owner, id);
    if (!e)
    {
        error = "invalid-handle";
        return false;
    }
    if (!m_model.Validate(owner, e->kind, o, error))
        return false;
    NativeUI::Entry updated = *e;
    updated.options = o;
    updated.tick = GetTickCount64_();
    updated.finished = e->finished && o.value == e->options.value;
    if (e->kind == ENativeUIKind::Text)
    {
        if (o.name != e->options.name)
        {
            error = "immutable-name";
            return false;
        }
        if (!LoadText(updated, error))
            return false;
        // Update cancels active presentations of this text before replacing its
        // storage. Menu/draw handles resolve the new value on the next frame.
        for (auto i = m_messages.begin(); i != m_messages.end();)
            if (i->second.owner == owner && i->second.text == id)
            {
                ClearMessage(i->second, i->first);
                i = m_messages.erase(i);
            }
            else
                ++i;
    }
    if (e->kind == ENativeUIKind::Menu || e->kind == ENativeUIKind::Grid)
    {
        // Build first, so invalid or occupied native state cannot destroy the
        // previous valid menu. The temporary globals never escape this call.
        if (!MakeMenu(id, updated, error))
            return false;
        updated.accepted = -1;
        updated.cancelled = false;
        m_focus = 0;
        m_events.erase(std::remove_if(m_events.begin(), m_events.end(), [&](const SNativeUIEvent& event) { return event.handle == id; }), m_events.end());
    }
    *e = std::move(updated);
    return true;
}
bool CNativeUISA::GetState(void* owner, NativeUIHandle id, SNativeUIState& state)
{
    auto* e = m_model.Find(owner, id);
    if (!e)
        return false;
    state = {};
    state.kind = e->kind;
    state.value = e->options.value;
    state.visible = e->options.visible;
    state.paused = e->options.paused;
    state.finished = e->finished;
    if (e->kind == ENativeUIKind::Clock)
        state.available = At<Timers>(0xBA1788).clock.var == 0;
    if (e->kind == ENativeUIKind::Counter)
        state.available = At<Timers>(0xBA1788).counters[e->slot].var == 0;
    if (e->kind == ENativeUIKind::Menu || e->kind == ENativeUIKind::Grid)
        state.available = !At<MenuGlobals>(0xBA82D8).used[e->slot];
    if (m_menus.count(id))
    {
        auto* menu = static_cast<Menu*>(m_menus.at(id));
        state.selected = menu->selected;
        state.accepted = e->accepted;
        state.cancelled = e->cancelled;
        if (e->kind == ENativeUIKind::Grid && state.selected >= 0 && state.selected < 64)
            state.color = menu->colors[state.selected];
        e->accepted = -1;
        e->cancelled = false;
    }
    return true;
}
bool CNativeUISA::Destroy(void* owner, NativeUIHandle id, std::string& error)
{
    auto* e = m_model.Find(owner, id);
    if (!e)
    {
        error = "invalid-handle";
        return false;
    }
    if (e->kind == ENativeUIKind::Text && m_model.Referenced(owner, id))
    {
        error = "text-in-use";
        return false;
    }
    for (auto i = m_messages.begin(); i != m_messages.end();)
        if (i->second.owner == owner && i->second.text == id)
        {
            ClearMessage(i->second, i->first);
            i = m_messages.erase(i);
        }
        else
            ++i;
    FreeMenu(id);
    m_model.entries.erase(id);
    if (!m_model.Count(ENativeUIKind::Card))
        UnloadCards();
    if (m_focus == id)
        m_focus = 0;
    return true;
}
void CNativeUISA::Release(void* owner)
{
    Clear(owner, "all");
    m_events.erase(std::remove_if(m_events.begin(), m_events.end(), [&](const SNativeUIEvent& event) { return event.owner == owner; }), m_events.end());
    for (auto i = m_model.entries.begin(); i != m_model.entries.end();)
        if (i->second.owner == owner)
        {
            FreeMenu(i->first);
            if (m_focus == i->first)
                m_focus = 0;
            i = m_model.entries.erase(i);
        }
        else
            ++i;
    if (!m_model.Count(ENativeUIKind::Card))
        UnloadCards();
}

void CNativeUISA::ClearMessage(Message& msg, const std::string& channel)
{
    // Match pointers, not text contents: another resource/GTA may display the
    // same sentence. Retail ClearThisPrint compares contents and can clear it.
    auto&      smallMessages = At<std::array<MessageRecord, 8>>(0xC1A7F0);
    auto&      big = At<std::array<MessageRecord, 7>>(0xC1A970);
    const bool ownsSmall = smallMessages.front().text == msg.bytes.c_str() ||
                           (!smallMessages.front().text && std::strcmp(reinterpret_cast<const char*>(0xBAB040), msg.bytes.c_str()) == 0);
    const bool ownsBig = big[msg.style].text == msg.bytes.c_str() ||
                         (!big[msg.style].text && std::strcmp(reinterpret_cast<const char*>(0xBAACC0 + msg.style * 128), msg.bytes.c_str()) == 0);
    for (auto i = smallMessages.begin(); i != smallMessages.end();)
        if (i->text == msg.bytes.c_str())
        {
            std::move(i + 1, smallMessages.end(), i);
            smallMessages.back() = {};
        }
        else
            ++i;
    for (auto& b : big)
        if (b.text == msg.bytes.c_str())
            b = {};
    auto& briefs = At<std::array<Brief, 20>>(0xC1A570);
    for (auto i = briefs.begin(); i != briefs.end();)
        if (i->text == msg.bytes.c_str() || i->string == msg.bytes.c_str())
        {
            std::move(i + 1, briefs.end(), i);
            briefs.back() = {};
        }
        else
            ++i;
    if (channel == "help" && std::strcmp(reinterpret_cast<const char*>(0xBAA7A0), msg.bytes.c_str()) == 0)
    {
        reinterpret_cast<void(__cdecl*)(const char*, bool, bool, bool)>(0x588BE0)(nullptr, true, false, false);
        // SetHelpMessage can return early during cutscenes/big messages. If it
        // did, detach only our copied help buffers so stop cannot resurrect it.
        if (std::strcmp(reinterpret_cast<const char*>(0xBAA7A0), msg.bytes.c_str()) == 0)
        {
            for (const auto address : {0xBAA7A0u, 0xBAA480u, 0xBAA610u})
                if (std::strcmp(reinterpret_cast<const char*>(address), msg.bytes.c_str()) == 0)
                    At<char>(address) = 0;
            At<int>(0xBAA474) = 0;
            At<unsigned int>(0xBAA478) = 0;
            At<unsigned int>(0xBAA47C) = 0;
            At<bool>(0xBAA464) = false;
        }
        if (pGame->GetHud()->IsComponentVisible(HUD_HELP_TEXT))
            pGame->GetHud()->SetComponentVisible(HUD_HELP_TEXT, m_helpWasVisible);
    }
    // HUD stores copies of queue text, so clearing an owned channel also drops
    // the corresponding already-presented copy without touching other styles.
    if ((channel == "objective" || channel == "dialogue") && ownsSmall)
        At<char>(0xBAB040) = 0;
    if (channel == "big" && ownsBig)
    {
        // Use the same per-style HUD reset as CLEAR_PRINT_BIG_NOW after the
        // pointer-owned queue has been removed (never a global message clear).
        At<float>(0xBAA3DC + msg.style * sizeof(float)) = 0;
        At<char>(0xBAACC0 + msg.style * 128) = 0;
    }
}
bool CNativeUISA::Show(void* owner, NativeUIHandle id, const std::string& channel, unsigned int duration, int style, const std::array<int, 2>& numbers,
                       std::string& error)
{
    auto* e = m_model.Find(owner, id);
    if (!e || e->kind != ENativeUIKind::Text)
    {
        error = "invalid-text-handle";
        return false;
    }
    if (channel != "objective" && channel != "dialogue" && channel != "help" && channel != "big")
    {
        error = "invalid-channel";
        return false;
    }
    if (!duration || duration > 600000 || style < 1 || style > 7)
    {
        error = "invalid-message-options";
        return false;
    }
    // One resource leases the mission presentation family at a time, shared
    // with the legacy GXT lease by the Lua boundary. No cross-owner eviction.
    for (const auto& item : m_messages)
        if (item.second.owner != owner)
        {
            error = "channel-busy";
            return false;
        }
    Message message;
    message.owner = owner;
    message.text = id;
    message.style = style - 1;
    message.expires = GetTickCount64_() + duration;
    message.bytes = NativeUI::Numbers(e->encoded, numbers);
    if (channel == "objective" && message.bytes.compare(0, 3, "~z~") == 0)
        message.bytes.erase(0, 3);
    if (channel == "dialogue" && message.bytes.compare(0, 3, "~z~") != 0)
        message.bytes.insert(0, "~z~");
    // Resolve effective binds into a generously sized private buffer before
    // any GTA 400-byte/128-byte destination sees the result.
    std::array<char, 2048> expanded{};
    if (message.bytes.size() < 400)
    {
        std::strcpy(expanded.data(), message.bytes.c_str());
        reinterpret_cast<void(__cdecl*)(char*)>(0x69E160)(expanded.data());
        message.bytes = expanded.data();
    }
    if (message.bytes.size() > (channel == "big" ? 120u : 300u))
    {
        error = "expanded-text-too-long";
        return false;
    }
    if (channel == "objective" || channel == "dialogue")
    {
        Clear(owner, "objective");
        Clear(owner, "dialogue");
    }
    else
        Clear(owner, channel);
    if (channel == "dialogue" && !At<bool>(0xBA678C))
        return true;
    if ((channel == "objective" || channel == "dialogue") && At<std::array<MessageRecord, 8>>(0xC1A7F0).front().text)
    {
        error = "native-channel-busy";
        return false;
    }
    if (channel == "big" && At<std::array<MessageRecord, 7>>(0xC1A970)[style - 1].text)
    {
        error = "native-channel-busy";
        return false;
    }
    if (channel == "help" && At<char>(0xBAA7A0))
    {
        error = "native-channel-busy";
        return false;
    }
    auto& stored = m_messages.emplace(channel, std::move(message)).first->second;
    if (channel == "help")
    {
        m_helpWasVisible = pGame->GetHud()->IsComponentVisible(HUD_HELP_TEXT);
        pGame->GetHud()->SetComponentVisible(HUD_HELP_TEXT, true);
        reinterpret_cast<void(__cdecl*)(const char*, bool, bool, bool)>(0x588BE0)(stored.bytes.c_str(), false, true, false);
        if (std::strcmp(reinterpret_cast<const char*>(0xBAA7A0), stored.bytes.c_str()) != 0)
        {
            pGame->GetHud()->SetComponentVisible(HUD_HELP_TEXT, m_helpWasVisible);
            m_messages.erase(channel);
            error = "native-message-unavailable";
            return false;
        }
    }
    else if (channel == "big")
        reinterpret_cast<void(__cdecl*)(const char*, unsigned int, unsigned int)>(0x69F2B0)(stored.bytes.c_str(), duration, style - 1);
    else
        reinterpret_cast<void(__cdecl*)(const char*, unsigned int, unsigned short, bool)>(0x69F1E0)(stored.bytes.c_str(), duration, 1, false);
    return true;
}
bool CNativeUISA::Clear(void* owner, const std::string& channel)
{
    if (channel != "all" && channel != "objective" && channel != "dialogue" && channel != "help" && channel != "big")
        return false;
    for (auto i = m_messages.begin(); i != m_messages.end();)
        if (i->second.owner == owner && (channel == "all" || i->first == channel))
        {
            ClearMessage(i->second, i->first);
            i = m_messages.erase(i);
        }
        else
            ++i;
    return true;
}
bool CNativeUISA::HasMessages() const
{
    return !m_messages.empty();
}
void CNativeUISA::Pulse(bool blocked)
{
    m_inputBlocked = blocked;
    const auto now = GetTickCount64_();
    for (auto i = m_messages.begin(); i != m_messages.end();)
        if (now >= i->second.expires)
        {
            ClearMessage(i->second, i->first);
            i = m_messages.erase(i);
        }
        else
            ++i;
    for (auto& item : m_model.entries)
    {
        if (NativeUI::Model::Tick(item.second, now) && !At<bool>(0xB6F065))
            reinterpret_cast<void(__thiscall*)(void*, int, float, float)>(0x506EA0)(reinterpret_cast<void*>(0xB6BC90), 0x21, 0.0f, 1.0f);
        if (m_events.size() < 256 && NativeUI::Model::TakeCompletion(item.second))
            m_events.push_back({item.second.owner, item.first, "finished", -1, -1});
    }
}

bool CNativeUISA::MakeMenu(NativeUIHandle id, NativeUI::Entry& e, std::string& error)
{
    int         slot = e.slot;
    const auto& native = At<MenuGlobals>(0xBA82D8);
    if (slot < 0)
        for (int s = 0; s < 2; ++s)
        {
            bool used = native.used[s];
            for (const auto& item : m_model.entries)
                if (item.first != id && (item.second.kind == ENativeUIKind::Menu || item.second.kind == ENativeUIKind::Grid) && item.second.slot == s)
                    used = true;
            if (!used)
            {
                slot = s;
                break;
            }
        }
    if (slot < 0 || native.used[slot])
    {
        error = "native-capacity";
        return false;
    }
    const auto& o = e.options;
    Menu*       menu{};
    {
        MenuScope scope;
        // CreateNewMenu indexes by count, not first free slot. Run it in a
        // private empty view then attach the result only for native calls.
        const auto nativeId =
            reinterpret_cast<unsigned char(__cdecl*)(unsigned char, const char*, float, float, float, unsigned char, bool, bool, signed char)>(0x582300)(
                e.kind == ENativeUIKind::Grid ? 1 : 0, Key(o.text).c_str(), o.x, o.y, o.width, static_cast<unsigned char>(o.columns), true, true,
                static_cast<signed char>(o.alignment));
        menu = At<MenuGlobals>(0xBA82D8).pointers[nativeId];
    }
    if (!menu)
    {
        error = "allocation-failed";
        return false;
    }
    if (e.kind == ENativeUIKind::Menu)
    {
        menu->rows = static_cast<unsigned char>(o.cells.size() / o.columns);
        for (unsigned r = 0; r < menu->rows; ++r)
        {
            menu->enabled[r] = o.enabled[r];
            for (unsigned c = 0; c < o.columns; ++c)
            {
                std::strcpy(menu->cells[c][r], Key(o.cells[r * o.columns + c]).c_str());
                menu->number1[c][r] = o.numbers[0];
                menu->number2[c][r] = o.numbers[1];
            }
        }
        for (unsigned c = 0; c < o.columns; ++c)
        {
            if (c < o.headers.size())
                std::strcpy(menu->headers[c], Key(o.headers[c]).c_str());
            menu->widths[c] = c < o.widths.size() ? o.widths[c] : o.width;
            menu->alignment[c] = static_cast<signed char>(c < o.alignments.size() ? o.alignments[c] : o.alignment);
            menu->headerAlignment[c] = menu->alignment[c];
        }
    }
    menu->selected = static_cast<signed char>(o.selected);
    menu->accepted = -99;
    FreeMenu(id);
    m_menus[id] = menu;
    e.slot = slot;
    return true;
}
void CNativeUISA::FreeMenu(NativeUIHandle id)
{
    auto i = m_menus.find(id);
    if (i == m_menus.end())
        return;
    // Pair GTA's new with GTA's delete; the DLL CRT must never free this block.
    reinterpret_cast<void(__cdecl*)(void*)>(0x8214BD)(i->second);
    m_menus.erase(i);
}
void CNativeUISA::PrepareStrings()
{
    m_renderStrings.clear();
    for (const auto& item : m_model.entries)
        if (item.second.kind == ENativeUIKind::Text)
            m_renderStrings.emplace(item.first, BoundedText(item.second.encoded, {{-1, -1}}));
}
const char* CNativeUISA::Resolve(const char* key)
{
    if (!customDraw || !key || key[0] != 'N' || std::strlen(key) != 7)
        return nullptr;
    char*      end{};
    const auto id = std::strtoul(key + 1, &end, 16);
    if (*end)
        return nullptr;
    auto i = m_renderStrings.find(static_cast<NativeUIHandle>(id));
    return i == m_renderStrings.end() ? nullptr : i->second.c_str();
}
void CNativeUISA::DrawTimers()
{
    auto&      native = At<Timers>(0xBA1788);
    const auto saved = native;
    PrepareStrings();
    FontScope font;
    customDraw = true;
    for (const auto& item : m_model.entries)
    {
        const auto& e = item.second;
        const auto& o = e.options;
        if (!o.visible)
            continue;
        if (e.kind == ENativeUIKind::Clock && !native.clock.var)
        {
            native.clock.enabled = true;
            std::strcpy(native.clock.key, Key(o.text ? item.first : 0).c_str());
            if (o.text)
                m_renderStrings[item.first] = BoundedText(m_model.entries.at(o.text).encoded, o.numbers);
            const int seconds = static_cast<int>(o.value / 1000);
            snprintf(native.clock.display, sizeof(native.clock.display), "%02d:%02d", seconds / 60 % 100, seconds % 60);
            native.display = true;
        }
        if (e.kind == ENativeUIKind::Counter && e.slot >= 0 && !native.counters[e.slot].var)
        {
            auto& c = native.counters[e.slot];
            c.enabled = true;
            c.flash = o.flash;
            c.color = static_cast<unsigned short>(o.color);
            c.type = static_cast<unsigned short>(o.style);
            std::strcpy(c.key, Key(o.text ? item.first : 0).c_str());
            if (o.text)
                m_renderStrings[item.first] = BoundedText(m_model.entries.at(o.text).encoded, o.numbers);
            if (o.style == 2)
                snprintf(c.display, sizeof(c.display), "%d / %d", static_cast<int>(o.value), static_cast<int>(o.maximum));
            else
                snprintf(c.display, sizeof(c.display), "%d", static_cast<int>(o.style == 1 ? 100 * o.value / o.maximum : o.value));
            native.display = true;
        }
    }
    reinterpret_cast<void(__cdecl*)()>(0x58B180)();
    customDraw = false;
    native = saved;
}
void CNativeUISA::Draw(bool before)
{
    if (m_model.entries.empty())
        return;
    FontScope font;
    PrepareStrings();
    customDraw = true;
    const float sx = At<int>(0xC17044) * At<float>(0x859520), sy = At<int>(0xC17048) * At<float>(0x859524);
    for (const auto& item : m_model.entries)
    {
        const auto& e = item.second;
        const auto& o = e.options;
        if (!o.visible || o.beforeFade != before)
            continue;
        if (e.kind == ENativeUIKind::Window || e.kind == ENativeUIKind::Rectangle || e.kind == ENativeUIKind::Card)
        {
            // Window coordinates are pixel-space; its title offsets and font
            // use GTA's HUD multipliers internally. Convert only once here.
            const float rect[] = {o.x * sx, (o.y + o.height) * sy, (o.x + o.width) * sx, o.y * sy};
            const auto  color = NativeColor(o.background);
            if (o.text)
                m_renderStrings[item.first] = BoundedText(m_model.entries.at(o.text).encoded, o.numbers);
            if (e.kind == ENativeUIKind::Card)
            {
                void*      sprite = m_cardTextures[o.card - 1];
                const auto tint = NativeColor(o.color);
                reinterpret_cast<void(__thiscall*)(void*, const float*, const std::uint32_t*)>(0x728350)(&sprite, rect, &tint);
            }
            else if (e.kind == ENativeUIKind::Rectangle)
                reinterpret_cast<void(__cdecl*)(const float*, const std::uint32_t*)>(0x727B60)(rect, &color);
            else
                reinterpret_cast<void(__thiscall*)(void*, const float*, const char*, unsigned char, std::uint32_t, bool, bool)>(0x573EE0)(
                    reinterpret_cast<void*>(0xBA6748), rect, Key(item.first).c_str(), 0, color, false, true);
        }
    }
    auto&      texts = At<std::array<ScriptText, 96>>(0xA913E8);
    const auto saved = texts;
    texts = {};
    std::size_t index = 0;
    for (const auto& item : m_model.entries)
    {
        const auto& e = item.second;
        const auto& o = e.options;
        if (e.kind != ENativeUIKind::DrawText || !o.visible || o.beforeFade != before)
            continue;
        auto& t = texts[index++];
        t.sx = o.scaleX;
        t.sy = o.scaleY;
        t.color = NativeColor(o.color);
        t.center = o.alignment == 0;
        t.right = o.alignment == 2;
        t.wrap = o.x + o.width;
        t.centerSize = o.width;
        t.bg = NativeColor(o.background);
        t.proportional = o.proportional;
        const auto drop = NativeColor(o.dropColor);
        std::memcpy(t.drop, &drop, 4);
        t.shadow = static_cast<signed char>(o.shadow);
        t.edge = static_cast<signed char>(o.outline);
        t.before = before;
        t.font = o.font;
        t.x = o.x;
        t.y = o.y;
        std::strcpy(t.key, Key(item.first).c_str());
        t.n1 = o.numbers[0];
        t.n2 = o.numbers[1];
        // Retail substitutes repeated ~1~ tokens sequentially; normalize our
        // explicit ~1~/~2~ contract before entering its bounded text buffer.
        m_renderStrings[item.first] = BoundedText(m_model.entries.at(o.text).encoded, o.numbers);
    }
    if (index)
        reinterpret_cast<void(__cdecl*)(bool)>(0x58C080)(before);
    texts = saved;
    for (const auto& item : m_menus)
    {
        const auto& e = m_model.entries.at(item.first);
        if (!e.options.visible || e.options.beforeFade != before || At<MenuGlobals>(0xBA82D8).used[e.slot])
            continue;
        if (e.options.text)
            m_renderStrings[e.options.text] = BoundedText(m_model.entries.at(e.options.text).encoded, e.options.numbers);
        for (auto text : e.options.cells)
            m_renderStrings[text] = BoundedText(m_model.entries.at(text).encoded, e.options.numbers);
        for (auto text : e.options.headers)
            if (text)
                m_renderStrings[text] = BoundedText(m_model.entries.at(text).encoded, e.options.numbers);
        MenuScope scope;
        scope.Set(static_cast<Menu*>(item.second), e.slot);
        reinterpret_cast<void(__cdecl*)(unsigned char, bool)>(e.kind == ENativeUIKind::Grid ? 0x5816E0 : 0x580E00)(static_cast<unsigned char>(e.slot), true);
    }
    customDraw = false;
}
bool CNativeUISA::CaptureInput(const CControllerState& input, bool blocked)
{
    NativeUIHandle focus{};
    for (const auto& item : m_menus)
        if (m_model.entries.at(item.first).options.visible && !At<MenuGlobals>(0xBA82D8).used[m_model.entries.at(item.first).slot])
        {
            focus = item.first;
            break;
        }
    if (blocked || m_inputBlocked || !focus)
    {
        m_previousInput = input;
        m_focus = 0;
        return false;
    }
    auto& e = m_model.entries.at(focus);
    if (At<MenuGlobals>(0xBA82D8).used[e.slot])
        return false;
    if (m_focus != focus)
    {
        m_focus = focus;
        m_previousInput = input;
        return true;
    }
    auto*            menu = static_cast<Menu*>(m_menus.at(focus));
    CControllerState current = input;
    // Retail menu code polls held controls. Feed only rising edges for accept
    // and cancel, so a held key cannot act on a newly created generation.
    if (m_previousInput.ButtonCross)
        current.ButtonCross = 0;
    if (m_previousInput.ButtonTriangle)
        current.ButtonTriangle = 0;
    const bool accept = current.ButtonCross != 0;
    // The retail grid accept branch indexes the 12-row enabled array with a
    // 0..63 color index. Keep native navigation, but validate acceptance here.
    if (e.kind == ENativeUIKind::Grid)
        current.ButtonCross = 0;
    CControllerState savedCurrent, savedLast;
    pGame->GetPad()->GetCurrentControllerState(&savedCurrent);
    pGame->GetPad()->GetLastControllerState(&savedLast);
    pGame->GetPad()->SetCurrentControllerState(&current);
    pGame->GetPad()->SetLastControllerState(&m_previousInput);
    {
        MenuScope scope;
        scope.Set(menu, e.slot);
        menu->accepted = -99;
        reinterpret_cast<void(__cdecl*)(unsigned char)>(0x5825D0)(static_cast<unsigned char>(e.slot));
    }
    pGame->GetPad()->SetCurrentControllerState(&savedCurrent);
    pGame->GetPad()->SetLastControllerState(&savedLast);
    if (current.ButtonTriangle)
    {
        e.cancelled = true;
        e.options.visible = false;
    }
    else if (accept && (e.kind == ENativeUIKind::Grid || menu->accepted >= 0))
    {
        e.accepted = e.kind == ENativeUIKind::Grid ? menu->selected : menu->accepted;
        e.options.visible = false;
    }
    if ((e.cancelled || e.accepted >= 0) && m_events.size() < 256)
        m_events.push_back(
            {e.owner, focus, e.cancelled ? "cancelled" : "accepted", menu->selected, e.kind == ENativeUIKind::Grid ? menu->colors[menu->selected] : -1});
    e.options.selected = menu->selected;
    m_previousInput = input;
    return true;
}

bool CNativeUISA::LoadCards(std::string& error)
{
    if (m_cardDictionary)
        return true;
    const char* root = reinterpret_cast<const char*>(0xB71AE0);
    const auto  length = strnlen(root, 128);
    if (!length || length == 128)
    {
        error = "gta-directory-unavailable";
        return false;
    }
    // A private dictionary keeps the 53 stock blackjack textures alive without
    // borrowing SCM sprite slots or changing GTA's current TXD/streaming refs.
    auto*             rw = static_cast<CRenderWareSA*>(pGame->GetRenderWare());
    const std::string path = std::string(root, length) + "\\models\\txd\\LD_CARD.txd";
    auto*             dictionary = rw->ReadTXD(path.c_str(), SString{});
    if (!dictionary)
    {
        error = "native-card-textures-unavailable";
        return false;
    }
    m_cardDictionary = dictionary;
    std::vector<RwTexture*> textures;
    CRenderWareSA::GetTxdTextures(textures, dictionary);
    for (unsigned card = 1; card <= 53; ++card)
    {
        char name[16]{};
        if (card == 53)
            std::strcpy(name, "cdback");
        else
            snprintf(name, sizeof(name), "cd%u%c", (card - 1) % 13 + 1, "cdsh"[(card - 1) / 13]);
        for (auto* texture : textures)
            if (_stricmp(texture->name, name) == 0)
            {
                m_cardTextures[card - 1] = texture;
                break;
            }
        if (!m_cardTextures[card - 1])
        {
            UnloadCards();
            error = "native-card-texture-missing";
            return false;
        }
    }
    return true;
}
void CNativeUISA::UnloadCards()
{
    if (m_cardDictionary)
        static_cast<CRenderWareSA*>(pGame->GetRenderWare())->DestroyTXD(static_cast<RwTexDictionary*>(m_cardDictionary));
    m_cardDictionary = nullptr;
    m_cardTextures = {};
}

bool CNativeUISA::PollEvent(SNativeUIEvent& event)
{
    while (!m_events.empty())
    {
        event = std::move(m_events.front());
        m_events.pop_front();
        if (m_model.Find(event.owner, event.handle))
            return true;
    }
    return false;
}
