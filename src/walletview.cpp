/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2013
 */
#include "walletview.h"
#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "charitydialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "guiutil.h"
#include "ui_interface.h"
#include "blockbrowser.h"


#include <QVBoxLayout>
#include <QActionGroup>
#include <QAction>
#include <QLabel>
#if QT_VERSION < 0x050000
#include <QDesktopServices>
#else
#include <QStandardPaths>
#endif
#include <QFileDialog>

WalletView::WalletView(QWidget *parent, BitcoinGUI *_gui):
    QStackedWidget(parent),
    gui(_gui),
    clientModel(0),
    walletModel(0),
    encryptWalletAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    changePassphraseAction(0)
{
    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create tabs
    overviewPage =  new OverviewPage();
    blockBrowser = new BlockBrowser(gui);

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);

    sendCoinsPage = new SendCoinsDialog(gui);

    signVerifyMessageDialog = new SignVerifyMessageDialog(gui);

    stakeForCharityDialog = new StakeForCharityDialog(this);

    addWidget(overviewPage);
    addWidget(transactionsPage);
    addWidget(addressBookPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);
    addWidget(stakeForCharityDialog);


    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), gui, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));
    // Clicking on "Stake For Charity" in the address book sends you to the stake for charity page
    connect(addressBookPage, SIGNAL(stakeForCharitySignal(QString)), this, SLOT(charityClicked(QString)));
    connect(transactionView, SIGNAL(blockBrowserSignal(QString)), this, SLOT(gotoBlockBrowser(QString)));

    gotoOverviewPage();
}

WalletView::~WalletView()
{
}

void WalletView::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send coins"), this);
    sendCoinsAction->setStatusTip(tr("Send coins to a HoboNickels address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive coins"), this);
    receiveCoinsAction->setStatusTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Address Book"), this);
    addressBookAction->setStatusTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setToolTip(addressBookAction->statusTip());
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    charityAction = new QAction(QIcon(":/icons/send"), tr("Stake For &Charity"), this);
    charityAction->setStatusTip(tr("Enable Stake For Charity"));
    charityAction->setToolTip(charityAction->statusTip());
    charityAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    charityAction->setCheckable(true);
    tabGroup->addAction(charityAction);


    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
    connect(charityAction, SIGNAL(triggered()), this, SLOT(charityClicked()));

    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);

    unlockWalletAction = new QAction(QIcon(":/icons/lock_open"), tr("&Unlock Wallet..."), this);
    unlockWalletAction->setStatusTip(tr("Unlock the wallet for minting"));
    unlockWalletAction->setCheckable(true);

    lockWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Lock Wallet..."), this);
    lockWalletAction->setStatusTip(tr("Lock the wallet"));
    lockWalletAction->setCheckable(true);

    checkWalletAction = new QAction(QIcon(":/icons/inspect"), tr("&Check Wallet..."), this);
    checkWalletAction->setStatusTip(tr("Check wallet integrity and report findings"));

    repairWalletAction = new QAction(QIcon(":/icons/repair"), tr("&Repair Wallet..."), this);
    repairWalletAction->setStatusTip(tr("Fix wallet integrity and remove orphans"));

    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));

    dumpWalletAction = new QAction(QIcon(":/icons/export"), tr("&Export Wallet..."), this);
    dumpWalletAction->setStatusTip(tr("Export a wallet's keys to a text file"));

    importWalletAction = new QAction(QIcon(":/icons/import"), tr("&Import Wallet..."), this);
    importWalletAction->setStatusTip(tr("Import keys from text file into wallet"));

    backupAllWalletsAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup All Wallets..."), this);
    backupAllWalletsAction->setStatusTip(tr("Backup all loaded wallets to another location"));

    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));

    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    signMessageAction->setStatusTip(tr("Sign messages with your HoboNickels addresses to prove you own them"));

    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);
    verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified HoboNickels addresses"));

    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setStatusTip(tr("Export the data in the current tab to a file"));
    exportAction->setToolTip(exportAction->statusTip());

    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));
    connect(checkWalletAction, SIGNAL(triggered()), this, SLOT(checkWallet()));
    connect(repairWalletAction, SIGNAL(triggered()), this, SLOT(repairWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(dumpWalletAction, SIGNAL(triggered()), this, SLOT(dumpWallet()));
    connect(importWalletAction, SIGNAL(triggered()), this, SLOT(importWallet()));
    connect(backupAllWalletsAction, SIGNAL(triggered()), this, SLOT(backupAllWallets()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
    connect(unlockWalletAction, SIGNAL(triggered(bool)), this, SLOT(unlockWalletForMint()));
    connect(lockWalletAction, SIGNAL(triggered(bool)), this, SLOT(lockWallet()));
}

void WalletView::setBitcoinGUI(BitcoinGUI *gui)
{
    this->gui = gui;
}

void WalletView::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        overviewPage->setClientModel(clientModel);
        addressBookPage->setOptionsModel(clientModel->getOptionsModel());
        receiveCoinsPage->setOptionsModel(clientModel->getOptionsModel());
    }
}

void WalletView::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Receive and report messages from wallet thread
        connect(walletModel, SIGNAL(message(QString,QString,unsigned int)), gui, SLOT(message(QString,QString,unsigned int)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);
        overviewPage->setWalletModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);
        stakeForCharityDialog->setModel(walletModel);

        setEncryptionStatus();
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), gui, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));

        // Set Total Balance for all loaded wallets.
        overviewPage->setTotBalance(walletModel->getTotBalance());
        connect(walletModel, SIGNAL(totBalanceChanged(qint64)), this, SLOT(setTotBalance()));
    }
}

void WalletView::incomingTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent balloon-spam when initial block download is in progress
    if(!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();

    QString date = ttm->index(start, TransactionTableModel::Date, parent)
                     .data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                      .data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::Type, parent)
                     .data().toString();
    QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                        .data().toString();

    gui->incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address);
}

void WalletView::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    setCurrentWidget(overviewPage);

    gui->exportAction->setEnabled(false);
    disconnect(gui->exportAction, SIGNAL(triggered()), 0, 0);
}

void WalletView::gotoBlockBrowser(QString transactionId)
{
    if(!transactionId.isEmpty())
        blockBrowser->setTransactionId(transactionId);

    blockBrowser->show();
}

void WalletView::gotoHistoryPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    if (fExportOnly && historyAction->isChecked() )
    {
        if (fExportFirstTime)
            disconnect(gui->exportAction, SIGNAL(triggered()), 0, 0);
        if (fExportConnect)
           connect(gui->exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
    }
    else if (fExportOnly && !historyAction->isChecked())
    {
        return;
    }
    else
    {
        historyAction->setChecked(true);
        setCurrentWidget(transactionsPage);
        gui->exportAction->setEnabled(true);
        if (fExportConnect)
          connect(gui->exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));

    }

}

void WalletView::gotoAddressBookPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    if (fExportOnly && addressBookAction->isChecked() )
    {
        if (fExportFirstTime)
            disconnect(gui->exportAction, SIGNAL(triggered()), 0, 0);
        if (fExportConnect)
           connect(gui->exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
    }
    else if (fExportOnly && !addressBookAction->isChecked())
    {
        return;
    }
    else
    {
        addressBookAction->setChecked(true);
        setCurrentWidget(addressBookPage);
        gui->exportAction->setEnabled(true);
        if (fExportConnect)
           connect(gui->exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
    }
}

void WalletView::gotoReceiveCoinsPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    if (fExportOnly && receiveCoinsAction->isChecked() )
    {
        if (fExportFirstTime)
            disconnect(gui->exportAction, SIGNAL(triggered()), 0, 0);
        if (fExportConnect)
           connect(gui->exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
    }
    else if (fExportOnly && !receiveCoinsAction->isChecked())
    {
        return;
    }
    else
    {

       receiveCoinsAction->setChecked(true);
       setCurrentWidget(receiveCoinsPage);
       gui->exportAction->setEnabled(true);
       if (fExportConnect)
          connect(gui->exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
    }
}

void WalletView::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    setCurrentWidget(sendCoinsPage);

    gui->exportAction->setEnabled(false);
    disconnect(gui->exportAction, SIGNAL(triggered()), 0, 0);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handleURI(const QString& strURI)
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI))
    {
        gotoSendCoinsPage();
        emit showNormalIfMinimized();
        return true;
    }
    else
        return false;
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::setTotBalance(bool fEmit)
{
    qint64 newTotBalance = walletModel->getTotBalance();
    overviewPage->setTotBalance(newTotBalance);
    if(fEmit)
       emit totBalanceChanged(newTotBalance);
}

void WalletView::setEncryptionStatus()
{
    gui->setEncryptionStatus(walletModel->getEncryptionStatus());
}

void WalletView::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt:
                                     AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus();
}

void WalletView::checkWallet()
{
    int nMismatchSpent;
    int64_t nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Check the wallet as requested by user
    walletModel->checkWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
       gui->message(tr("Check Wallet Information"),
                    tr("Wallet %1 passed integrity test!\n"
                       "Nothing found to fix.")
                    .arg(gui->getCurrentWallet())
                    ,CClientUIInterface::MSG_INFORMATION);
    else
       gui->message(tr("Check Wallet Information"),
                    tr("Wallet %1 failed integrity test!\n\n"
                       "Mismatched coin(s) found: %2.\n"
                       "Amount in question: %3.\n"
                       "Orphans found: %4.\n\n"
                       "Please backup wallet and run repair wallet.\n")
                          .arg(gui->getCurrentWallet())
                          .arg(nMismatchSpent)
                          .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                          .arg(nOrphansFound)
                    ,CClientUIInterface::MSG_WARNING);
}

void WalletView::repairWallet()
{
    int nMismatchSpent;
    int64_t nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Repair the wallet as requested by user
    walletModel->repairWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
       gui->message(tr("Repair Wallet Information"),
                    tr("Wallet %1 passed integrity test!\n"
                       "Nothing found to fix.")
                    .arg(gui->getCurrentWallet())
                    ,CClientUIInterface::MSG_INFORMATION);
    else
       gui->message(tr("Repair Wallet Information"),
                    tr("Wallet %1 failed integrity test and has been repaired!\n"
                       "Mismatched coin(s) found: %2\n"
                       "Amount affected by repair: %3\n"
                       "Orphans removed: %4\n")
                          .arg(gui->getCurrentWallet())
                          .arg(nMismatchSpent)
                          .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                          .arg(nOrphansFound)
                    ,CClientUIInterface::MSG_WARNING);
}

void WalletView::backupWallet()
{
#if QT_VERSION < 0x050000
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            gui->message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to the location you specified."),
                      CClientUIInterface::MSG_ERROR);
        }
        else
            gui->message(tr("Backup Successful"), tr("The wallet data was successfully saved to the location you specified."),
                      CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::dumpWallet()
{
   if(!walletModel)
      return;

   WalletModel::UnlockContext ctx(walletModel->requestUnlock());
   if(!ctx.isValid())
   {
       // Unlock wallet failed or was cancelled
       return;
   }

#if QT_VERSION < 0x050000
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    QString filename = QFileDialog::getSaveFileName(this, tr("Export Wallet"), saveDir, tr("Wallet Text (*.txt)"));
    if(!filename.isEmpty()) {
        if(!walletModel->dumpWallet(filename)) {
            gui->message(tr("Export Failed"),
                         tr("There was an error trying to save the wallet's keys to the location you specified.\n"
                            "Keys from wallet: %1, were not saved")
                         .arg(gui->getCurrentWallet())
                      ,CClientUIInterface::MSG_ERROR);
        }
        else
          gui->message(tr("Export Successful"),
                       tr("Keys from wallet:%1,\n were saved to:\n %2")
                       .arg(gui->getCurrentWallet())
                       .arg(filename)
                      ,CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::importWallet()
{
   if(!walletModel)
      return;

   WalletModel::UnlockContext ctx(walletModel->requestUnlock());
   if(!ctx.isValid())
   {
       // Unlock wallet failed or was cancelled
       return;
   }

#if QT_VERSION < 0x050000
    QString openDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
    QString openDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    QString filename = QFileDialog::getOpenFileName(this, tr("Import Wallet"), openDir, tr("Wallet Text (*.txt)"));
    if(!filename.isEmpty()) {
        if(!walletModel->importWallet(filename)) {
            gui->message(tr("Import Failed"),
                         tr("There was an error trying to import the file's keys into your wallet.\n"
                            "Some or all keys from:\n %2,\n were not imported into wallet: %1")
                         .arg(gui->getCurrentWallet())
                         .arg(filename)
                      ,CClientUIInterface::MSG_ERROR);
        }
        else
          gui->message(tr("Import Successful"),
                       tr("Keys from:\n %2,\n were imported into wallet: %1.")
                       .arg(gui->getCurrentWallet())
                       .arg(filename)
                      ,CClientUIInterface::MSG_INFORMATION);
    }
}


void WalletView::backupAllWallets()
{
#if QT_VERSION < 0x050000
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Prefix for wallet Data (*)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupAllWallets(filename)) {
            gui->message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to the location you specified."),
                      CClientUIInterface::MSG_ERROR);
        }
        else
            gui->message(tr("Backup Successful"), tr("The wallets were successfully saved to the location you specified."),
                      CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::lockWallet()
{
    if(!walletModel)
        return;
    // Lock wallet when requested by user
    if(walletModel->getEncryptionStatus() == WalletModel::Unlocked)
        walletModel->setWalletLocked(true,"",true);
    gui->message(tr("Lock Wallet Information"),
                 tr("Wallet %1 has been locked.\n"
                    "Proof of Stake has stopped.\n")
                 .arg(gui->getCurrentWallet())
                 ,CClientUIInterface::MSG_INFORMATION);
}

void WalletView::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
 }

void WalletView::unlockWalletForMint()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet user
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::UnlockForMint, this);
        dlg.setModel(walletModel);
        dlg.exec();
        // Only show message if unlock is sucessfull.
        if(walletModel->getEncryptionStatus() == WalletModel::Unlocked)
           gui->message(tr("Unlock Wallet Information"),
                     tr("Wallet %1 has been unlocked. \n"
                        "Proof of Stake has started.\n")
                     .arg(gui->getCurrentWallet())
                     ,CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::charityClicked(QString addr)
{
    charityAction->setChecked(true);
    setCurrentWidget(stakeForCharityDialog);
    if(!addr.isEmpty())
        stakeForCharityDialog->setAddress(addr);

    gui->exportAction->setEnabled(false);
    disconnect(gui->exportAction, SIGNAL(triggered()), 0, 0);
}

void WalletView::getStakeWeight(uint64_t& nMinWeight, uint64_t& nMaxWeight, uint64_t& nWeight)
{
    if(!walletModel)
       return;
    walletModel->getStakeWeight(nMinWeight,nMaxWeight,nWeight);
}

quint64 WalletView::getReserveBalance()
{
    if(!walletModel)
       return 0;
    return walletModel->getReserveBalance();
}

quint64 WalletView::getTotStakeWeight()
{
    if(!walletModel)
       return 0;
    return walletModel->getTotStakeWeight();
}


int WalletView::getWalletVersion() const
{
    if(!walletModel)
       return 0;
    return walletModel->getWalletVersion();
}

bool WalletView::isWalletLocked()
{
    if(!walletModel)
       return false;
    return (walletModel->getEncryptionStatus() == WalletModel::Locked);

}

void WalletView::getStakeForCharity(int& nStakeForCharityPercent,
                        CBitcoinAddress& strStakeForCharityAddress,
                        CBitcoinAddress& strStakeForCharityChangeAddress,
                        qint64& nStakeForCharityMinAmount,
                        qint64& nStakeForCharityMaxAmount)
{
    walletModel->getStakeForCharity(nStakeForCharityPercent,
                                    strStakeForCharityAddress,
                                    strStakeForCharityChangeAddress,
                                    nStakeForCharityMinAmount,
                                    nStakeForCharityMaxAmount);

}
