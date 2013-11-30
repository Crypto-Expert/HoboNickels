/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2013
 */
#include "walletstack.h"
#include "walletview.h"
#include "bitcoingui.h"

#include <QMap>
#include <QMessageBox>

WalletStack::WalletStack(QWidget *parent) :
    QStackedWidget(parent),
    clientModel(0),
    bOutOfSync(true)
{
}

WalletStack::~WalletStack()
{
}

bool WalletStack::addWalletView(const QString& name, WalletModel *walletModel)
{
    if (!gui || !clientModel || mapWalletViews.count(name) > 0)
        return false;
    
    WalletView *walletView = new WalletView(this, gui);
    walletView->setBitcoinGUI(gui);
    walletView->setClientModel(clientModel);
    walletView->setWalletModel(walletModel);
    walletView->showOutOfSyncWarning(bOutOfSync);
    addWidget(walletView);
    mapWalletViews[name] = walletView;
    return true;

}

bool WalletStack::removeWalletView(const QString& name)
{
    if (mapWalletViews.count(name) == 0) return false;
    WalletView *walletView = mapWalletViews.take(name);
    removeWidget(walletView);
    return true;
}

bool WalletStack::handleURI(const QString &uri)
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (!walletView) return false;
    
    return walletView->handleURI(uri);
}

void WalletStack::showOutOfSyncWarning(bool fShow)
{
    bOutOfSync = fShow;
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->showOutOfSyncWarning(fShow);
}

void WalletStack::gotoOverviewPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoOverviewPage();
}

void WalletStack::gotoHistoryPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
    {
        if ((WalletView*)currentWidget() == i.value())
          i.value()->gotoHistoryPage(false, true, false);
        else
          i.value()->gotoHistoryPage(false, false, false);
    }
}

void WalletStack::gotoAddressBookPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
    {
       if ((WalletView*)currentWidget() == i.value())
         i.value()->gotoAddressBookPage(false, true, false);
       else
         i.value()->gotoAddressBookPage(false, false, false);
    }
}

void WalletStack::gotoReceiveCoinsPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
    {
       if ((WalletView*)currentWidget() == i.value())
         i.value()->gotoReceiveCoinsPage(false, true, false);
       else
         i.value()->gotoReceiveCoinsPage(false, false, false);
    }
}

void WalletStack::gotoSendCoinsPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoSendCoinsPage();
}

void WalletStack::gotoSignMessageTab(QString addr)
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (walletView) walletView->gotoSignMessageTab(addr);
}

void WalletStack::gotoVerifyMessageTab(QString addr)
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (walletView) walletView->gotoVerifyMessageTab(addr);
}

void WalletStack::encryptWallet(bool status)
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (walletView) walletView->encryptWallet(status);
}

void WalletStack::backupWallet()
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (walletView) walletView->backupWallet();
}

void WalletStack::changePassphrase()
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (walletView) walletView->changePassphrase();
}

void WalletStack::unlockWallet()
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (walletView) walletView->unlockWallet();
}

void WalletStack::setEncryptionStatus()
{
    WalletView *walletView = (WalletView*)currentWidget();
    if (walletView) walletView->setEncryptionStatus();
}

QString WalletStack::getCurrentWallet()
{
  return QString (mapWalletViews.key((WalletView*)currentWidget()));
}

void WalletStack::setCurrentWalletView(const QString& name)
{
    if (mapWalletViews.count(name) == 0) return;
    WalletView *walletView = mapWalletViews.value(name);
    setCurrentWidget(walletView);
    walletView->setEncryptionStatus();

    //Call the pages that have export and only set that connect
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
    {
        bool x;
        if ( mapWalletViews.constBegin() == (i) )
            x=true;
        else
            x=false;

        if ((WalletView*)currentWidget() == i.value())
        {
          i.value()->gotoReceiveCoinsPage(true, true, x);
          i.value()->gotoHistoryPage(true, true, x);
          i.value()->gotoAddressBookPage(true, true, x);
        }
        else
        {
          i.value()->gotoReceiveCoinsPage(true, false, x);
          i.value()->gotoHistoryPage(true, false, x);
          i.value()->gotoAddressBookPage(true, false, x );
        }
     }
}
