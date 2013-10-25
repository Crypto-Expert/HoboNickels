#define BOOST_TEST_MODULE Bitcoin Test Suite
#include <boost/test/unit_test.hpp>

#include "db.h"
#include "main.h"
#include "wallet.h"


CWalletManager* pWalletManager;
// TODO: get rid of pwalletMain.
CWallet* pwalletMain;
CClientUIInterface uiInterface;

extern bool fPrintToConsole;
extern void noui_connect();

struct TestingSetup {
    TestingSetup() {
        fPrintToDebugger = true; // don't want to write to debug.log file
        noui_connect();
        bitdb.MakeMock();
        LoadBlockIndex(true);
        pWalletManager = new CWalletManager();
        std::ostringstream ossErrors;
        pWalletManager->LoadWallet("", ossErrors);
        pwalletMain = pWalletManager->GetDefaultWallet().get();
    }
    ~TestingSetup()
    {
        delete pWalletManager;
        pWalletManager = NULL;
        pwalletMain = NULL;
        bitdb.Flush(true);
    }
};

BOOST_GLOBAL_FIXTURE(TestingSetup);

void Shutdown(void* parg)
{
  exit(0);
}

void StartShutdown()
{
  exit(0);
}

