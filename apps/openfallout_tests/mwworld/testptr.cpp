#include "apps/openfallout/mwclass/misc.hpp"
#include "apps/openfallout/mwworld/esmstore.hpp"
#include "apps/openfallout/mwworld/livecellref.hpp"
#include "apps/openfallout/mwworld/ptr.hpp"
#include "apps/openfallout/mwworld/worldmodel.hpp"

#include <components/esm3/loadmisc.hpp>
#include <components/esm3/readerscache.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace OFWorld
{
    namespace
    {
        using namespace testing;

        TEST(OFWorldPtrTest, toStringShouldReturnHumanReadableTextRepresentationOfPtrWithNullRef)
        {
            Ptr ptr;
            EXPECT_EQ(ptr.toString(), "null object");
        }

        TEST(OFWorldPtrTest, toStringShouldReturnHumanReadableTextRepresentationOfPtrWithDeletedRef)
        {
            OFClass::Miscellaneous::registerSelf();
            ESM::Miscellaneous item;
            item.blank();
            item.mId = ESM::RefId::stringRefId("Bottle");
            ESMStore store;
            store.insert(item);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = item.mId;
            cellRef.mRefNum = ESM::RefNum{ .mIndex = 0x2a, .mContentFile = 0xd };
            LiveCellRef<ESM::Miscellaneous> liveCellRef(cellRef, &item);
            liveCellRef.mData.setDeletedByContentFile(true);
            Ptr ptr(&liveCellRef);
            EXPECT_THAT(ptr.toString(), StrCaseEq("deleted object0xd00002a (Miscellaneous, \"bottle\")"));
        }

        TEST(OFWorldPtrTest, toStringShouldReturnHumanReadableTextRepresentationOfPtr)
        {
            OFClass::Miscellaneous::registerSelf();
            ESM::Miscellaneous item;
            item.blank();
            item.mId = ESM::RefId::stringRefId("Bottle");
            ESMStore store;
            store.insert(item);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = item.mId;
            cellRef.mRefNum = ESM::RefNum{ .mIndex = 0x2a, .mContentFile = 0xd };
            LiveCellRef<ESM::Miscellaneous> liveCellRef(cellRef, &item);
            Ptr ptr(&liveCellRef);
            EXPECT_THAT(ptr.toString(), StrCaseEq("object0xd00002a (Miscellaneous, \"bottle\")"));
        }

        TEST(OFWorldPtrTest, underlyingLiveCellRefShouldBeDeregisteredOnDestruction)
        {
            OFClass::Miscellaneous::registerSelf();
            ESM::Miscellaneous item;
            item.blank();
            item.mId = ESM::RefId::stringRefId("Bottle");
            ESMStore store;
            store.insert(item);
            ESM::ReadersCache readersCache;
            WorldModel worldModel(store, readersCache);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = item.mId;
            cellRef.mRefNum = ESM::FormId{ .mIndex = 0x2a, .mContentFile = 0xd };
            {
                LiveCellRef<ESM::Miscellaneous> liveCellRef(cellRef, &item);
                Ptr ptr(&liveCellRef);
                worldModel.registerPtr(ptr);
                ASSERT_EQ(worldModel.getPtr(cellRef.mRefNum), ptr);
            }
            EXPECT_EQ(worldModel.getPtr(cellRef.mRefNum), Ptr());
        }
    }
}
