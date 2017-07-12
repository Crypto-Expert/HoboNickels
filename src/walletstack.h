/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2013
 */
#ifndef WALLETSTACK_H
#define WALLETSTACK_H

#include <QStackedWidget>

#include "util.h"

#include <QMap>
#include <boost/shared_ptr.hpp>

class BitcoinGUI;
class TransactionTableModel;
class ClientModel;
class WalletModel;
class WalletView;
class TransactionView;
class OverviewPage;
class AddressBookPage;
class SendCoinsDialog;
class SignVerifyMessageDialog;
class Notificator;
class RPCConsole;
class StakeForCharityDialog;
class CBitcoinAddress;
class CWalletManager;

QT_BEGIN_NAMESPACE
class QLabel;
class QModelIndex;
QT_END_NAMESPACE

/*
 WalletWidget class. This class represents the wallet view of the app. It takes the place of centralWidget.
 It was added to support multiple wallet functionality. It communicates with both the client and the
 wallet models to give the user an up-to-date view of the current core state.
 */
class WalletStack : public QStackedWidget
{
    Q_OBJECT
public:
    explicit WalletStack(QWidget *parent = 0);
    ~WalletStack();

    void setBitcoinGUI(BitcoinGUI *gui) { this->gui = gui; }
    
    void setClientModel(ClientModel *clientModel) { this->clientModel = clientModel; }

    bool addWalletView(const QString& name, WalletModel *walletModel);
    bool removeWalletView(const QString& name);

    bool handleURI(const QString &uri);
    
    void showOutOfSyncWarning(bool fShow);

private:
    BitcoinGUI *gui;
    ClientModel *clientModel;
    QMap<QString, WalletView*> mapWalletViews;
    QMap<QString, ClientModel*> mapClientModels;
    
    bool bOutOfSync;

public slots:
    void setCurrentWalletView(const QString& name);
    QString getCurrentWallet();
    
    /** Switch to overview (home) page */
    void gotoOverviewPage();
    /** Switch to history (transactions) page */
    void gotoHistoryPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to address book page */
    void gotoAddressBookPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to receive coins page */
    void gotoReceiveCoinsPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to send coins page */
    void gotoSendCoinsPage();
    /** Switch to block browser page */
    void gotoBlockBrowser(QString transactionId);
    
    /** Show Sign/Verify Message dialog and switch to sign message tab */
    void gotoSignMessageTab(QString addr = "");
    /** Show Sign/Verify Message dialog and switch to verify message tab */
    void gotoVerifyMessageTab(QString addr = "");
    
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
    /** Show Stake For Charity Dialog */
    void charityClicked(QString addr = "");
    /** Add up all loaded wallets and show total balance */
    void setTotBalance();
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
    /** Report from Stack about Wallet Encryption */
    bool isWalletLocked();
    
    /** Set the encryption status as shown in the UI.
     @param[in] status            current encryption status
     @see WalletModel::EncryptionStatus
     */
    void setEncryptionStatus();

};

#endif // WALLETSTACK_H
