#include <gtest/gtest.h>

#include <limits>

#include "apps/openfallout/mwclass/esm4npc.hpp"

namespace OFClass
{
    namespace
    {
        TEST(OFClassNpcBodyTest, aRaceOfTheUsualHeightHasTheCapsuleOfTheExecutables)
        {
            // radius 31 (bhkCharacterControllerCinfo), half height 66.22 (the human skeleton's bound)
            const osg::Vec3f extents = npcBodyHalfExtents(1.f);
            EXPECT_EQ(extents, osg::Vec3f(31.f, 31.f, 66.22f));
        }

        TEST(OFClassNpcBodyTest, aTallerRaceHasAWiderAndTallerBody)
        {
            const osg::Vec3f extents = npcBodyHalfExtents(1.25f);
            EXPECT_FLOAT_EQ(extents.x(), 38.75f);
            EXPECT_FLOAT_EQ(extents.y(), 38.75f);
            EXPECT_FLOAT_EQ(extents.z(), 82.775f);
        }

        TEST(OFClassNpcBodyTest, aHeightThatIsNoNumberOrNotPositiveCountsAsOne)
        {
            const osg::Vec3f usual = npcBodyHalfExtents(1.f);
            EXPECT_EQ(npcBodyHalfExtents(0.f), usual);
            EXPECT_EQ(npcBodyHalfExtents(-2.f), usual);
            EXPECT_EQ(npcBodyHalfExtents(std::numeric_limits<float>::quiet_NaN()), usual);
            EXPECT_EQ(npcBodyHalfExtents(std::numeric_limits<float>::infinity()), usual);
        }

        TEST(OFClassNpcBodyTest, theHeightOfARaceIsLimitedToWhatTheDataHasMeant)
        {
            EXPECT_EQ(npcBodyHalfExtents(1000.f), npcBodyHalfExtents(4.f));
            EXPECT_EQ(npcBodyHalfExtents(0.001f), npcBodyHalfExtents(0.25f));
        }
    }
}
