/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/DevTools/CDevTools.cpp
 *  PURPOSE:     Core-owned structured script diagnostics console
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CDevTools.h"

#include <core/CAjaxResourceHandlerInterface.h>
#include <core/CWebCoreInterface.h>
#include <core/CWebViewInterface.h>
#include <gui/CGUIWebBrowser.h>
#include <json.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace
{
    constexpr char WEB_ROOT[] = "MTA\\cef\\devtools";

    bool IsSafeRelativePath(const SString& path)
    {
        return !path.empty() && path[0] != '/' && path[0] != '\\' && path.find("..") == SString::npos && path.find(':') == SString::npos;
    }

    const char* SideName(EDebugEventSide side)
    {
        return side == EDebugEventSide::SERVER ? "server" : "client";
    }

    const char* SeverityName(EDebugEventSeverity severity)
    {
        switch (severity)
        {
            case EDebugEventSeverity::ERROR_MESSAGE:
                return "error";
            case EDebugEventSeverity::WARNING_MESSAGE:
                return "warning";
            case EDebugEventSeverity::INFORMATION:
                return "info";
            case EDebugEventSeverity::CUSTOM_MESSAGE:
                return "custom";
            default:
                return "debug";
        }
    }

    void AddString(json_object* object, const char* key, const std::string& value)
    {
        json_object_object_add(object, key, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
    }

    void AppendEvent(json_object* events, const SStoredDebugEvent& event)
    {
        json_object* item = json_object_new_object();
        json_object_array_add(events, item);
        AddString(item, "entryId", std::to_string(event.entryId));
        json_object_object_add(item, "sequence", json_object_new_int64(event.sequence));
        json_object_object_add(item, "timestamp", json_object_new_int64(event.timestamp));
        json_object_object_add(item, "line", json_object_new_int64(event.line));
        json_object_object_add(item, "repeatCount", json_object_new_int64(event.repeatCount));
        json_object_object_add(item, "red", json_object_new_int(event.red));
        json_object_object_add(item, "green", json_object_new_int(event.green));
        json_object_object_add(item, "blue", json_object_new_int(event.blue));
        json_object_object_add(item, "firstSeen", json_object_new_int64(event.firstSeen));
        json_object_object_add(item, "lastSeen", json_object_new_int64(event.lastSeen));
        AddString(item, "side", SideName(event.side));
        AddString(item, "severity", SeverityName(event.severity));
        AddString(item, "category", event.category);
        AddString(item, "context", event.context);
        AddString(item, "resource", event.resource);
        AddString(item, "file", event.file);
        AddString(item, "message", event.message);
        AddString(item, "correlationId", event.correlationId);
    }
}

CDevTools* CDevTools::ms_instance = nullptr;

CDevTools::CDevTools()
{
    ms_instance = this;
}

CDevTools::~CDevTools()
{
    if (ms_instance == this)
        ms_instance = nullptr;
    if (m_ownsForcedCursor)
    {
        g_pCore->ForceCursorVisible(false);
        m_ownsForcedCursor = false;
    }
    if (m_widget)
    {
        g_pCore->GetGUI()->DestroyElementRecursive(m_widget);
        m_widget = nullptr;
    }
    if (m_webView)
    {
        m_webView->ClearWebBrowserEvents(this);
        if (CWebCoreInterface* webCore = g_pCore->GetWebCoreUnchecked())
            webCore->DestroyWebView(m_webView);
        m_webView = nullptr;
    }
}

void CDevTools::Submit(const SDebugEvent& event)
{
    m_store.Add(event);
}

bool CDevTools::InitialiseWebView()
{
    CWebCoreInterface* webCore = g_pCore->GetWebCore();
    m_resolution = g_pCore->GetGUI()->GetResolution();
    if (!webCore || m_resolution.fX < 1 || m_resolution.fY < 1)
        return false;

    CWebBrowserItem* renderItem =
        g_pCore->GetGraphics()->GetRenderItemManager()->CreateWebBrowser(static_cast<uint>(m_resolution.fX), static_cast<uint>(m_resolution.fY));
    if (!renderItem)
        return false;
    m_webView = webCore->CreateWebView(static_cast<uint>(m_resolution.fX), static_cast<uint>(m_resolution.fY), true, renderItem, true);
    renderItem->Release();
    if (!m_webView)
        return false;

    m_webView->SetWebBrowserEvents(this);
    m_webView->Initialise();
    m_widget = g_pCore->GetGUI()->CreateWebBrowser(static_cast<CGUIElement*>(nullptr));
    if (!m_widget)
        return false;
    m_widget->SetPosition(CVector2D(0, 0), false);
    m_widget->SetSize(m_resolution, false);
    m_widget->SetFrameEnabled(false);
    m_widget->LoadFromWebView(m_webView);
    m_widget->SetAlwaysOnTop(true);
    m_widget->SetZOrderingEnabled(true);
    m_widget->SetVisible(false);
    return m_webView->LoadURL("http://mta/local/index.html", false);
}

void CDevTools::SaveCursorPosition()
{
    HWND  window = g_pCore->GetHookedWindow();
    POINT position;
    RECT  clientRect;
    if (!window || !GetCursorPos(&position) || !ScreenToClient(window, &position) || !GetClientRect(window, &clientRect) || !PtInRect(&clientRect, position))
        return;

    m_cursorClientPosition = position;
    m_hasCursorPosition = true;
}

void CDevTools::RestoreCursorPosition()
{
    HWND window = g_pCore->GetHookedWindow();
    RECT clientRect;
    if (!window || !GetClientRect(window, &clientRect) || clientRect.right <= clientRect.left || clientRect.bottom <= clientRect.top)
        return;

    POINT position = m_hasCursorPosition ? m_cursorClientPosition : POINT{clientRect.right / 2, clientRect.bottom / 2};
    position.x = std::clamp<LONG>(position.x, clientRect.left, clientRect.right - 1);
    position.y = std::clamp<LONG>(position.y, clientRect.top, clientRect.bottom - 1);
    if (!ClientToScreen(window, &position))
        return;

    // Seed MTA's stored GUI position as well as the system cursor so the next
    // cursor-ownership pulse cannot replace the DevTools-specific position.
    g_pCore->GetLocalGUI()->SetCursorPos(position.x, position.y, true);
}

void CDevTools::SetVisible(bool visible)
{
    m_visible = visible;
    if (!m_widget || !m_webView)
        return;

    if (visible)
    {
        // Do not expose a blank or partially loaded Chromium surface on the
        // first invocation. Events_OnDocumentReady will complete the request.
        if (!m_documentReady || m_surfaceVisible)
            return;

        CLocalGUI* localGUI = g_pCore->GetLocalGUI();
        const bool consoleWasVisible = localGUI->IsConsoleVisible();
        const bool chatHadInput = localGUI->IsChatBoxInputEnabled();
        m_restorePreviousCursorPosition = !consoleWasVisible && !chatHadInput && localGUI->InputGoesToGUI() && GetCursorPos(&m_previousScreenCursorPosition);

        // DevTools is an exclusive keyboard surface. Explicitly relinquish F8
        // and chat input instead of displaying a browser that cannot receive keys.
        if (consoleWasVisible)
            localGUI->SetConsoleVisible(false);
        if (chatHadInput)
            localGUI->SetChatBoxInputEnabled(false);

        // The inspector can be opened while GTA owns mouse input. Temporarily
        // claim the cursor, but leave an existing resource-owned cursor alone.
        if (!g_pCore->IsCursorForcedVisible())
        {
            g_pCore->ForceCursorVisible(true);
            m_ownsForcedCursor = true;
        }
        RestoreCursorPosition();
        m_webView->SetRenderingPaused(false);
        m_widget->SetEnabled(true);
        m_widget->SetVisible(true);
        m_widget->BringToFront();
        m_widget->Activate();
        m_webView->Focus(true);
        m_surfaceVisible = true;
        SendSnapshot();
    }
    else
    {
        if (!m_surfaceVisible)
            return;

        SaveCursorPosition();
        m_widget->Deactivate();
        m_widget->SetEnabled(false);
        m_widget->SetVisible(false);
        m_webView->Focus(false);
        m_webView->SetRenderingPaused(true);
        if (m_ownsForcedCursor)
        {
            g_pCore->ForceCursorVisible(false);
            m_ownsForcedCursor = false;
        }
        if (m_restorePreviousCursorPosition)
            g_pCore->GetLocalGUI()->SetCursorPos(m_previousScreenCursorPosition.x, m_previousScreenCursorPosition.y, true);
        m_restorePreviousCursorPosition = false;
        m_surfaceVisible = false;
    }
}

void CDevTools::DoPulse()
{
    if (m_initialisationFailed)
        return;
    if (!m_webView)
    {
        if (!m_visible)
            return;
        if (m_initialisationDelayPulses > 0)
        {
            --m_initialisationDelayPulses;
            return;
        }
        if (!InitialiseWebView())
        {
            m_initialisationFailed = true;
            return;
        }
        SetVisible(m_visible);
    }

    const CVector2D resolution = g_pCore->GetGUI()->GetResolution();
    if (resolution.fX != m_resolution.fX || resolution.fY != m_resolution.fY)
    {
        m_resolution = resolution;
        m_widget->SetSize(resolution, false);
        m_webView->Resize(resolution);
    }

    if (m_visible && m_documentReady && m_sentRevision != m_store.GetRevision() && ++m_snapshotDelayPulses >= 3)
    {
        m_snapshotDelayPulses = 0;
        SendSnapshot();
    }
}

bool CDevTools::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!ms_instance)
        return false;

    const bool keyboardMessage =
        message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR || message == WM_SYSCHAR || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    if (!keyboardMessage)
        return false;

    if (message == WM_KEYUP && wParam == 'I' && ms_instance->m_toggleShortcutHeld)
    {
        ms_instance->m_toggleShortcutHeld = false;
        return true;
    }
    if (message == WM_CHAR && ms_instance->m_toggleShortcutHeld)
        return true;
    if (message == WM_KEYDOWN && wParam == 'I' && ms_instance->m_toggleShortcutHeld)
        return true;
    if (message == WM_KEYDOWN && wParam == 'I' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000))
    {
        // Windows repeats WM_KEYDOWN while the shortcut is held. Toggle only
        // on the physical press edge so cursor and focus ownership stay stable.
        ms_instance->m_toggleShortcutHeld = true;
        if ((lParam & 0x40000000) == 0)
            ms_instance->Toggle();
        return true;
    }

    if (wParam == VK_ESCAPE && ms_instance->m_escapeClosingHeld)
    {
        if (message == WM_KEYUP)
            ms_instance->m_escapeClosingHeld = false;
        return keyboardMessage;
    }
    if (!ms_instance->m_visible)
        return false;
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        // Consume the complete key gesture after closing. Otherwise Escape
        // auto-repeat can immediately open the regular MTA pause menu.
        ms_instance->m_escapeClosingHeld = true;
        ms_instance->SetVisible(false);
        return true;
    }
    if (g_pCore->IsWebCoreLoaded() && !g_pCore->GetConsole()->IsVisible() && !g_pCore->IsChatInputEnabled())
    {
        g_pCore->GetWebCore()->ProcessInputMessage(message, wParam, lParam);
        return true;
    }
    return false;
}

std::string CDevTools::BuildSnapshotJson() const
{
    json_object* root = json_object_new_object();
    json_object* events = json_object_new_array_ext(static_cast<int>(m_store.GetEvents().size()));
    json_object_object_add(root, "events", events);
    json_object_object_add(root, "dropped", json_object_new_int64(m_store.GetDroppedCount()));
    json_object_object_add(root, "capturing", json_object_new_boolean(m_store.IsCaptureActive()));
    json_object_object_add(root, "captureAvailable", json_object_new_boolean(m_store.HasCapture()));
    json_object_object_add(root, "captureEntries", json_object_new_int64(m_store.GetCapture().size()));

    for (const auto& event : m_store.GetEvents())
        AppendEvent(events, event);

    const char* encoded = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    std::string output = encoded ? encoded : "{\"events\":[]}";
    json_object_put(root);
    return output;
}

void CDevTools::SendSnapshot()
{
    if (!m_documentReady || !m_webView)
        return;
    const std::string script = "window.__neonDevTools&&window.__neonDevTools.receive(" + BuildSnapshotJson() + ");";
    m_webView->ExecuteJavascript(script);
    m_sentRevision = m_store.GetRevision();
}

void CDevTools::ExportCapture(const char* format)
{
    const bool                           exportingCapture = m_store.HasCapture();
    const std::vector<SStoredDebugEvent> capture =
        exportingCapture ? m_store.GetCapture() : std::vector<SStoredDebugEvent>(m_store.GetEvents().begin(), m_store.GetEvents().end());
    CreateDirectoryA(CalcMTASAPath("MTA\\logs"), nullptr);
    const std::string extension = strcmp(format, "txt") == 0 ? "txt" : "json";
    const SString     path = CalcMTASAPath(SString("MTA\\logs\\neon-devtools-%u.%s", GetTickCount32(), extension.c_str()));
    FILE*             file = File::Fopen(path, "wb");
    if (!file)
        return;

    if (extension == "json")
    {
        json_object* root = json_object_new_object();
        json_object* events = json_object_new_array_ext(static_cast<int>(capture.size()));
        json_object_object_add(root, "format", json_object_new_string("neon-devtools-diagnostics-v2"));
        json_object_object_add(root, "source", json_object_new_string(exportingCapture ? "capture" : "history"));
        json_object_object_add(root, "generatedAt", json_object_new_int64(std::time(nullptr)));
        json_object_object_add(root, "dropped", json_object_new_int64(m_store.GetDroppedCount()));
        json_object_object_add(root, "events", events);
        for (const auto& event : capture)
            AppendEvent(events, event);
        const char* encoded = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
        if (encoded)
            fwrite(encoded, strlen(encoded), 1, file);
        json_object_put(root);
    }
    else
    {
        for (const auto& event : capture)
        {
            const SString row("[%u] [%s] [%s] [%s] %s%s%s%s%s%s%s%s%s\n", event.timestamp, SideName(event.side), SeverityName(event.severity),
                              event.resource.empty() ? "-" : event.resource.c_str(), event.file.c_str(), event.file.empty() ? "" : ":",
                              event.line ? SString("%u", event.line).c_str() : "", event.file.empty() ? "" : " ", event.message.c_str(),
                              event.context.empty() ? "" : " [", event.context.c_str(), event.context.empty() ? "" : "]",
                              event.repeatCount > 1 ? SString(" [x%u]", event.repeatCount).c_str() : "");
            fwrite(row.data(), row.size(), 1, file);
        }
    }
    fclose(file);

    if (m_webView)
    {
        json_object* pathJson = json_object_new_string(path);
        m_webView->ExecuteJavascript(SString("window.__neonDevTools&&window.__neonDevTools.exported(%s);", json_object_to_json_string(pathJson)));
        json_object_put(pathJson);
    }
}

void CDevTools::Events_OnCreated()
{
}
void CDevTools::Events_OnLoadingStart(const SString&, bool)
{
    m_documentReady = false;
}
void CDevTools::Events_OnDocumentReady(const SString&)
{
    m_documentReady = true;
    SendSnapshot();
    if (m_visible)
        SetVisible(true);
}
void CDevTools::Events_OnLoadingFailed(const SString&, int, const SString&)
{
    m_initialisationFailed = true;
}
void CDevTools::Events_OnNavigate(const SString&, bool, bool)
{
}
void CDevTools::Events_OnPopup(const SString&, const SString&)
{
}
void CDevTools::Events_OnChangeCursor(unsigned char)
{
}
void CDevTools::Events_OnTooltip(const SString&)
{
}
void CDevTools::Events_OnInputFocusChanged(bool)
{
}
void CDevTools::Events_OnResourceBlocked(const SString&, const SString&, unsigned char)
{
}
void CDevTools::Events_OnAjaxRequest(CAjaxResourceHandlerInterface*, const SString&)
{
}
void CDevTools::Events_OnConsoleMessage(const std::string& message, const std::string& source, int line, std::int16_t)
{
    WriteDebugEvent(SString("DevTools CEF: %s:%d: %s", source.c_str(), line, message.c_str()));
}

void CDevTools::Events_OnTriggerEvent(const SString& eventName, const std::vector<std::string>& arguments)
{
    if (eventName == "devtools:ready")
        SendSnapshot();
    else if (eventName == "devtools:close")
        SetVisible(false);
    else if (eventName == "devtools:clear")
        m_store.Clear();
    else if (eventName == "devtools:capture-start")
        m_store.StartCapture(GetTickCount32());
    else if (eventName == "devtools:capture-stop")
        m_store.StopCapture(GetTickCount32());
    else if (eventName == "devtools:capture-discard")
        m_store.ClearCapture();
    else if (eventName == "devtools:export")
        ExportCapture(arguments.empty() ? "json" : arguments.front().c_str());
    SendSnapshot();
}

bool CDevTools::Events_OnResourcePathCheck(SString& url)
{
    if (!IsSafeRelativePath(url))
        return false;
    std::replace(url.begin(), url.end(), '/', '\\');
    url = CalcMTASAPath(PathJoin(WEB_ROOT, url));
    return FileExists(url);
}

bool CDevTools::Events_OnResourceFileCheck(const SString& path, CBuffer& outFileData)
{
    const SString root = CalcMTASAPath(WEB_ROOT) + "\\";
    return path.BeginsWith(root) && FileExists(path) && outFileData.LoadFromFile(path);
}
