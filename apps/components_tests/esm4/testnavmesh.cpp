#include <components/esm4/loadnavm.hpp>

#include "syntheticplugin.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    using namespace testing;
    using namespace ESM4Test;

    // The 32-bit count of an array and its bytes, which can be fewer or more than the count says
    std::string counted(std::uint32_t count, const std::string& elements)
    {
        std::string data;
        append(data, count);
        return data + elements;
    }

    std::string head(std::uint32_t worldSpace = 0x42)
    {
        std::string data;
        append<std::uint32_t>(data, 12); // NVER
        append<std::uint32_t>(data, 0x34); // LCTN
        append<std::uint32_t>(data, worldSpace);
        append<std::uint32_t>(data, 0x1234); // the cell, or the grid of an exterior one
        return data;
    }

    std::string vertices()
    {
        std::string data;
        for (const float value : { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f })
            append(data, value);
        return counted(2, data);
    }

    std::string triangles()
    {
        std::string data;
        for (std::uint16_t value = 1; value <= 8; ++value)
            append(data, value);
        return counted(1, data);
    }

    std::string extConns()
    {
        std::string data;
        append<std::uint32_t>(data, 0xaabbccdd);
        append<std::uint32_t>(data, 0x00010001);
        append<std::uint16_t>(data, 9);
        return counted(1, data);
    }

    std::string doorTriangles()
    {
        std::string data;
        append<std::uint16_t>(data, 7);
        append<std::uint32_t>(data, 0x11223344);
        append<std::uint32_t>(data, 0x00010002);
        return counted(1, data);
    }

    std::string coverTriangles()
    {
        std::string data;
        for (const std::uint16_t value : { 11, 12, 13 })
            append(data, value);
        return counted(3, data);
    }

    // The divisor and the eight floats of the bounds, the part before the segments
    std::string bounds(std::uint32_t divisor)
    {
        std::string data;
        append(data, divisor);
        for (const float value : { 1.5f, 2.5f, -1.f, -2.f, -3.f, 4.f, 5.f, 6.f })
            append(data, value);
        return data;
    }

    std::string segment(const std::vector<std::uint16_t>& indices)
    {
        std::string data;
        append<std::uint32_t>(data, static_cast<std::uint32_t>(indices.size()));
        for (const std::uint16_t index : indices)
            append(data, index);
        return data;
    }

    // Four segments, a divisor of two, one of them empty
    std::string segments()
    {
        return segment({ 1, 2, 3 }) + segment({}) + segment({ 4 }) + segment({ 5, 6 });
    }

    std::string wholeNvnm()
    {
        return head() + vertices() + triangles() + extConns() + doorTriangles() + coverTriangles() + bounds(2)
            + segments();
    }

    std::string emptyNvnm()
    {
        return head() + counted(0, "") + counted(0, "") + counted(0, "") + counted(0, "") + counted(0, "") + bounds(0);
    }

    std::string navmesh(const std::string& subRecords, std::uint32_t id = 5)
    {
        return record("NAVM", id, subRecords);
    }

    std::vector<ESM4::NavMesh> load(const std::string& record, std::size_t cutBytes = 0)
    {
        return loadRecords<ESM4::NavMesh>("NAVM", record, cutBytes);
    }

    std::string loadFailure(const std::string& record, std::size_t cutBytes = 0)
    {
        try
        {
            load(record, cutBytes);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    std::string loadNvnmFailure(const std::string& nvnm)
    {
        return loadFailure(navmesh(subRecord("NVNM", nvnm)));
    }

    TEST(ESM4NavMeshTest, readsEveryField)
    {
        const std::vector<ESM4::NavMesh> records = load(navmesh(subRecord("NVNM", wholeNvnm())));

        ASSERT_EQ(records.size(), 1u);
        EXPECT_EQ(records[0].mId.toUint32(), 5u);
        ASSERT_EQ(records[0].mData.size(), 1u);
        const ESM4::NavMesh::NVNMstruct& nvnm = records[0].mData[0];
        EXPECT_EQ(nvnm.unknownNVER, 12u);
        EXPECT_EQ(nvnm.unknownLCTN, 0x34u);
        EXPECT_EQ(nvnm.worldSpaceId.toUint32(), 0x42u);
        EXPECT_EQ(std::get<ESM::FormId>(nvnm.cellGrid).toUint32(), 0x1234u);

        ASSERT_EQ(nvnm.verticies.size(), 2u);
        EXPECT_EQ(nvnm.verticies[0].x, 1.f);
        EXPECT_EQ(nvnm.verticies[0].z, 3.f);
        EXPECT_EQ(nvnm.verticies[1].x, 4.f);
        EXPECT_EQ(nvnm.verticies[1].z, 6.f);

        ASSERT_EQ(nvnm.triangles.size(), 1u);
        EXPECT_EQ(+nvnm.triangles[0].vertexIndex0, 1);
        EXPECT_EQ(+nvnm.triangles[0].edge0, 4);
        EXPECT_EQ(+nvnm.triangles[0].coverFlags, 8);

        ASSERT_EQ(nvnm.extConns.size(), 1u);
        EXPECT_EQ(+nvnm.extConns[0].unknown, 0xaabbccddu);
        EXPECT_EQ(+nvnm.extConns[0].navMesh, 0x00010001u);
        EXPECT_EQ(+nvnm.extConns[0].triangleIndex, 9);

        ASSERT_EQ(nvnm.doorTriangles.size(), 1u);
        EXPECT_EQ(+nvnm.doorTriangles[0].triangleIndex, 7);
        EXPECT_EQ(+nvnm.doorTriangles[0].unknown, 0x11223344u);
        EXPECT_EQ(+nvnm.doorTriangles[0].doorRef, 0x00010002u);

        EXPECT_THAT(nvnm.coverTriangles, ElementsAre(11, 12, 13));

        EXPECT_EQ(nvnm.divisor, 2u);
        EXPECT_EQ(nvnm.maxXDist, 1.5f);
        EXPECT_EQ(nvnm.maxYDist, 2.5f);
        EXPECT_EQ(nvnm.minX, -1.f);
        EXPECT_EQ(nvnm.minY, -2.f);
        EXPECT_EQ(nvnm.minZ, -3.f);
        EXPECT_EQ(nvnm.maxX, 4.f);
        EXPECT_EQ(nvnm.maxY, 5.f);
        EXPECT_EQ(nvnm.maxZ, 6.f);

        ASSERT_EQ(nvnm.triSegments.size(), 4u);
        EXPECT_THAT(nvnm.triSegments[0], ElementsAre(1, 2, 3));
        EXPECT_THAT(nvnm.triSegments[1], IsEmpty());
        EXPECT_THAT(nvnm.triSegments[2], ElementsAre(4));
        EXPECT_THAT(nvnm.triSegments[3], ElementsAre(5, 6));
    }

    TEST(ESM4NavMeshTest, readsTheGridOfAnExteriorMesh)
    {
        // The grid of Tamriel is two 16-bit numbers, y and then x, where other meshes have the form ID of a cell
        std::string grid;
        append<std::int16_t>(grid, -3);
        append<std::int16_t>(grid, 5);
        const std::string nvnm = head(0x3c).substr(0, 12) + grid + emptyNvnm().substr(16);

        const std::vector<ESM4::NavMesh> records = load(navmesh(subRecord("NVNM", nvnm)));

        ASSERT_EQ(records.size(), 1u);
        ASSERT_EQ(records[0].mData.size(), 1u);
        const ESM4::Grid& result = std::get<ESM4::Grid>(records[0].mData[0].cellGrid);
        EXPECT_EQ(result.y, -3);
        EXPECT_EQ(result.x, 5);
    }

    TEST(ESM4NavMeshTest, readsAMeshWithNoVerticesAndNoSegments)
    {
        const std::vector<ESM4::NavMesh> records = load(navmesh(subRecord("NVNM", emptyNvnm())));

        ASSERT_EQ(records.size(), 1u);
        ASSERT_EQ(records[0].mData.size(), 1u);
        const ESM4::NavMesh::NVNMstruct& nvnm = records[0].mData[0];
        EXPECT_THAT(nvnm.verticies, IsEmpty());
        EXPECT_THAT(nvnm.triangles, IsEmpty());
        EXPECT_THAT(nvnm.extConns, IsEmpty());
        EXPECT_THAT(nvnm.doorTriangles, IsEmpty());
        EXPECT_THAT(nvnm.coverTriangles, IsEmpty());
        EXPECT_EQ(nvnm.divisor, 0u);
        EXPECT_THAT(nvnm.triSegments, IsEmpty());
    }

    TEST(ESM4NavMeshTest, readsTheMeshesOfOneRecordInOrder)
    {
        std::string second = wholeNvnm();
        second.replace(12, 4, std::string("\x78\x56\x00\x00", 4)); // the cell
        const std::vector<ESM4::NavMesh> records = load(navmesh(
            subRecord("NVNM", wholeNvnm()) + valueSubRecord<std::uint32_t>("NNAM", 1) + subRecord("NVNM", second)));

        ASSERT_EQ(records.size(), 1u);
        ASSERT_EQ(records[0].mData.size(), 2u);
        EXPECT_EQ(std::get<ESM::FormId>(records[0].mData[0].cellGrid).toUint32(), 0x1234u);
        EXPECT_EQ(std::get<ESM::FormId>(records[0].mData[1].cellGrid).toUint32(), 0x5678u);
        EXPECT_THAT(records[0].mData[1].triSegments[3], ElementsAre(5, 6));
    }

    TEST(ESM4NavMeshTest, goesOnAfterBytesThatFollowTheSegments)
    {
        // The reader goes on from the end of the sub-record, so a mesh that has more in it than the loader knows does
        // not make the bytes after the segments read as the header of the next sub-record
        std::string second = wholeNvnm();
        second.replace(12, 4, std::string("\x78\x56\x00\x00", 4));
        const std::vector<ESM4::NavMesh> records
            = load(navmesh(subRecord("NVNM", wholeNvnm() + bytePattern(7, 1)) + subRecord("NVNM", second)));

        ASSERT_EQ(records.size(), 1u);
        ASSERT_EQ(records[0].mData.size(), 2u);
        EXPECT_EQ(std::get<ESM::FormId>(records[0].mData[1].cellGrid).toUint32(), 0x5678u);
        EXPECT_THAT(records[0].mData[1].triSegments[0], ElementsAre(1, 2, 3));
    }

    // Each of the five counted arrays before the bounds, in the order of the file, with the others as they are in a
    // whole mesh
    std::string nvnmWithArray(std::size_t which, const std::string& array)
    {
        std::vector<std::string> arrays = { vertices(), triangles(), extConns(), doorTriangles(), coverTriangles() };
        arrays[which] = array;
        std::string data = head();
        for (const std::string& each : arrays)
            data += each;
        return data + bounds(2) + segments();
    }

    TEST(ESM4NavMeshTest, rejectsAnArrayWithMoreElementsThanTheSubRecordHolds)
    {
        const char* const names[] = { "vertices", "triangles", "external connections", "door triangles", "cover" };
        for (std::size_t which = 0; which < 5; ++which)
        {
            SCOPED_TRACE(names[which]);
            // far more than the sub-record has bytes, and the largest count there is
            for (const std::uint32_t count : { std::uint32_t(100000), std::uint32_t(0xffffffff) })
            {
                SCOPED_TRACE(count);
                EXPECT_EQ(loadNvnmFailure(nvnmWithArray(which, counted(count, ""))),
                    "NVNM array is longer than the rest of the sub-record");
            }
        }
    }

    TEST(ESM4NavMeshTest, rejectsAnArrayOneElementLongerThanTheRestOfTheSubRecord)
    {
        // The last array has the bounds and the segments after it, which the count may not reach into
        const std::string before = head() + vertices() + triangles() + extConns() + doorTriangles();
        const std::string rest = bytePattern(32, 1) + bounds(0); // 68 bytes, 34 elements
        EXPECT_EQ(loadNvnmFailure(before + counted(35, rest)), "NVNM array is longer than the rest of the sub-record");
        // 34 elements fit the sub-record, and leave nothing to read the divisor from
        EXPECT_EQ(loadNvnmFailure(before + counted(34, rest)), "NVNM ends inside a field");
    }

    TEST(ESM4NavMeshTest, rejectsADivisorWhoseSquareOfSegmentsHasNoRoom)
    {
        const std::string before = head() + vertices() + triangles() + extConns() + doorTriangles() + coverTriangles();
        for (const std::uint32_t divisor : { std::uint32_t(3), std::uint32_t(65536), std::uint32_t(0xffffffff) })
        {
            SCOPED_TRACE(divisor);
            EXPECT_EQ(loadNvnmFailure(before + bounds(divisor) + segments()),
                "Triangle segments end before the square of divisor");
        }
    }

    TEST(ESM4NavMeshTest, rejectsASegmentWithMoreIndicesThanTheSubRecordHolds)
    {
        const std::string before = head() + vertices() + triangles() + extConns() + doorTriangles() + coverTriangles();
        // the last of the four segments says three indices and has two
        const std::string cut = segment({ 1, 2, 3 }) + segment({}) + segment({ 4 }) + counted(3, bytePattern(4, 5));
        EXPECT_EQ(loadNvnmFailure(before + bounds(2) + cut), "NVNM array is longer than the rest of the sub-record");
    }

    TEST(ESM4NavMeshTest, rejectsASubRecordThatEndsInsideAField)
    {
        const std::string whole = wholeNvnm();
        // inside the header, inside the count of the vertices, inside the divisor and the bounds
        const std::string beforeBounds
            = head() + counted(0, "") + counted(0, "") + counted(0, "") + counted(0, "") + counted(0, "");
        for (const std::string& data :
            { whole.substr(0, 10), whole.substr(0, 18), beforeBounds + bounds(0).substr(0, 2),
                beforeBounds + bounds(0).substr(0, 20), beforeBounds + bounds(0).substr(0, 35) })
        {
            SCOPED_TRACE(data.size());
            EXPECT_EQ(loadNvnmFailure(data), "NVNM ends inside a field");
        }
    }

    TEST(ESM4NavMeshTest, rejectsAFileThatEndsInsideTheSubRecord)
    {
        const std::string plugin = navmesh(subRecord("NVNM", wholeNvnm()));
        // the last index of the last segment is cut
        EXPECT_EQ(loadFailure(plugin, 2), "NVNM ends inside an array");
        // the indices of the last segment, and half of its count, are cut
        EXPECT_EQ(loadFailure(plugin, 6), "NVNM ends inside a field");
    }

    // The corners of a packed triangle, by value
    std::vector<int> corners(const ESM4::NavMesh::PathTriangle& triangle)
    {
        return { triangle.vertices[0], triangle.vertices[1], triangle.vertices[2] };
    }

    std::vector<int> edges(const ESM4::NavMesh::PathTriangle& triangle)
    {
        return { triangle.edges[0], triangle.edges[1], triangle.edges[2] };
    }

    // Bytes written as pairs of hex digits
    std::string hex(std::string_view digits)
    {
        std::string data;
        for (std::size_t i = 0; i + 1 < digits.size(); i += 2)
            data += static_cast<char>(std::stoi(std::string(digits.substr(i, 2)), nullptr, 16));
        return data;
    }

    // The sub-records of NAVM 0014F130 in FalloutNV.esm, as the game ships them: version 11, four vertices, two
    // triangles, a door on the second one, one cell of grid and one external connection
    std::string newVegasMesh()
    {
        return subRecord("NVER", hex("0b000000"))
            + subRecord("DATA", hex("4a4308000400000002000000010000000000000001000000"))
            + subRecord("NVVX",
                hex("0050c546000000c777fcfb450068c646006800c7f6fbfb450088c646000000c776fafb450020c546004c00c74ffffb45"))
            + subRecord("NVTR", hex("0000010002000100ffff000004080000030001000000ffff0000ffff000c0000"))
            + subRecord("NVDP", hex("e2ed140001000000"))
            + subRecord(
                "NVGD", hex("01000000000034430000d0420020c546006800c776fafb450088c646000000c74ffffb45020000000100"))
            + subRecord("NVEX", hex("00000000812d15000000"));
    }

    // NAVM 0007E5A9 in Fallout3.esm, which has a cover triangle too
    std::string fallout3Mesh()
    {
        return subRecord("NVER", hex("0b000000"))
            + subRecord("DATA", hex("b0de01000600000004000000010000000100000001000000"))
            + subRecord("NVVX",
                hex("75b9f94054a22f4572b3fa45e0fabebf925b2045a0d1fa4599eaef42015f37452ac2fa4504d700436983194538c9fa45e0"
                    "7aaec052d918451feefa45bc3a5241d5663745c3b1fa45"))
            + subRecord("NVTR",
                hex("000001000200ffff01000300000801000100030002000200ffff000000084000010004000300ffff0000010002080400"
                    "020005000000ffffffff0000000c0100"))
            + subRecord("NVCA", hex("0200")) + subRecord("NVDP", hex("e2df010003000000"))
            + subRecord("NVGD",
                hex("01000000db4a0643186cf443e07aaec052d91845c3b1fa4504d70043d56637451feefa4504000000010002000300"))
            + subRecord("NVEX", hex("02000000aae507001e00"));
    }

    TEST(ESM4NavMeshTest, readsARealNewVegasMesh)
    {
        const std::vector<ESM4::NavMesh> records = load(navmesh(newVegasMesh(), 0x14f130));

        ASSERT_EQ(records.size(), 1u);
        const ESM4::NavMesh& mesh = records[0];
        EXPECT_THAT(mesh.mData, IsEmpty());
        EXPECT_EQ(mesh.mVersion, 11u);
        EXPECT_EQ(mesh.mCell.toUint32(), 0x8434au);

        ASSERT_EQ(mesh.mVertices.size(), 4u);
        EXPECT_EQ(+mesh.mVertices[0].x, 25256.f);
        EXPECT_EQ(+mesh.mVertices[0].y, -32768.f);
        EXPECT_EQ(+mesh.mVertices[3].x, 25232.f);

        ASSERT_EQ(mesh.mTriangles.size(), 2u);
        EXPECT_THAT(corners(mesh.mTriangles[0]), ElementsAre(0, 1, 2));
        EXPECT_THAT(edges(mesh.mTriangles[0]), ElementsAre(1, 0xffff, 0));
        EXPECT_EQ(+mesh.mTriangles[0].flags, 0x804u);
        EXPECT_THAT(corners(mesh.mTriangles[1]), ElementsAre(3, 1, 0));
        EXPECT_THAT(edges(mesh.mTriangles[1]), ElementsAre(0xffff, 0, 0xffff));
        EXPECT_EQ(+mesh.mTriangles[1].flags, 0xc00u);

        EXPECT_THAT(mesh.mCover, IsEmpty());
        ASSERT_EQ(mesh.mDoors.size(), 1u);
        EXPECT_EQ(+mesh.mDoors[0].door, 0x14ede2u);
        EXPECT_EQ(+mesh.mDoors[0].triangle, 1);

        const ESM4::NavMesh::TriangleGrid& grid = mesh.mTriangleGrid;
        EXPECT_EQ(grid.divisor, 1u);
        EXPECT_EQ(grid.cellX, 180.f);
        EXPECT_EQ(grid.cellY, 104.f);
        EXPECT_EQ(grid.minX, 25232.f);
        EXPECT_EQ(grid.minY, -32872.f);
        EXPECT_EQ(grid.maxX, 25412.f);
        EXPECT_EQ(grid.maxY, -32768.f);
        ASSERT_EQ(grid.cells.size(), 1u);
        EXPECT_THAT(grid.cells[0], ElementsAre(0, 1));

        ASSERT_EQ(mesh.mExternal.size(), 1u);
        EXPECT_EQ(+mesh.mExternal[0].unknown, 0u);
        EXPECT_EQ(+mesh.mExternal[0].navMesh, 0x152d81u);
        EXPECT_EQ(+mesh.mExternal[0].triangleIndex, 0);
    }

    TEST(ESM4NavMeshTest, readsARealFallout3MeshWithCover)
    {
        const std::vector<ESM4::NavMesh> records = load(navmesh(fallout3Mesh(), 0x7e5a9));

        ASSERT_EQ(records.size(), 1u);
        const ESM4::NavMesh& mesh = records[0];
        EXPECT_EQ(mesh.mCell.toUint32(), 0x1deb0u);
        EXPECT_EQ(mesh.mVertices.size(), 6u);
        ASSERT_EQ(mesh.mTriangles.size(), 4u);
        EXPECT_THAT(corners(mesh.mTriangles[3]), ElementsAre(2, 5, 0));
        EXPECT_EQ(+mesh.mTriangles[3].flags, 0x10c00u);
        EXPECT_THAT(mesh.mCover, ElementsAre(2));
        ASSERT_EQ(mesh.mDoors.size(), 1u);
        EXPECT_EQ(+mesh.mDoors[0].door, 0x1dfe2u);
        EXPECT_EQ(+mesh.mDoors[0].triangle, 3);
        ASSERT_EQ(mesh.mTriangleGrid.cells.size(), 1u);
        EXPECT_THAT(mesh.mTriangleGrid.cells[0], ElementsAre(0, 1, 2, 3));
        ASSERT_EQ(mesh.mExternal.size(), 1u);
        EXPECT_EQ(+mesh.mExternal[0].unknown, 2u);
        EXPECT_EQ(+mesh.mExternal[0].navMesh, 0x7e5aau);
        EXPECT_EQ(+mesh.mExternal[0].triangleIndex, 30);
    }

    // No shipped mesh is older than version 11, so the older layouts of the external connections are built here
    TEST(ESM4NavMeshTest, readsTheExternalConnectionsOfEveryVersion)
    {
        std::string old = bytePattern(46, 0x80);
        old.replace(4, 4, hex("44332200"));
        old.replace(44, 2, hex("0700"));
        std::string middle;
        append<std::uint32_t>(middle, 0x00667788);
        append<std::uint16_t>(middle, 9);

        for (const std::uint32_t version : { 1u, 4u })
        {
            SCOPED_TRACE(version);
            std::string data;
            append(data, version);
            const std::vector<ESM4::NavMesh> records
                = load(navmesh(subRecord("NVER", data) + subRecord("NVEX", old + old)));
            ASSERT_EQ(records.size(), 1u);
            ASSERT_EQ(records[0].mExternal.size(), 2u);
            EXPECT_EQ(+records[0].mExternal[1].unknown, 0u);
            EXPECT_EQ(+records[0].mExternal[1].navMesh, 0x00223344u);
            EXPECT_EQ(+records[0].mExternal[1].triangleIndex, 7);
        }
        for (const std::uint32_t version : { 5u, 8u })
        {
            SCOPED_TRACE(version);
            std::string data;
            append(data, version);
            const std::vector<ESM4::NavMesh> records
                = load(navmesh(subRecord("NVER", data) + subRecord("NVEX", middle + middle + middle)));
            ASSERT_EQ(records.size(), 1u);
            ASSERT_EQ(records[0].mExternal.size(), 3u);
            EXPECT_EQ(+records[0].mExternal[2].unknown, 0u);
            EXPECT_EQ(+records[0].mExternal[2].navMesh, 0x00667788u);
            EXPECT_EQ(+records[0].mExternal[2].triangleIndex, 9);
        }

        // without NVER the version is 1, as in the game
        const std::vector<ESM4::NavMesh> records = load(navmesh(subRecord("NVEX", old)));
        ASSERT_EQ(records.size(), 1u);
        EXPECT_EQ(records[0].mVersion, 1u);
        ASSERT_EQ(records[0].mExternal.size(), 1u);
        EXPECT_EQ(+records[0].mExternal[0].navMesh, 0x00223344u);
    }

    TEST(ESM4NavMeshTest, readsAMeshWithNoSubRecords)
    {
        // Broken Steel and The Pitt ship meshes that are a header and nothing else
        const std::vector<ESM4::NavMesh> records = load(navmesh(""));

        ASSERT_EQ(records.size(), 1u);
        EXPECT_EQ(records[0].mVersion, 1u);
        EXPECT_THAT(records[0].mTriangles, IsEmpty());
        EXPECT_THAT(records[0].mTriangleGrid.cells, IsEmpty());
    }

    TEST(ESM4NavMeshTest, rejectsBrokenFallout3SubRecords)
    {
        EXPECT_EQ(
            loadFailure(navmesh(subRecord("NVTR", bytePattern(17, 1)))), "NVTR is not a whole number of elements");
        EXPECT_EQ(
            loadFailure(navmesh(subRecord("NVEX", bytePattern(11, 1)))), "NVEX is not a whole number of elements");
        std::string grid;
        append<std::uint32_t>(grid, 3);
        EXPECT_EQ(loadFailure(navmesh(subRecord("NVGD", grid + bytePattern(32, 1) + bytePattern(10, 0)))),
            "NVGD cells end before the square of divisor");
        EXPECT_EQ(loadFailure(navmesh(subRecord("DATA", hex("0102")))), "DATA ends inside a field");
    }
}
