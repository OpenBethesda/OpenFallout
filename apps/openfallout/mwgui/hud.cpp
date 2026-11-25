#include "hud.hpp"

#include <algorithm>
#include <cmath>

#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_RenderManager.h>

#include <osg/Math>

#include <components/esm3/loadgmst.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"


#include "../mwscript/falloutscripts.hpp"

namespace OFGui
{
    namespace
    {
        // Fallout 3 and New Vegas have a head-up display of their own (files/data/scripts/omw/falloutstatus.lua), so
        // the bars of health, magicka and fatigue of Morrowind are not shown for them. The scripts of Fallout are there
        // for every content (also Morrowind), so the content is Fallout when it has the dialogue of Fallout, as the
        // window of the dialogue decides too.
        bool isFalloutContent()
        {
            const OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
            return scripts != nullptr && scripts->hasDialogue();
        }
    }

    HUD::HUD()
        : WindowBase("openmw_hud.layout")
    {
        // Energy bars
        getWidget(mHealthFrame, "HealthFrame");
        getWidget(mHealth, "Health");
        getWidget(mMagicka, "Magicka");
        getWidget(mStamina, "Stamina");

        // Drowning bar
        getWidget(mDrowningBar, "DrowningBar");
        getWidget(mDrowningFrame, "DrowningFrame");
        getWidget(mDrowning, "Drowning");
        getWidget(mDrowningFlash, "Flash");
        mDrowning->setProgressRange(200);

        getWidget(mCrosshair, "Crosshair");
        getWidget(mMouseEmulationCursor, "MouseEmulationCursor");

        mMainWidget->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWorldClicked);
        mMainWidget->eventMouseMove += MyGUI::newDelegate(this, &HUD::onWorldMouseOver);
        mMainWidget->eventMouseLostFocus += MyGUI::newDelegate(this, &HUD::onWorldMouseLostFocus);
    }

    HUD::~HUD()
    {
        mMainWidget->eventMouseLostFocus.clear();
        mMainWidget->eventMouseMove.clear();
        mMainWidget->eventMouseButtonClick.clear();
    }

    void HUD::setDrowningTimeLeft(float time, float maxTime)
    {
        size_t progress = static_cast<size_t>(time / maxTime * 200);
        mDrowning->setProgressPosition(progress);

        bool isDrowning = (progress == 0);
        if (isDrowning && !mIsDrowning) // Just started drowning
            mDrowningFlashTheta = 0.0f; // Start out on bright red every time.

        mDrowningFlash->setVisible(isDrowning);
        mIsDrowning = isDrowning;
    }

    void HUD::setDrowningBarVisible(bool visible)
    {
        mDrowningBar->setVisible(visible);
    }

    void HUD::onWorldClicked(MyGUI::Widget* /*sender*/)
    {
        OFBase::WindowManager* winMgr = OFBase::Environment::get().getWindowManager();
        if (!winMgr->isGuiMode() || !winMgr->isConsoleMode())
            return;

        // the console selects the object that the click is on
        winMgr->setConsoleSelectedObject(OFBase::Environment::get().getWorld()->getFocusObject());
    }

    void HUD::onWorldMouseOver(MyGUI::Widget* /*sender*/, int x, int y)
    {
        OFBase::Environment::get().getWindowManager()->changePointer("arrow");
        mWorldMouseOver = true;
    }

    void HUD::onWorldMouseLostFocus(MyGUI::Widget* /*sender*/, MyGUI::Widget* newWidget)
    {
        OFBase::Environment::get().getWindowManager()->changePointer("arrow");
        mWorldMouseOver = false;
    }

    void HUD::onFrame(float dt)
    {
        // the bars of Morrowind are taken away once, in the first frame, when the content is Fallout
        if (!mContentChecked)
        {
            mContentChecked = true;
            if (isFalloutContent())
                setHmsVisible(false);
        }

        if (mDrowningBar->getVisible())
            mDrowningBar->setPosition(
                mMainWidget->getWidth() / 2 - mDrowningFrame->getWidth() / 2, mMainWidget->getTop());

        if (mIsDrowning)
        {
            mDrowningFlashTheta += dt * osg::PIf * 2;

            float intensity = (cos(mDrowningFlashTheta) + 2.0f) / 3.0f;

            mDrowningFlash->setAlpha(intensity);
        }
    }

    void HUD::setCrosshairVisible(bool visible)
    {
        mCrosshair->setVisible(visible);
    }

    void HUD::setCrosshairOwned(bool owned)
    {
        if (owned)
        {
            mCrosshair->changeWidgetSkin("HUD_Crosshair_Owned");
        }
        else
        {
            mCrosshair->changeWidgetSkin("HUD_Crosshair");
        }
    }

    void HUD::setMouseEmulationCursorVisible(bool visible)
    {
        mMouseEmulationCursor->setVisible(visible);
    }

    void HUD::setMouseEmulationCursorPosition(int left, int top)
    {
        mMouseEmulationCursor->setPosition(
            left - mMouseEmulationCursor->getWidth() / 2, top - mMouseEmulationCursor->getHeight() / 2);
    }

    void HUD::setHmsVisible(bool visible)
    {
        visible = visible && !isFalloutContent();
        mHealth->setVisible(visible);
        mMagicka->setVisible(visible);
        mStamina->setVisible(visible);
        // the frames of the bars hold their tooltips and take the clicks
        for (const char* name : { "HealthFrame", "MagickaFrame", "FatigueFrame" })
        {
            MyGUI::Widget* frame = nullptr;
            getWidget(frame, name);
            frame->setVisible(visible);
        }
    }

    void HUD::clear() {}
}
