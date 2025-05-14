#include "pch.h"
#include <wx/app.h>
#include <wx/init.h>

class TestWxAppForGTest : public wxAppConsole
{
public:
    virtual bool OnInit() override
    {
        if (!wxAppConsole::OnInit())
        {
            return false;
        }
        return true;
    }
};

wxIMPLEMENT_APP_NO_MAIN(TestWxAppForGTest);

class WxWidgetsGlobalEnvironment : public ::testing::Environment
{
public:
    virtual void SetUp() override
    {
        wxApp::SetInstance(new TestWxAppForGTest());
        char appname[] = "test_runner.exe";
        char *argv_[] = {appname, nullptr};
        int argc_ = 1;

        if (!wxEntryStart(argc_, argv_))
        {
            FAIL() << "wxEntryStart failed. wxWidgets could not be initialized for tests.";
            return;
        }

        if (wxTheApp)
        {
            if (!wxTheApp->CallOnInit())
            {
                FAIL() << "wxTheApp->CallOnInit() failed.";
                wxEntryCleanup();
            }
        }
        else
        {
            FAIL() << "wxTheApp is null after wxEntryStart. wxWidgets initialization incomplete.";
            wxEntryCleanup();
        }
    }

    virtual void TearDown() override
    {
        if (wxTheApp)
        {
            wxTheApp->OnExit();
        }
        wxEntryCleanup();
        wxApp::SetInstance(nullptr);
    }
};

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new WxWidgetsGlobalEnvironment);
    return RUN_ALL_TESTS();
}