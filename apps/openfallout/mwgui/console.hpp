#ifndef MWGUI_CONSOLE_H
#define MWGUI_CONSOLE_H

#include <list>
#include <string>
#include <vector>

#include <components/files/configurationmanager.hpp>

#include "../mwbase/windowmanager.hpp"

#include "referenceinterface.hpp"
#include "windowbase.hpp"

namespace OFGui
{
    class Console : public WindowBase, public ReferenceInterface
    {
    public:
        /// Set the implicit object for script execution
        void setSelectedObject(const OFWorld::Ptr& object);
        OFWorld::Ptr getSelectedObject() const { return mPtr; }

        MyGUI::EditBox* mCommandLine;
        MyGUI::EditBox* mHistory;
        MyGUI::EditBox* mSearchTerm;
        MyGUI::Button* mNextButton;
        MyGUI::Button* mPreviousButton;
        MyGUI::Button* mCaseSensitiveToggleButton;
        MyGUI::Button* mRegExSearchToggleButton;

        typedef std::list<std::string> StringList;

        // History of previous entered commands
        StringList mCommandHistory;
        StringList::iterator mCurrent;
        std::string mEditString;
        std::ofstream mCommandHistoryFile;

        Console(int w, int h, Files::ConfigurationManager& cfgMgr);
        ~Console();

        void onOpen() override;

        // Print a message to the console, in specified color.
        void print(const std::string& msg, std::string_view color = OFBase::WindowManager::sConsoleColor_Default);

        // These are pre-colored versions that you should use.

        /// Output from successful console command
        void printOK(const std::string& msg);

        /// Error message
        void printError(const std::string& msg);

        void execute(const std::string& command);

        void updateSelectedObjectPtr(const OFWorld::Ptr& currentPtr, const OFWorld::Ptr& newPtr);

        void onFrame(float dt) override { checkReferenceAvailable(); }
        void clear() override;

        void resetReference() override;

        const std::string& getConsoleMode() const { return mConsoleMode; }
        void setConsoleMode(std::string_view mode);

    protected:
        void onReferenceUnavailable() override;

    private:
        std::string mConsoleMode;

        void updateConsoleTitle();

        void commandBoxKeyPress(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char value);
        void acceptCommand(MyGUI::EditBox* sender);

        enum class SearchDirection;
        void toggleCaseSensitiveSearch(MyGUI::Widget* sender);
        void toggleRegExSearch(MyGUI::Widget* sender);
        void acceptSearchTerm(MyGUI::EditBox* sender);
        void findNextOccurrence(MyGUI::Widget* sender);
        void findPreviousOccurrence(MyGUI::Widget* sender);
        void findOccurrence(SearchDirection direction);
        void findInHistoryText(
            const std::string& historyText, SearchDirection direction, size_t firstIndex, size_t lastIndex);
        void findWithRegex(
            const std::string& historyText, SearchDirection direction, size_t firstIndex, size_t lastIndex);
        void findWithStringSearch(
            const std::string& historyText, SearchDirection direction, size_t firstIndex, size_t lastIndex);
        void markOccurrence(size_t textPosition, size_t length);
        size_t mCurrentOccurrenceIndex = std::string::npos;
        size_t mCurrentOccurrenceLength = 0;
        std::string mCurrentSearchTerm;
        bool mCaseSensitiveSearch;
        bool mRegExSearch;

        Files::ConfigurationManager& mCfgMgr;

        void initConsoleHistory();
    };
}
#endif
