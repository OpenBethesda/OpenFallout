/*
  Copyright (C) 2015, 2018, 2020 cc9cii

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  cc9cii cc9c@iinet.net.au

  Much of the information on the data structures are based on the information
  from Tes4Mod:Mod_File_Format and Tes5Mod:File_Formats but also refined by
  trial & error.  See http://en.uesp.net/wiki for details.

*/
#ifndef ESM4_NAVM_H
#define ESM4_NAVM_H

#include <cstdint>
#include <vector>

#include <components/esm/defs.hpp>
#include <components/esm/formid.hpp>

#include "cellgrid.hpp"
#include "vertex.hpp"

namespace ESM4
{
    class Reader;
    class Writer;

    struct NavMesh
    {
#pragma pack(push, 1)
        struct Triangle
        {
            std::uint16_t vertexIndex0;
            std::uint16_t vertexIndex1;
            std::uint16_t vertexIndex2;
            std::uint16_t edge0;
            std::uint16_t edge1;
            std::uint16_t edge2;
            std::uint16_t coverMarker;
            std::uint16_t coverFlags;
        };

        struct ExtConnection
        {
            std::uint32_t unknown;
            ESM::FormId32 navMesh;
            std::uint16_t triangleIndex;
        };

        struct DoorTriangle
        {
            std::uint16_t triangleIndex;
            std::uint32_t unknown;
            ESM::FormId32 doorRef;
        };

        /// A triangle of a Fallout 3 or New Vegas mesh, as the file has it (NVTR, 16 bytes; NV 0x692FE7): its corners,
        /// a number for each edge (0xffff for none), and its flags, which the path search tests (NV 0x691140)
        struct PathTriangle
        {
            std::uint16_t vertices[3];
            std::uint16_t edges[3];
            std::uint32_t flags;
        };

        /// A door on a triangle of a Fallout 3 or New Vegas mesh (NVDP, 8 bytes; NV 0x692E52)
        struct DoorLink
        {
            ESM::FormId32 door;
            std::uint16_t triangle;
            std::uint16_t padding;
        };
#pragma pack(pop)

        /// The triangles of a Fallout 3 or New Vegas mesh by where they are (NVGD; NV 0x69DA40): divisor by divisor
        /// cells of the box, the size of a cell, the box, and the triangles in each cell, row after row
        struct TriangleGrid
        {
            std::uint32_t divisor = 0;
            float cellX = 0.f;
            float cellY = 0.f;
            float minX = 0.f;
            float minY = 0.f;
            float minZ = 0.f;
            float maxX = 0.f;
            float maxY = 0.f;
            float maxZ = 0.f;
            std::vector<std::vector<std::uint16_t>> cells;
        };

        struct NVNMstruct
        {
            std::uint32_t unknownNVER;
            std::uint32_t unknownLCTN;
            ESM::FormId worldSpaceId;
            CellGrid cellGrid;
            std::vector<Vertex> verticies;
            std::vector<Triangle> triangles;
            std::vector<ExtConnection> extConns;
            std::vector<DoorTriangle> doorTriangles;
            std::vector<std::uint16_t> coverTriangles;
            std::uint32_t divisor;
            float maxXDist;
            float maxYDist;
            float minX;
            float minY;
            float minZ;
            float maxX;
            float maxY;
            float maxZ;
            // there are divisor^2 segments, each segment is a vector of triangle indices
            std::vector<std::vector<std::uint16_t>> triSegments;

            void load(ESM4::Reader& esm);
        };

        std::vector<NVNMstruct> mData; // Up to 4 skywind cells in one Morrowind cell

        // Fallout 3 and New Vegas keep a mesh in sub-records of its own (NV 0x692950)
        std::uint32_t mVersion = 1; // NVER, 1 when it has none (NV 0x692A32); every shipped mesh has 11
        ESM::FormId mCell; // DATA
        std::vector<Vertex> mVertices; // NVVX
        std::vector<PathTriangle> mTriangles; // NVTR
        std::vector<ExtConnection> mExternal; // NVEX; `unknown` is 0 before version 9
        std::vector<std::uint16_t> mCover; // NVCA, triangles
        std::vector<DoorLink> mDoors; // NVDP
        TriangleGrid mTriangleGrid; // NVGD

        ESM::FormId mId; // from the header
        std::uint32_t mFlags; // from the header, see enum type RecordFlag for details

        void load(ESM4::Reader& reader);
        // void save(ESM4::Writer& writer) const;

        void blank();
        static constexpr ESM::RecNameInts sRecordId = ESM::RecNameInts::REC_NAVM4;
    };

}

#endif // ESM4_NAVM_H
