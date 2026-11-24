#include "processlevels.hpp"

namespace FalloutScript
{
    std::string_view processLevelName(ProcessLevel level)
    {
        switch (level)
        {
            case ProcessLevel::High:
                return "High";
            case ProcessLevel::MiddleHigh:
                return "Middle High";
            case ProcessLevel::MiddleLow:
                return "Middle Low";
            case ProcessLevel::Low:
                return "Low";
            case ProcessLevel::None:
                break;
        }
        return "None";
    }

    ProcessLevel desiredProcessLevel(const ProcessInputs& inputs)
    {
        const bool interesting = inputs.mBulkLoading || inputs.mHas3D || inputs.mTeammate;
        if (interesting)
        {
            if (inputs.mCell == CellLoad::Attached)
                return ProcessLevel::High;
            if (inputs.mCell == CellLoad::Loaded)
                return ProcessLevel::MiddleHigh;
        }
        if (inputs.mCellKeepsActors || inputs.mCell != CellLoad::None)
            return ProcessLevel::MiddleLow;
        return ProcessLevel::Low;
    }

    bool isDeadState(LifeState state, bool strict)
    {
        if (state == LifeState::Dying || state == LifeState::Dead)
            return true;
        return !strict && state == LifeState::EssentialDown;
    }
}
