/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2013
 */
#ifndef WALLETVIEW_H
#define WALLETVIEW_H

#include <QStackedWidget>

#include "util.h"

class BitcoinGUI;
class TransactionTableModel;
class ClientModel;
class WalletModel;
class TransactionView;
class OverviewPage;
class AddressBookPage;
class SendCoinsDialog;
class SignVerifyMessageDialog;
class Notificator;
class RPCConsole;
class BlockBrowser;
class StakeForCharityDialog;
class CBitcoinAddress;

QT_BEGIN_NAMESPACE
class QLabel;
class QModelIndex;
QT_END_NAMESPACE

/*
  WalletWidget class. This class represents the wallet view of the app. It takes the place of centralWidget.
  It was added to support multiple wallet functionality. It communicates with both the client and the
  wallet models to give the user an up-to-date view of the current core state.
*/
class WalletView : public QStackedWidget
{
    Q_OBJECT
public:
    explicit WalletView(QWidget *parent, BitcoinGUI *_gui);
    ~WalletView();

    void setBitcoinGUI(BitcoinGUI *gui);
    /** Set the client model.
        The client model represents the part of the core that communicates with the P2P network, and is wallet-agnostic.
    */
    void setClientModel(ClientModel *clientModel);
    /** Set the wallet model.
        The wallet model represents a bitcoin wallet, and offers access to the list of transactions, address book and sending
        functionality.
    */
    void setWalletModel(WalletModel *walletModel);
    
    bool handleURI(const QString &uri);
    
    void showOutOfSyncWarning(bool fShow);

private:
    BitcoinGUI *gui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    BlockBrowser *blockBrowser;

    OverviewPage *overviewPage;
    QWidget *transactionsPage;
    AddressBookPage *addressBookPage;
    AddressBookPage *receiveCoinsPage;
    SendCoinsDialog *sendCoinsPage;
    SignVerifyMessageDialog *signVerifyMessageDialog;
    StakeForCharityDialog *stakeForCharityDialog;

    QLabel *labelEncryptionIcon;
    QLabel *labelStakingIcon;
    QLabel *labelConnectionsIcon;
    QLabel *labelBlocksIcon;
    QLabel *progressBarLabel;

    QAction *overviewAction;
    QAction *historyAction;
    QAction *quitAction;
    QAction *sendCoinsAction;
    QAction *addressBookAction;
    QAction *signMessageAction;
    QAction *verifyMessageAction;
    QAction *aboutAction;
    QAction *receiveCoinsAction;
    QAction *optionsAction;
    QAction *toggleHideAction;
    QAction *exportAction;
    QAction *encryptWalletAction;
    QAction *unlockWalletAction;
    QAction *lockWalletAction;
    QAction *checkWalletAction;
    QAction *repairWalletAction;
    QAction *backupWalletAction;
    QAction *dumpWalletAction;
    QAction *importWalletAction;
    QAction *backupAllWalletsAction;
    QAction *changePassphraseAction;
    QAction *startStakingAction;
    QAction *stopStakingAction;
    QAction *aboutQtAction;
    QAction *charityAction;

    TransactionView *transactionView;

    /** Create the main UI actions. */
    void createActions();
    /** Create the menu bar and sub-menus. */

public slots:
    /** Switch to overview (home) page */
    void gotoOverviewPage();
    /** Switch to history (transactions) page */
    void gotoHistoryPage(bool fExportOnly =false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to address book page */
    void gotoAddressBookPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to receive coins page */
    void gotoReceiveCoinsPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to send coins page */
    void gotoSendCoinsPage();
    /** Switch to block browser page */
    void gotoBlockBrowser(QString transactionId = "");

    /** Show Sign/Verify Message dialog and switch to sign message tab */
    void gotoSignMessageTab(QString addr = "");
    /** Show Sign/Verify Message dialog and switch to verify message tab */
    void gotoVerifyMessageTab(QString addr = "");

    /** Show incoming transaction notification for new transactions.

        The new items are those between start and end inclusive, under the given parent item.
    */
    void incomingTransaction(const QModelIndex& parent, int start, int /*end*/);
    /** Encrypt the wallet */
    void encryptWallet(bool status);
    /** Check the wallet */
    void checkWallet();
    /** Repair the wallet */
    void repairWallet();
    /** Backup the wallet(s) */
    void backupWallet();
    void backupAllWallets();
    /** Import/Export the wallet's keys */
    void dumpWallet();
    void importWallet();
    /** Change encrypted wallet passphrase */
    void changePassphrase();
    /** Start/Stop the Stake miner thread */
    void startStaking();
    void stopStaking();
    /** Ask for passphrase to unlock wallet temporarily */
    void unlockWallet();
    /** Allow user to lock wallet */
    void lockWallet();
    /** Ask for passphrase to unlock wallet for the session to mint */
    void unlockWalletForMint();
    void charityClicked(QString addr = "");

    void setEncryptionStatus();
    /** Add up all loaded wallets and show total balance */
    void setTotBalance(bool fEmit=true);
    /** Give user information about staking */
    void getStakeWeight(uint64_t& nMinWeight, uint64_t& nMaxWeight, uint64_t& nWeight);
    quint64 getTotStakeWeight();
    /** Give user information about reserve balance */
    quint64 getReserveBalance();
    /** Give user information about Stake For Charity */
    void getStakeForCharity(int& nStakeForCharityPercent,
                            CBitcoinAddress& strStakeForCharityAddress,
                            CBitcoinAddress& strStakeForCharityChangeAddress,
                            qint64& nStakeForCharityMinAmount,
                            qint64& nStakeForCharityMaxAmount);
    /** Report Current Wallet Version */
    int getWalletVersion() const;
    /** Report from View about Wallet Encryptions */
    bool isWalletLocked();


    signals:
    /** Signal that we want to show the main window */
    void showNormalIfMinimized();
    void totBalanceChanged(qint64 totBalance);

};

#endif // WALLETVIEW_H
