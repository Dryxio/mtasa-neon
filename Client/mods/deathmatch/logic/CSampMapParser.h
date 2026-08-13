/*****************************************************************************/
/*
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CSampMapParser.h
 *  PURPOSE:     Parser for the literal map format exported by SA-MP editors
 *
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace SampMap
{
    enum class EDiagnosticSeverity
    {
        Warning,
        Error,
    };

    struct SSourceLocation
    {
        std::size_t line = 1;
        std::size_t column = 1;
    };

    struct SDiagnostic
    {
        EDiagnosticSeverity severity = EDiagnosticSeverity::Error;
        SSourceLocation     location;
        std::string         message;
    };

    enum class EObjectCreationKind
    {
        Object,
        DynamicObject,
        DynamicObjectEx,
    };

    struct SMaterial
    {
        unsigned int    slot = 0;
        int             sourceModel = -1;
        std::string     txdName;
        std::string     textureName;
        std::uint32_t   color = 0;
        SSourceLocation location;
    };

    struct SObject
    {
        EObjectCreationKind creationKind = EObjectCreationKind::Object;
        std::string         sourceHandle;
        int                 model = 0;
        double              positionX = 0.0;
        double              positionY = 0.0;
        double              positionZ = 0.0;
        double              rotationX = 0.0;
        double              rotationY = 0.0;
        double              rotationZ = 0.0;

        // These retain the Streamer plugin values so the Lua-facing loader can
        // choose how SA-MP worlds and interiors map onto MTA dimensions.
        int    virtualWorld = -1;
        int    interior = -1;
        int    player = -1;
        int    area = -1;
        int    priority = 0;
        double streamDistance = 200.0;
        double drawDistance = 0.0;

        SSourceLocation        location;
        std::vector<SMaterial> materials;
    };

    struct SRemovedBuilding
    {
        int             model = 0;
        double          positionX = 0.0;
        double          positionY = 0.0;
        double          positionZ = 0.0;
        double          radius = 0.0;
        SSourceLocation location;
    };

    struct SParserLimits
    {
        std::size_t maxSourceBytes = 32 * 1024 * 1024;
        std::size_t maxTokens = 2'000'000;
        std::size_t maxObjects = 50'000;
        std::size_t maxRemovedBuildings = 50'000;
        std::size_t maxMaterials = 800'000;
        std::size_t maxStringBytes = 1'024;
        std::size_t maxDiagnostics = 256;
    };

    struct SParseResult
    {
        std::vector<SObject>          objects;
        std::vector<SRemovedBuilding> removedBuildings;
        std::vector<SDiagnostic>      diagnostics;
        std::size_t                   errorCount = 0;

        bool Succeeded() const;
    };

    // This deliberately parses the data-oriented output emitted by Texture
    // Studio rather than arbitrary Pawn. Keeping the grammar literal-only makes
    // map imports deterministic and prevents map files from executing code.
    SParseResult Parse(std::string_view source, const SParserLimits& limits = {});
}  // namespace SampMap
