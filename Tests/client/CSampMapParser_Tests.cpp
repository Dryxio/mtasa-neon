/*****************************************************************************/
/*
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Tests/client/CSampMapParser_Tests.cpp
 *  PURPOSE:     Google Test suite for SA-MP map parsing
 *
 *****************************************************************************/

#include "../../Client/mods/deathmatch/logic/CSampMapParser.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
    using SampMap::EDiagnosticSeverity;
    using SampMap::EObjectCreationKind;

    TEST(CSampMapParser, ParsesTextureStudioObjectsMaterialsAndComments)
    {
        constexpr std::string_view source = R"pawn(
        /* Calls in comments must not become objects:
           CreateObject(1, 2, 3, 4, 5, 6, 7); */
        tmpobjid = CreateDynamicObject(
            19456, -10.5, +20.25, 1.0e3,
            0.0, -90.0, 180.0,
            -1, 139, -1, 250.0, 300.0
        ); // generated wall
        SetDynamicObjectMaterial(tmpobjid, 3, 10765,
            "airport//not-a-comment", "white,detail", 0xAABBCCDD);
    )pawn";

        const auto result = SampMap::Parse(source);
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.objects.size(), 1u);
        const auto& object = result.objects.front();
        EXPECT_EQ(object.creationKind, EObjectCreationKind::DynamicObject);
        EXPECT_EQ(object.sourceHandle, "tmpobjid");
        EXPECT_EQ(object.model, 19456);
        EXPECT_DOUBLE_EQ(object.positionX, -10.5);
        EXPECT_DOUBLE_EQ(object.positionY, 20.25);
        EXPECT_DOUBLE_EQ(object.positionZ, 1000.0);
        EXPECT_EQ(object.virtualWorld, -1);
        EXPECT_EQ(object.interior, 139);
        EXPECT_DOUBLE_EQ(object.streamDistance, 250.0);
        EXPECT_DOUBLE_EQ(object.drawDistance, 300.0);
        ASSERT_EQ(object.materials.size(), 1u);
        EXPECT_EQ(object.materials.front().slot, 3u);
        EXPECT_EQ(object.materials.front().txdName, "airport//not-a-comment");
        EXPECT_EQ(object.materials.front().textureName, "white,detail");
        EXPECT_EQ(object.materials.front().color, 0xAABBCCDDu);
    }

    TEST(CSampMapParser, AssociatesReusedAndArrayHandlesWithTheCorrectObject)
    {
        constexpr std::string_view source = R"pawn(
        tmpobjid = CreateObject(100, 1, 2, 3, 4, 5, 6);
        SetObjectMaterial(tmpobjid, 0, 10, "first", "one", -256);
        tmpobjid = CreateObject(200, 7, 8, 9, 10, 11, 12);
        SetObjectMaterial(tmpobjid, 1, 20, "second", "two", 0);
        new g_Object[420];
        g_Object[94] = CreateObject(300, 0, 0, 0, 0, 0, 0);
        SetObjectMaterial(g_Object[94], 2, 30, "array", "slot", 0x01020304);
    )pawn";

        const auto result = SampMap::Parse(source);
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.objects.size(), 3u);
        ASSERT_EQ(result.objects[0].materials.size(), 1u);
        ASSERT_EQ(result.objects[1].materials.size(), 1u);
        ASSERT_EQ(result.objects[2].materials.size(), 1u);
        EXPECT_EQ(result.objects[0].materials[0].color, 0xFFFFFF00u);
        EXPECT_EQ(result.objects[0].materials[0].textureName, "one");
        EXPECT_EQ(result.objects[1].materials[0].textureName, "two");
        EXPECT_EQ(result.objects[2].sourceHandle, "g_Object[94]");
        EXPECT_EQ(result.objects[2].materials[0].textureName, "slot");
    }

    TEST(CSampMapParser, ParsesTextureStudioDynamicObjectExAndBuildingRemoval)
    {
        constexpr std::string_view source = R"pawn(
        object = CreateDynamicObjectEx(19379, 1, 2, 3, 4, 5, 6, 450.0, 325.0);
        RemoveBuildingForPlayer(playerid, 1307, 998.578, -951.070, 41.484, 0.250);
    )pawn";

        const auto result = SampMap::Parse(source);
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(result.objects[0].creationKind, EObjectCreationKind::DynamicObjectEx);
        EXPECT_DOUBLE_EQ(result.objects[0].streamDistance, 450.0);
        EXPECT_DOUBLE_EQ(result.objects[0].drawDistance, 325.0);
        ASSERT_EQ(result.removedBuildings.size(), 1u);
        EXPECT_EQ(result.removedBuildings[0].model, 1307);
        EXPECT_DOUBLE_EQ(result.removedBuildings[0].positionY, -951.070);
        EXPECT_DOUBLE_EQ(result.removedBuildings[0].radius, 0.250);
    }

    TEST(CSampMapParser, LastMaterialAssignmentWinsPerSlot)
    {
        constexpr std::string_view source = R"pawn(
        object = CreateObject(100, 0, 0, 0, 0, 0, 0);
        SetObjectMaterial(object, 0, 10, "first", "first", 0);
        SetObjectMaterial(object, 0, 20, "second", "second", 0xFFFFFFFF);
    )pawn";

        const auto result = SampMap::Parse(source);
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.objects[0].materials.size(), 1u);
        EXPECT_EQ(result.objects[0].materials[0].sourceModel, 20);
        EXPECT_EQ(result.objects[0].materials[0].textureName, "second");
    }

    TEST(CSampMapParser, ReportsPreciseDiagnosticsWithoutExecutingPawnExpressions)
    {
        constexpr std::string_view source = R"pawn(CreateObject(MODEL_ID, 0, 0, 0, 0, 0, 0);
SetObjectMaterial(missing[1], 0, 10, "txd", "texture", 0);
)pawn";

        const auto result = SampMap::Parse(source);
        EXPECT_FALSE(result.Succeeded());
        ASSERT_GE(result.diagnostics.size(), 2u);
        EXPECT_EQ(result.diagnostics[0].severity, EDiagnosticSeverity::Error);
        EXPECT_EQ(result.diagnostics[0].location.line, 1u);
        EXPECT_EQ(result.diagnostics[0].location.column, 1u);
        EXPECT_NE(result.diagnostics[0].message.find("integer literal"), std::string::npos);
        EXPECT_EQ(result.diagnostics[1].location.line, 2u);
        EXPECT_NE(result.diagnostics[1].message.find("unknown object handle"), std::string::npos);
    }

    TEST(CSampMapParser, WarnsAboutKnownUnsupportedTextureStudioCalls)
    {
        constexpr std::string_view source = R"pawn(
        object = CreateObject(100, 0, 0, 0, 0, 0, 0);
        SetObjectMaterialText(object, "Hello, world");
        AddSimpleModel(-1, 19379, -1000, "model.dff", "model.txd");
    )pawn";

        const auto result = SampMap::Parse(source);
        EXPECT_TRUE(result.Succeeded());
        ASSERT_EQ(result.diagnostics.size(), 2u);
        EXPECT_EQ(result.diagnostics[0].severity, EDiagnosticSeverity::Warning);
        EXPECT_NE(result.diagnostics[0].message.find("not supported"), std::string::npos);
    }

    TEST(CSampMapParser, EnforcesConfiguredSafetyLimits)
    {
        SampMap::SParserLimits limits;
        limits.maxObjects = 1;

        constexpr std::string_view source = R"pawn(
        first = CreateObject(100, 0, 0, 0, 0, 0, 0);
        second = CreateObject(200, 0, 0, 0, 0, 0, 0);
    )pawn";

        const auto result = SampMap::Parse(source, limits);
        EXPECT_FALSE(result.Succeeded());
        ASSERT_EQ(result.objects.size(), 1u);
        ASSERT_FALSE(result.diagnostics.empty());
        EXPECT_NE(result.diagnostics.back().message.find("object limit exceeded"), std::string::npos);
    }

    TEST(CSampMapParser, RemainsFailedWhenDiagnosticStorageIsDisabled)
    {
        SampMap::SParserLimits limits;
        limits.maxDiagnostics = 0;

        const auto result = SampMap::Parse("object = CreateObject(BAD_MODEL, 0, 0, 0, 0, 0, 0);", limits);
        EXPECT_FALSE(result.Succeeded());
        EXPECT_GT(result.errorCount, 0u);
        EXPECT_TRUE(result.diagnostics.empty());
    }

    TEST(CSampMapParser, ParsesOptionalExternalCorpus)
    {
        const char* corpusRoot = std::getenv("MTA_SAMP_MAP_CORPUS");
        if (!corpusRoot || !*corpusRoot)
            GTEST_SKIP() << "Set MTA_SAMP_MAP_CORPUS to a directory containing Texture Studio .pwn exports";

        std::size_t fileCount = 0;
        std::size_t successfulFileCount = 0;
        std::size_t objectCount = 0;
        std::size_t materialCount = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(corpusRoot))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".pwn")
                continue;

            std::ifstream     input(entry.path(), std::ios::binary);
            const std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
            const auto        result = SampMap::Parse(source);
            if (result.Succeeded())
                ++successfulFileCount;
            else if (std::getenv("MTA_SAMP_MAP_CORPUS_VERBOSE"))
            {
                const auto firstError = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
                                                     [](const SampMap::SDiagnostic& diagnostic) { return diagnostic.severity == EDiagnosticSeverity::Error; });
                if (firstError != result.diagnostics.end())
                    std::cout << entry.path().filename().string() << ':' << firstError->location.line << ':' << firstError->location.column << ": "
                              << firstError->message << '\n';
            }

            ++fileCount;
            objectCount += result.objects.size();
            for (const auto& object : result.objects)
                materialCount += object.materials.size();
        }

        EXPECT_GT(fileCount, 0u);
        EXPECT_GT(successfulFileCount, 0u);
        EXPECT_GT(objectCount, 0u);
        EXPECT_GT(materialCount, 0u);
        std::cout << "Corpus: " << successfulFileCount << '/' << fileCount << " files parsed without errors, " << objectCount << " objects, " << materialCount
                  << " materials\n";
    }
}  // namespace
