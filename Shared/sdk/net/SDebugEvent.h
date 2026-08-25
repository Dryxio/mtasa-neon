/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Shared/sdk/net/SDebugEvent.h
 *  PURPOSE:     Structured script diagnostic event wire contract
 *
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include "bitstream.h"

enum class EDebugEventSide : std::uint8_t
{
    CLIENT,
    SERVER,
};

enum class EDebugEventSeverity : std::uint8_t
{
    DEBUG_MESSAGE,
    ERROR_MESSAGE,
    WARNING_MESSAGE,
    INFORMATION,
    CUSTOM_MESSAGE,
};

struct SDebugEvent
{
    static constexpr std::uint8_t  WIRE_VERSION = 2;
    static constexpr std::size_t   CATEGORY_BYTE_LIMIT = 64;
    static constexpr std::size_t   CONTEXT_BYTE_LIMIT = 192;
    static constexpr std::size_t   RESOURCE_BYTE_LIMIT = 128;
    static constexpr std::size_t   FILE_BYTE_LIMIT = 512;
    static constexpr std::size_t   MESSAGE_BYTE_LIMIT = 2048;
    static constexpr std::size_t   CORRELATION_ID_BYTE_LIMIT = 96;
    static constexpr std::uint32_t INVALID_SOURCE_LINE = 0;

    std::uint32_t       sequence{};
    std::uint32_t       timestamp{};
    EDebugEventSide     side{EDebugEventSide::CLIENT};
    EDebugEventSeverity severity{EDebugEventSeverity::DEBUG_MESSAGE};
    std::uint32_t       line{INVALID_SOURCE_LINE};
    std::uint32_t       repeatCount{1};
    std::uint8_t        red{255};
    std::uint8_t        green{255};
    std::uint8_t        blue{255};
    std::string         category;
    std::string         context;
    std::string         resource;
    std::string         file;
    std::string         message;
    std::string         correlationId;
};

inline bool IsValidDebugEventSide(EDebugEventSide side) noexcept
{
    return side == EDebugEventSide::CLIENT || side == EDebugEventSide::SERVER;
}

inline bool IsValidDebugEventSeverity(EDebugEventSeverity severity) noexcept
{
    return severity >= EDebugEventSeverity::DEBUG_MESSAGE && severity <= EDebugEventSeverity::CUSTOM_MESSAGE;
}

inline std::string_view ClampDebugEventString(std::string_view value, std::size_t maximumLength) noexcept
{
    if (value.size() <= maximumLength)
        return value;

    std::size_t length = maximumLength;
    while (length > 0 && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80)
        --length;
    if (length == maximumLength)
        return value.substr(0, maximumLength);

    // Do not put malformed UTF-8 on the wire when the byte limit falls in the
    // middle of a multibyte code point. Invalid source bytes are otherwise
    // preserved so diagnostics still reveal the script's actual payload.
    return value.substr(0, length);
}

inline void WriteDebugEventString(NetBitStreamInterface& stream, std::string_view value, std::size_t maximumLength)
{
    const auto clamped = ClampDebugEventString(value, maximumLength);
    stream.Write(static_cast<std::uint16_t>(clamped.size()));
    stream.WriteStringCharacters(clamped);
}

inline bool ReadDebugEventString(NetBitStreamInterface& stream, std::string& value, std::size_t maximumLength)
{
    std::uint16_t length{};
    if (!stream.Read(length) || length > maximumLength)
        return false;
    return stream.ReadStringCharacters(value, length);
}

inline void WriteDebugEvent(NetBitStreamInterface& stream, const SDebugEvent& event)
{
    stream.Write(SDebugEvent::WIRE_VERSION);
    stream.Write(event.sequence);
    stream.Write(event.timestamp);
    stream.Write(static_cast<std::uint8_t>(event.side));
    stream.Write(static_cast<std::uint8_t>(event.severity));
    stream.Write(event.line);
    stream.Write(std::max<std::uint32_t>(event.repeatCount, 1));
    stream.Write(event.red);
    stream.Write(event.green);
    stream.Write(event.blue);
    WriteDebugEventString(stream, event.category, SDebugEvent::CATEGORY_BYTE_LIMIT);
    WriteDebugEventString(stream, event.context, SDebugEvent::CONTEXT_BYTE_LIMIT);
    WriteDebugEventString(stream, event.resource, SDebugEvent::RESOURCE_BYTE_LIMIT);
    WriteDebugEventString(stream, event.file, SDebugEvent::FILE_BYTE_LIMIT);
    WriteDebugEventString(stream, event.message, SDebugEvent::MESSAGE_BYTE_LIMIT);
    WriteDebugEventString(stream, event.correlationId, SDebugEvent::CORRELATION_ID_BYTE_LIMIT);
}

inline bool ReadDebugEvent(NetBitStreamInterface& stream, SDebugEvent& output)
{
    SDebugEvent  event;
    std::uint8_t wireVersion{};
    std::uint8_t side{};
    std::uint8_t severity{};
    if (!stream.Read(wireVersion) || wireVersion != SDebugEvent::WIRE_VERSION || !stream.Read(event.sequence) || !stream.Read(event.timestamp) ||
        !stream.Read(side) || !stream.Read(severity) || !stream.Read(event.line) || !stream.Read(event.repeatCount) || !stream.Read(event.red) ||
        !stream.Read(event.green) || !stream.Read(event.blue))
    {
        return false;
    }

    event.side = static_cast<EDebugEventSide>(side);
    event.severity = static_cast<EDebugEventSeverity>(severity);
    if (!IsValidDebugEventSide(event.side) || !IsValidDebugEventSeverity(event.severity) || event.repeatCount == 0)
        return false;

    if (!ReadDebugEventString(stream, event.category, SDebugEvent::CATEGORY_BYTE_LIMIT) ||
        !ReadDebugEventString(stream, event.context, SDebugEvent::CONTEXT_BYTE_LIMIT) ||
        !ReadDebugEventString(stream, event.resource, SDebugEvent::RESOURCE_BYTE_LIMIT) ||
        !ReadDebugEventString(stream, event.file, SDebugEvent::FILE_BYTE_LIMIT) ||
        !ReadDebugEventString(stream, event.message, SDebugEvent::MESSAGE_BYTE_LIMIT) ||
        !ReadDebugEventString(stream, event.correlationId, SDebugEvent::CORRELATION_ID_BYTE_LIMIT))
    {
        return false;
    }

    output = std::move(event);
    return true;
}
