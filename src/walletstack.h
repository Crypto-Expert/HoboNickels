/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2013
 */
#ifndef WALLETSTACK_H
#define WALLETSTACK_H

#include <QStackedWidget>
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
    /** Ask for passphrase to unlock wallet temporarily */
    void unlockWallet();
    /** Allow user to lock wallet */
    void lockWallet();
    /** Ask for passphrase to unlock wallet for the session to mint */
    void unlockWalletForMint();
    /** Add up all loaded wallets and show total balance */
    void setTotBalance();
    /** Give user information about staking */
    void getStakeWeight(quint64& nMinWeight, quint64& nMaxWeight, quint64& nWeight);
    quint64 getTotStakeWeight();
    /** Give user information about reserve balance */
    quint64 getReserveBalance();
    /** Give user information about Stake For Charity */
    int getStakeForCharityPercent();
    QString getStakeForCharityAddress();
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
