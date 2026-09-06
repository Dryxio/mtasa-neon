#pragma once
#include <game/CNativeUI.h>
#include "NativeUIModel.h"
#include <game/CPad.h>
#include <memory>
#include <deque>

class CNativeUISA final : public CNativeUI
{
public:
    CNativeUISA();
    ~CNativeUISA() override;
    NativeUIHandle Create(void*, ENativeUIKind, const SNativeUIOptions&, std::string&) override;
    bool           Update(void*, NativeUIHandle, const SNativeUIOptions&, std::string&) override;
    bool           GetOptions(void*, NativeUIHandle, SNativeUIOptions&, ENativeUIKind&) const override;
    bool           GetState(void*, NativeUIHandle, SNativeUIState&) override;
    bool           Destroy(void*, NativeUIHandle, std::string&) override;
    void           Release(void*) override;
    bool           Show(void*, NativeUIHandle, const std::string&, unsigned int, int, const std::array<int, 2>&, std::string&) override;
    bool           Clear(void*, const std::string&) override;
    bool           HasMessages() const override;
    void           Pulse(bool) override;
    bool           PollEvent(SNativeUIEvent&) override;
    bool           CaptureInput(const CControllerState&, bool blocked) override;
    void           Draw(bool);
    void           DrawTimers();
    const char*    Resolve(const char*);

private:
    struct Message
    {
        void*          owner{};
        NativeUIHandle text{};
        std::string    bytes;
        std::uint64_t  expires{};
        int            style{};
    };
    NativeUI::Model                       m_model;
    std::map<NativeUIHandle, void*>       m_menus;
    std::map<std::string, Message>        m_messages;
    std::map<NativeUIHandle, std::string> m_renderStrings;
    std::deque<SNativeUIEvent>            m_events;
    CControllerState                      m_previousInput;
    NativeUIHandle                        m_focus{};
    bool                                  m_inputBlocked{true}, m_ready{}, m_helpWasVisible{};
    void                                  ClearMessage(Message&, const std::string&);
    bool                                  MakeMenu(NativeUIHandle, NativeUI::Entry&, std::string&);
    void                                  FreeMenu(NativeUIHandle);
    bool                                  LoadText(NativeUI::Entry&, std::string&);
    void                                  PrepareStrings();
    bool                                  LoadCards(std::string& error);
    void                                  UnloadCards();
    void*                                 m_cardDictionary{};
    std::array<void*, 53>                 m_cardTextures{};
};
