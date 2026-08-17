/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Runtime collision model serialization
 *
 *****************************************************************************/

#pragma once

#include <CVector.h>
#include <cstdint>
#include <string>
#include <vector>

namespace RuntimeCollision
{
    struct Sphere
    {
        CVector      position;
        float        radius = 0.0f;
        std::uint8_t material = 0;
    };

    struct Box
    {
        CVector      position;
        CVector      size;
        std::uint8_t material = 0;
    };

    struct Mesh
    {
        std::vector<CVector>       vertices;
        std::vector<std::uint32_t> indices;
        std::uint8_t               material = 0;
    };

    struct Model
    {
        std::vector<Sphere> spheres;
        std::vector<Box>    boxes;
        std::vector<Mesh>   meshes;
    };

    bool BuildCOLBuffer(const Model& model, std::string& outBuffer, std::string& outError);
}  // namespace RuntimeCollision
