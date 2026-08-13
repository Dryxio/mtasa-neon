/*****************************************************************************/
/*
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CSampMapParser.cpp
 *  PURPOSE:     Parser for the literal map format exported by SA-MP editors
 *
 *****************************************************************************/

#ifndef SAMP_MAP_PARSER_STANDALONE
    #include "StdInc.h"
#endif
#include "CSampMapParser.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace SampMap
{
    namespace
    {
        enum class ETokenKind
        {
            Identifier,
            Number,
            String,
            LeftParenthesis,
            RightParenthesis,
            LeftBracket,
            RightBracket,
            Comma,
            Semicolon,
            Equal,
            Plus,
            Minus,
            Other,
            End,
        };

        struct SToken
        {
            ETokenKind      kind = ETokenKind::Other;
            std::string     text;
            SSourceLocation location;
        };

        class CDiagnosticSink
        {
        public:
            CDiagnosticSink(std::vector<SDiagnostic>& diagnostics, std::size_t& errorCount, std::size_t maximum)
                : m_diagnostics(diagnostics), m_errorCount(errorCount), m_maximum(maximum)
            {
            }

            void Add(EDiagnosticSeverity severity, SSourceLocation location, std::string message)
            {
                if (severity == EDiagnosticSeverity::Error)
                    ++m_errorCount;
                if (m_diagnostics.size() >= m_maximum)
                    return;

                m_diagnostics.push_back({severity, location, std::move(message)});
            }

        private:
            std::vector<SDiagnostic>& m_diagnostics;
            std::size_t&              m_errorCount;
            std::size_t               m_maximum;
        };

        class CLexer
        {
        public:
            CLexer(std::string_view source, const SParserLimits& limits, CDiagnosticSink& diagnostics)
                : m_source(source), m_limits(limits), m_diagnostics(diagnostics)
            {
            }

            std::vector<SToken> Tokenize()
            {
                std::vector<SToken> tokens;
                tokens.reserve(std::min<std::size_t>(m_source.size() / 4, m_limits.maxTokens));

                while (!AtEnd())
                {
                    SkipTrivia();
                    if (AtEnd())
                        break;

                    if (tokens.size() >= m_limits.maxTokens)
                    {
                        m_diagnostics.Add(EDiagnosticSeverity::Error, CurrentLocation(), "token limit exceeded");
                        break;
                    }

                    const SSourceLocation location = CurrentLocation();
                    const char            character = Peek();

                    if (IsIdentifierStart(character))
                        tokens.push_back(ReadIdentifier(location));
                    else if (IsNumberStart())
                        tokens.push_back(ReadNumber(location));
                    else if (character == '"')
                        tokens.push_back(ReadString(location));
                    else
                        tokens.push_back(ReadPunctuation(location));
                }

                tokens.push_back({ETokenKind::End, {}, CurrentLocation()});
                return tokens;
            }

        private:
            bool AtEnd() const { return m_offset >= m_source.size(); }

            char Peek(std::size_t ahead = 0) const
            {
                const std::size_t offset = m_offset + ahead;
                return offset < m_source.size() ? m_source[offset] : '\0';
            }

            SSourceLocation CurrentLocation() const { return {m_line, m_column}; }

            char Advance()
            {
                const char character = m_source[m_offset++];
                if (character == '\r')
                {
                    if (Peek() == '\n')
                        ++m_offset;
                    ++m_line;
                    m_column = 1;
                }
                else if (character == '\n')
                {
                    ++m_line;
                    m_column = 1;
                }
                else
                    ++m_column;
                return character;
            }

            static bool IsIdentifierStart(char character)
            {
                return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
            }

            static bool IsIdentifierContinuation(char character) { return IsIdentifierStart(character) || (character >= '0' && character <= '9'); }

            static bool IsHexDigit(char character)
            {
                return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
            }

            bool IsNumberStart() const
            {
                const char character = Peek();
                return (character >= '0' && character <= '9') || (character == '.' && Peek(1) >= '0' && Peek(1) <= '9');
            }

            void SkipTrivia()
            {
                while (!AtEnd())
                {
                    const char character = Peek();
                    if (character == ' ' || character == '\t' || character == '\v' || character == '\f' || character == '\r' || character == '\n')
                    {
                        Advance();
                        continue;
                    }

                    if (character == '/' && Peek(1) == '/')
                    {
                        Advance();
                        Advance();
                        while (!AtEnd() && Peek() != '\r' && Peek() != '\n')
                            Advance();
                        continue;
                    }

                    if (character == '/' && Peek(1) == '*')
                    {
                        const SSourceLocation location = CurrentLocation();
                        Advance();
                        Advance();
                        while (!AtEnd() && !(Peek() == '*' && Peek(1) == '/'))
                            Advance();

                        if (AtEnd())
                        {
                            m_diagnostics.Add(EDiagnosticSeverity::Error, location, "unterminated block comment");
                            return;
                        }

                        Advance();
                        Advance();
                        continue;
                    }
                    break;
                }
            }

            SToken ReadIdentifier(SSourceLocation location)
            {
                const std::size_t start = m_offset;
                while (!AtEnd() && IsIdentifierContinuation(Peek()))
                    Advance();
                return {ETokenKind::Identifier, std::string{m_source.substr(start, m_offset - start)}, location};
            }

            SToken ReadNumber(SSourceLocation location)
            {
                const std::size_t start = m_offset;
                if (Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X'))
                {
                    Advance();
                    Advance();
                    while (!AtEnd() && IsHexDigit(Peek()))
                        Advance();
                }
                else
                {
                    while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
                        Advance();
                    if (Peek() == '.')
                    {
                        Advance();
                        while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
                            Advance();
                    }
                    if (Peek() == 'e' || Peek() == 'E')
                    {
                        Advance();
                        if (Peek() == '+' || Peek() == '-')
                            Advance();
                        while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
                            Advance();
                    }
                }
                return {ETokenKind::Number, std::string{m_source.substr(start, m_offset - start)}, location};
            }

            SToken ReadString(SSourceLocation location)
            {
                std::string value;
                Advance();
                bool terminated = false;

                while (!AtEnd())
                {
                    const char character = Advance();
                    if (character == '"')
                    {
                        terminated = true;
                        break;
                    }

                    if (character == '\r' || character == '\n')
                    {
                        m_diagnostics.Add(EDiagnosticSeverity::Error, location, "unterminated string literal");
                        break;
                    }

                    if (character == '\\' && !AtEnd())
                    {
                        const char escaped = Advance();
                        switch (escaped)
                        {
                            case 'n':
                                value.push_back('\n');
                                break;
                            case 'r':
                                value.push_back('\r');
                                break;
                            case 't':
                                value.push_back('\t');
                                break;
                            case '\\':
                                value.push_back('\\');
                                break;
                            case '"':
                                value.push_back('"');
                                break;
                            default:
                                // Pawn exporters commonly preserve unknown escapes. Do
                                // the same so texture names are never silently changed.
                                value.push_back('\\');
                                value.push_back(escaped);
                                break;
                        }
                    }
                    else
                        value.push_back(character);

                    if (value.size() > m_limits.maxStringBytes)
                    {
                        m_diagnostics.Add(EDiagnosticSeverity::Error, location, "string literal exceeds size limit");
                        while (!AtEnd() && Peek() != '"' && Peek() != '\r' && Peek() != '\n')
                            Advance();
                        if (Peek() == '"')
                        {
                            Advance();
                            terminated = true;
                        }
                        break;
                    }
                }

                if (!terminated && AtEnd())
                    m_diagnostics.Add(EDiagnosticSeverity::Error, location, "unterminated string literal");
                return {ETokenKind::String, std::move(value), location};
            }

            SToken ReadPunctuation(SSourceLocation location)
            {
                const char character = Advance();
                ETokenKind kind = ETokenKind::Other;
                switch (character)
                {
                    case '(':
                        kind = ETokenKind::LeftParenthesis;
                        break;
                    case ')':
                        kind = ETokenKind::RightParenthesis;
                        break;
                    case '[':
                        kind = ETokenKind::LeftBracket;
                        break;
                    case ']':
                        kind = ETokenKind::RightBracket;
                        break;
                    case ',':
                        kind = ETokenKind::Comma;
                        break;
                    case ';':
                        kind = ETokenKind::Semicolon;
                        break;
                    case '=':
                        kind = ETokenKind::Equal;
                        break;
                    case '+':
                        kind = ETokenKind::Plus;
                        break;
                    case '-':
                        kind = ETokenKind::Minus;
                        break;
                    default:
                        break;
                }
                return {kind, std::string(1, character), location};
            }

            std::string_view     m_source;
            const SParserLimits& m_limits;
            CDiagnosticSink&     m_diagnostics;
            std::size_t          m_offset = 0;
            std::size_t          m_line = 1;
            std::size_t          m_column = 1;
        };

        struct STokenSpan
        {
            std::size_t begin = 0;
            std::size_t end = 0;
        };

        class CParser
        {
        public:
            CParser(const std::vector<SToken>& tokens, const SParserLimits& limits, SParseResult& result, CDiagnosticSink& diagnostics)
                : m_tokens(tokens), m_limits(limits), m_result(result), m_diagnostics(diagnostics)
            {
            }

            void Run()
            {
                for (std::size_t index = 0; index + 1 < m_tokens.size() && !m_stopped; ++index)
                {
                    if (m_tokens[index].kind != ETokenKind::Identifier || m_tokens[index + 1].kind != ETokenKind::LeftParenthesis)
                        continue;

                    const std::string& name = m_tokens[index].text;
                    if (name == "CreateObject" || name == "CreateDynamicObject" || name == "CreateDynamicObjectEx")
                        index = ParseObject(index, name);
                    else if (name == "SetObjectMaterial" || name == "SetDynamicObjectMaterial")
                        index = ParseMaterial(index);
                    else if (name == "RemoveBuildingForPlayer")
                        index = ParseRemovedBuilding(index);
                    else if (name == "SetObjectMaterialText" || name == "SetDynamicObjectMaterialText" || name == "AddSimpleModel")
                        index = ReportUnsupportedCall(index, name);
                }
            }

        private:
            bool SplitArguments(std::size_t functionIndex, std::vector<STokenSpan>& arguments, std::size_t& closingParenthesis)
            {
                std::size_t argumentStart = functionIndex + 2;
                int         parenthesisDepth = 1;
                int         bracketDepth = 0;

                for (std::size_t index = argumentStart; index < m_tokens.size(); ++index)
                {
                    switch (m_tokens[index].kind)
                    {
                        case ETokenKind::LeftParenthesis:
                            ++parenthesisDepth;
                            break;
                        case ETokenKind::RightParenthesis:
                            --parenthesisDepth;
                            if (parenthesisDepth == 0)
                            {
                                if (index != argumentStart || !arguments.empty())
                                    arguments.push_back({argumentStart, index});
                                closingParenthesis = index;
                                return true;
                            }
                            break;
                        case ETokenKind::LeftBracket:
                            ++bracketDepth;
                            break;
                        case ETokenKind::RightBracket:
                            --bracketDepth;
                            break;
                        case ETokenKind::Comma:
                            if (parenthesisDepth == 1 && bracketDepth == 0)
                            {
                                arguments.push_back({argumentStart, index});
                                argumentStart = index + 1;
                            }
                            break;
                        case ETokenKind::End:
                            m_diagnostics.Add(EDiagnosticSeverity::Error, m_tokens[functionIndex].location, "unterminated function call");
                            return false;
                        default:
                            break;
                    }
                }
                return false;
            }

            bool ParseDouble(STokenSpan span, double& output)
            {
                bool negative = false;
                if (span.begin < span.end && (m_tokens[span.begin].kind == ETokenKind::Plus || m_tokens[span.begin].kind == ETokenKind::Minus))
                {
                    negative = m_tokens[span.begin].kind == ETokenKind::Minus;
                    ++span.begin;
                }

                if (span.end - span.begin != 1 || m_tokens[span.begin].kind != ETokenKind::Number)
                    return false;

                const std::string& text = m_tokens[span.begin].text;
                if (text.starts_with("0x") || text.starts_with("0X"))
                {
                    std::uint64_t value = 0;
                    const auto [end, error] = std::from_chars(text.data() + 2, text.data() + text.size(), value, 16);
                    if (error != std::errc{} || end != text.data() + text.size())
                        return false;
                    output = negative ? -static_cast<double>(value) : static_cast<double>(value);
                    return std::isfinite(output);
                }

                char* parsedEnd = nullptr;
                output = std::strtod(text.c_str(), &parsedEnd);
                if (parsedEnd != text.c_str() + text.size() || !std::isfinite(output))
                    return false;
                if (negative)
                    output = -output;
                return true;
            }

            bool ParseInteger(STokenSpan span, int& output)
            {
                double value = 0.0;
                if (!ParseDouble(span, value) || std::floor(value) != value || value < std::numeric_limits<int>::min() ||
                    value > std::numeric_limits<int>::max())
                    return false;
                output = static_cast<int>(value);
                return true;
            }

            bool ParseColor(STokenSpan span, std::uint32_t& output)
            {
                bool negative = false;
                if (span.begin < span.end && (m_tokens[span.begin].kind == ETokenKind::Plus || m_tokens[span.begin].kind == ETokenKind::Minus))
                {
                    negative = m_tokens[span.begin].kind == ETokenKind::Minus;
                    ++span.begin;
                }
                if (span.end - span.begin != 1 || m_tokens[span.begin].kind != ETokenKind::Number)
                    return false;

                const std::string& text = m_tokens[span.begin].text;
                if (text.starts_with("0x") || text.starts_with("0X"))
                {
                    if (negative)
                        return false;
                    std::uint64_t value = 0;
                    const auto [end, error] = std::from_chars(text.data() + 2, text.data() + text.size(), value, 16);
                    if (error != std::errc{} || end != text.data() + text.size() || value > std::numeric_limits<std::uint32_t>::max())
                        return false;
                    output = static_cast<std::uint32_t>(value);
                    return true;
                }

                std::int64_t value = 0;
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
                if (error != std::errc{} || end != text.data() + text.size())
                    return false;
                if (negative)
                    value = -value;
                if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::uint32_t>::max())
                    return false;
                output = static_cast<std::uint32_t>(value);
                return true;
            }

            bool ParseString(STokenSpan span, std::string& output)
            {
                if (span.end - span.begin != 1 || m_tokens[span.begin].kind != ETokenKind::String)
                    return false;
                output = m_tokens[span.begin].text;
                return true;
            }

            bool ParseSymbol(STokenSpan span, std::string& output)
            {
                if (span.begin >= span.end || m_tokens[span.begin].kind != ETokenKind::Identifier)
                    return false;

                output = m_tokens[span.begin++].text;
                while (span.begin < span.end)
                {
                    if (span.end - span.begin < 3 || m_tokens[span.begin].kind != ETokenKind::LeftBracket ||
                        (m_tokens[span.begin + 1].kind != ETokenKind::Identifier && m_tokens[span.begin + 1].kind != ETokenKind::Number) ||
                        m_tokens[span.begin + 2].kind != ETokenKind::RightBracket)
                        return false;
                    output += '[';
                    output += m_tokens[span.begin + 1].text;
                    output += ']';
                    span.begin += 3;
                }
                return true;
            }

            std::string FindAssignmentHandle(std::size_t functionIndex)
            {
                if (functionIndex == 0 || m_tokens[functionIndex - 1].kind != ETokenKind::Equal)
                    return {};

                const std::size_t end = functionIndex - 1;
                std::size_t       begin = end;
                while (begin > 0)
                {
                    const ETokenKind kind = m_tokens[begin - 1].kind;
                    if (kind == ETokenKind::Semicolon || kind == ETokenKind::Other)
                        break;
                    --begin;
                }

                // Declarations may precede the actual lvalue. Try each identifier and
                // retain the last complete symbol ending at the assignment operator.
                for (std::size_t candidate = begin; candidate < end; ++candidate)
                {
                    std::string handle;
                    if (ParseSymbol({candidate, end}, handle))
                        return handle;
                }
                return {};
            }

            bool RequireArgumentCount(const std::vector<STokenSpan>& arguments, std::size_t minimum, std::size_t maximum, const SToken& function)
            {
                if (arguments.size() >= minimum && arguments.size() <= maximum)
                    return true;
                m_diagnostics.Add(EDiagnosticSeverity::Error, function.location,
                                  function.text + " expects " + std::to_string(minimum) + " to " + std::to_string(maximum) + " arguments");
                return false;
            }

            bool ParseRequiredInteger(const std::vector<STokenSpan>& arguments, std::size_t index, int& output, const SToken& function)
            {
                if (index < arguments.size() && ParseInteger(arguments[index], output))
                    return true;
                m_diagnostics.Add(EDiagnosticSeverity::Error, function.location,
                                  function.text + " argument " + std::to_string(index + 1) + " must be an integer literal");
                return false;
            }

            bool ParseRequiredDouble(const std::vector<STokenSpan>& arguments, std::size_t index, double& output, const SToken& function)
            {
                if (index < arguments.size() && ParseDouble(arguments[index], output))
                    return true;
                m_diagnostics.Add(EDiagnosticSeverity::Error, function.location,
                                  function.text + " argument " + std::to_string(index + 1) + " must be a finite number literal");
                return false;
            }

            std::size_t ParseObject(std::size_t functionIndex, const std::string& name)
            {
                std::vector<STokenSpan> arguments;
                std::size_t             closingParenthesis = functionIndex;
                if (!SplitArguments(functionIndex, arguments, closingParenthesis))
                    return functionIndex;

                const SToken&     function = m_tokens[functionIndex];
                const std::size_t maximumArguments = name == "CreateObject" ? 8 : (name == "CreateDynamicObject" ? 14 : 18);
                if (!RequireArgumentCount(arguments, 7, maximumArguments, function))
                    return closingParenthesis;

                if (m_result.objects.size() >= m_limits.maxObjects)
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, "object limit exceeded");
                    m_stopped = true;
                    return closingParenthesis;
                }

                SObject object;
                object.location = function.location;
                object.sourceHandle = FindAssignmentHandle(functionIndex);
                object.creationKind = name == "CreateObject"          ? EObjectCreationKind::Object
                                      : name == "CreateDynamicObject" ? EObjectCreationKind::DynamicObject
                                                                      : EObjectCreationKind::DynamicObjectEx;

                bool valid = ParseRequiredInteger(arguments, 0, object.model, function) && ParseRequiredDouble(arguments, 1, object.positionX, function) &&
                             ParseRequiredDouble(arguments, 2, object.positionY, function) && ParseRequiredDouble(arguments, 3, object.positionZ, function) &&
                             ParseRequiredDouble(arguments, 4, object.rotationX, function) && ParseRequiredDouble(arguments, 5, object.rotationY, function) &&
                             ParseRequiredDouble(arguments, 6, object.rotationZ, function);

                if (object.model < 0 || object.model > 65535)
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, "object model must be between 0 and 65535");
                    valid = false;
                }

                if (name == "CreateObject")
                {
                    if (arguments.size() > 7)
                        valid = ParseRequiredDouble(arguments, 7, object.drawDistance, function) && valid;
                }
                else if (name == "CreateDynamicObject")
                {
                    if (arguments.size() > 7)
                        valid = ParseRequiredInteger(arguments, 7, object.virtualWorld, function) && valid;
                    if (arguments.size() > 8)
                        valid = ParseRequiredInteger(arguments, 8, object.interior, function) && valid;
                    if (arguments.size() > 9)
                        valid = ParseRequiredInteger(arguments, 9, object.player, function) && valid;
                    if (arguments.size() > 10)
                        valid = ParseRequiredDouble(arguments, 10, object.streamDistance, function) && valid;
                    if (arguments.size() > 11)
                        valid = ParseRequiredDouble(arguments, 11, object.drawDistance, function) && valid;
                    if (arguments.size() > 12)
                        valid = ParseRequiredInteger(arguments, 12, object.area, function) && valid;
                    if (arguments.size() > 13)
                        valid = ParseRequiredInteger(arguments, 13, object.priority, function) && valid;
                }
                else
                {
                    if (arguments.size() > 7)
                        valid = ParseRequiredDouble(arguments, 7, object.streamDistance, function) && valid;
                    if (arguments.size() > 8)
                        valid = ParseRequiredDouble(arguments, 8, object.drawDistance, function) && valid;
                    if (arguments.size() > 9)
                        m_diagnostics.Add(EDiagnosticSeverity::Warning, function.location,
                                          "CreateDynamicObjectEx filter arrays are ignored by the Texture Studio importer");
                }

                if (!valid)
                    return closingParenthesis;

                const std::size_t objectIndex = m_result.objects.size();
                m_result.objects.push_back(std::move(object));
                if (!m_result.objects.back().sourceHandle.empty())
                    m_objectHandles[m_result.objects.back().sourceHandle] = objectIndex;
                return closingParenthesis;
            }

            std::size_t ParseMaterial(std::size_t functionIndex)
            {
                std::vector<STokenSpan> arguments;
                std::size_t             closingParenthesis = functionIndex;
                if (!SplitArguments(functionIndex, arguments, closingParenthesis))
                    return functionIndex;

                const SToken& function = m_tokens[functionIndex];
                if (!RequireArgumentCount(arguments, 5, 6, function))
                    return closingParenthesis;

                std::string handle;
                int         slot = 0;
                SMaterial   material;
                material.location = function.location;
                bool valid = true;
                if (!ParseSymbol(arguments[0], handle))
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, function.text + " object handle must be a variable or array element");
                    valid = false;
                }
                valid = ParseRequiredInteger(arguments, 1, slot, function) && valid;
                valid = ParseRequiredInteger(arguments, 2, material.sourceModel, function) && valid;
                if (!ParseString(arguments[3], material.txdName) || !ParseString(arguments[4], material.textureName))
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, function.text + " texture dictionary and texture must be string literals");
                    valid = false;
                }
                if (arguments.size() > 5 && !ParseColor(arguments[5], material.color))
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, function.text + " color must be a 32-bit integer literal");
                    valid = false;
                }
                if (slot < 0 || slot > 15)
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, "material slot must be between 0 and 15");
                    valid = false;
                }
                if (material.sourceModel < -1 || material.sourceModel > 65535)
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, "material source model must be between -1 and 65535");
                    valid = false;
                }
                if (!valid)
                    return closingParenthesis;

                const auto handleIterator = m_objectHandles.find(handle);
                if (handleIterator == m_objectHandles.end())
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, "material refers to unknown object handle '" + handle + "'");
                    return closingParenthesis;
                }
                if (m_materialCount >= m_limits.maxMaterials)
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, "material limit exceeded");
                    m_stopped = true;
                    return closingParenthesis;
                }

                material.slot = static_cast<unsigned int>(slot);
                auto&      materials = m_result.objects[handleIterator->second].materials;
                const auto existing = std::find_if(materials.begin(), materials.end(),
                                                   [slot](const SMaterial& candidate) { return candidate.slot == static_cast<unsigned int>(slot); });
                if (existing == materials.end())
                {
                    materials.push_back(std::move(material));
                    ++m_materialCount;
                }
                else
                    *existing = std::move(material);
                return closingParenthesis;
            }

            std::size_t ParseRemovedBuilding(std::size_t functionIndex)
            {
                std::vector<STokenSpan> arguments;
                std::size_t             closingParenthesis = functionIndex;
                if (!SplitArguments(functionIndex, arguments, closingParenthesis))
                    return functionIndex;

                const SToken& function = m_tokens[functionIndex];
                if (!RequireArgumentCount(arguments, 6, 6, function))
                    return closingParenthesis;
                if (m_result.removedBuildings.size() >= m_limits.maxRemovedBuildings)
                {
                    m_diagnostics.Add(EDiagnosticSeverity::Error, function.location, "removed building limit exceeded");
                    m_stopped = true;
                    return closingParenthesis;
                }

                SRemovedBuilding building;
                building.location = function.location;
                const bool valid =
                    ParseRequiredInteger(arguments, 1, building.model, function) && ParseRequiredDouble(arguments, 2, building.positionX, function) &&
                    ParseRequiredDouble(arguments, 3, building.positionY, function) && ParseRequiredDouble(arguments, 4, building.positionZ, function) &&
                    ParseRequiredDouble(arguments, 5, building.radius, function);
                if (valid)
                    m_result.removedBuildings.push_back(building);
                return closingParenthesis;
            }

            std::size_t ReportUnsupportedCall(std::size_t functionIndex, const std::string& name)
            {
                std::vector<STokenSpan> arguments;
                std::size_t             closingParenthesis = functionIndex;
                SplitArguments(functionIndex, arguments, closingParenthesis);
                m_diagnostics.Add(EDiagnosticSeverity::Warning, m_tokens[functionIndex].location, name + " is not supported by this importer version");
                return closingParenthesis;
            }

            const std::vector<SToken>&              m_tokens;
            const SParserLimits&                    m_limits;
            SParseResult&                           m_result;
            CDiagnosticSink&                        m_diagnostics;
            std::unordered_map<std::string, size_t> m_objectHandles;
            std::size_t                             m_materialCount = 0;
            bool                                    m_stopped = false;
        };
    }  // namespace

    bool SParseResult::Succeeded() const
    {
        return errorCount == 0;
    }

    SParseResult Parse(std::string_view source, const SParserLimits& limits)
    {
        SParseResult    result;
        CDiagnosticSink diagnostics(result.diagnostics, result.errorCount, limits.maxDiagnostics);
        if (source.size() > limits.maxSourceBytes)
        {
            diagnostics.Add(EDiagnosticSeverity::Error, {1, 1}, "source exceeds size limit");
            return result;
        }

        CLexer                    lexer(source, limits, diagnostics);
        const std::vector<SToken> tokens = lexer.Tokenize();
        CParser                   parser(tokens, limits, result, diagnostics);
        parser.Run();
        return result;
    }
}  // namespace SampMap
