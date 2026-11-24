#ifndef OPENFALLOUT_GAME_MWGUI_HUD_H
#define OPENFALLOUT_GAME_MWGUI_HUD_H

#include <memory>

#include <components/esm3/refnum.hpp>

#include "windowbase.hpp"

namespace MyGUI
{
    class ImageBox;
    class ProgressBar;
    class Widget;
}

namespace OFWorld
{
    class Ptr;
}

namespace OFGui
{
    class HUD : public WindowBase
    {
    public:
        HUD();
        virtual ~HUD();

        /// Set time left for the player to start drowning
        /// @param time time left to start drowning
        /// @param maxTime how long we can be underwater (in total) until drowning starts
        void setDrowningTimeLeft(float time, float maxTime);
        void setDrowningBarVisible(bool visible);

        void setHmsVisible(bool visible);

        void setCrosshairVisible(bool visible);
        void setCrosshairOwned(bool owned);

        void setMouseEmulationCursorVisible(bool visible);
        void setMouseEmulationCursorPosition(int left, int top);

        void onFrame(float dt) override;

        bool getWorldMouseOver() { return mWorldMouseOver; }

        void setEnemy(const OFWorld::Ptr& enemy);

        void clear() override;

    private:
        MyGUI::ProgressBar *mHealth = nullptr, *mMagicka = nullptr, *mStamina = nullptr, *mEnemyHealth = nullptr,
                           *mDrowning = nullptr;
        MyGUI::Widget* mHealthFrame = nullptr;
        MyGUI::ImageBox* mCrosshair = nullptr;
        MyGUI::ImageBox* mMouseEmulationCursor = nullptr;
        MyGUI::Widget *mDrowningBar = nullptr, *mDrowningFrame = nullptr, *mDrowningFlash = nullptr;
        ESM::RefNum mEnemyActor;

        float mEnemyHealthTimer = -1;
        float mDrowningFlashTheta = 0.f;

        bool mWorldMouseOver = false;
        bool mIsDrowning = false;
        bool mContentChecked = false;

        void onWorldClicked(MyGUI::Widget* sender);
        void onWorldMouseOver(MyGUI::Widget* sender, int x, int y);
        void onWorldMouseLostFocus(MyGUI::Widget* sender, MyGUI::Widget* newWidget);

        void updateEnemyHealthBar();
    };
}

#endif
