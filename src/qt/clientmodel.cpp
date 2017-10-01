#include "clientmodel.h"

#include "guiconstants.h"
#include "peertablemodel.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"

#include "alert.h"
#include "main.h"
#include "ui_interface.h"

#include <QDateTime>
#include <QTimer>
#include <QDebug>

static const qint64 nClientStartupTime = GetTime();
double GetPoSKernelPS(const CBlockIndex* blockindex = NULL);
double GetDifficulty(const CBlockIndex* blockindex);
double GetPoWMHashPS(const CBlockIndex* blockindex = NULL);

ClientModel::ClientModel(OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), optionsModel(optionsModel), peerTableModel(0),
    cachedNumBlocks(0), cachedNumBlocksOfPeers(0), numBlocksAtStartup(-1), pollTimer(0)
{

    peerTableModel = new PeerTableModel(this);
    pollTimer = new QTimer(this);
    pollTimer->setInterval(MODEL_UPDATE_DELAY);
    pollTimer->start();
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));

    subscribeToCoreSignals();
}

ClientModel::~ClientModel()
{

}

int ClientModel::getNumConnections() const
{
    return vNodes.size();
}

int ClientModel::getNumBlocks() const
{
    return nBestHeight;
}

int ClientModel::getProtocolVersion() const
{
    return PROTOCOL_VERSION;
}

double ClientModel::getDifficulty(bool fProofofStake)
{
    if (fProofofStake)
       return GetDifficulty(GetLastBlockIndex(pindexBest,true));
    else
       return GetDifficulty(GetLastBlockIndex(pindexBest,false));
}

double ClientModel::getProofOfStakeReward()
{
    return GetProofOfStakeReward(0, GetLastBlockIndex(pindexBest, true)->nBits, GetLastBlockIndex(pindexBest, true)->nTime, true)/10000;
}

int ClientModel::getLastPoSBlock()
{
    return GetLastBlockIndex(pindexBest,true)->nHeight;
}

qint64 ClientModel::getMoneySupply()
{
   return pindexBest->nMoneySupply;
}

double ClientModel::getPoWMHashPS()
{
   return GetPoWMHashPS();
}

int ClientModel::getNumBlocksAtStartup()
{
    if (numBlocksAtStartup == -1) numBlocksAtStartup = getNumBlocks();
    return numBlocksAtStartup;
}

double ClientModel::getPosKernalPS()
{
    return GetPoSKernelPS();
}

int ClientModel::getStakeTargetSpacing()
{
    return GetTargetSpacing();
}

quint64 ClientModel::getTotalBytesRecv() const
{
    return CNode::GetTotalBytesRecv();
}

quint64 ClientModel::getTotalBytesSent() const
{
    return CNode::GetTotalBytesSent();
}


QDateTime ClientModel::getLastBlockDate(bool fProofofStake) const
{
    if (pindexBest && !fProofofStake)
      return QDateTime::fromTime_t(pindexBest->GetBlockTime());
    else if (pindexBest && fProofofStake)
       return QDateTime::fromTime_t(GetLastBlockIndex(pindexBest,true)->GetBlockTime());
    else
      return QDateTime::fromTime_t(1374635824); // Genesis block's time
}

void ClientModel::updateTimer()
{
    // Get required lock upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;

    // Some quantities (such as number of blocks) change so fast that we don't want to be notified for each change.
    // Periodically check and update with a timer.
    int newNumBlocks = getNumBlocks();
    int newNumBlocksOfPeers = getNumBlocksOfPeers();

    if(cachedNumBlocks != newNumBlocks || cachedNumBlocksOfPeers != newNumBlocksOfPeers)
    {
        cachedNumBlocks = newNumBlocks;
        cachedNumBlocksOfPeers = newNumBlocksOfPeers;

        // ensure we return the maximum of newNumBlocksOfPeers and newNumBlocks to not create weird displays in the GUI
        emit numBlocksChanged(newNumBlocks, std::max(newNumBlocksOfPeers, newNumBlocks));
    }

    emit bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void ClientModel::updateNumConnections(int numConnections)
{
    emit numConnectionsChanged(numConnections);
}

void ClientModel::updateWalletAdded(const QString &name)
{
    emit walletAdded(name);
}

void ClientModel::updateWalletRemoved(const QString &name)
{
    emit walletRemoved(name);
}

void ClientModel::updateAlert(const QString &hash, int status)
{
    // Show error message notification for new alert
    if(status == CT_NEW)
    {
        uint256 hash_256;
        hash_256.SetHex(hash.toStdString());
        CAlert alert = CAlert::getAlertByHash(hash_256);
        if(!alert.IsNull())
        {
            emit message(tr("Network Alert"), QString::fromStdString(alert.strStatusBar), CClientUIInterface::ICON_ERROR);
        }
    }

    emit alertsChanged(getStatusBarWarnings());
}

bool ClientModel::isTestNet() const
{
    return fTestNet;
}

bool ClientModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload();
}

enum BlockSource ClientModel::getBlockSource() const
{
    return BLOCK_SOURCE_NETWORK;
}

int ClientModel::getNumBlocksOfPeers() const
{
    return GetNumBlocksOfPeers();
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(GetWarnings("statusbar"));
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

PeerTableModel *ClientModel::getPeerTableModel()
{
    return peerTableModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatBuildDate() const
{
    return QString::fromStdString(CLIENT_DATE);
}

bool ClientModel::isReleaseVersion() const
{
    return CLIENT_VERSION_IS_RELEASE;
}

QString ClientModel::clientName() const
{
    return QString::fromStdString(CLIENT_NAME);
}

QString ClientModel::formatClientStartupTime() const
{
    return QDateTime::fromTime_t(nClientStartupTime).toString();
}

// Handlers for core signals
static void NotifyBlocksChanged(ClientModel *clientmodel)
{
    // This notification is too frequent. Don't trigger a signal.
    // Don't remove it, though, as it might be useful later.
}

static void NotifyNumConnectionsChanged(ClientModel *clientmodel, int newNumConnections)
{
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged : " + QString::number(newNumConnections);
    QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumConnections));
}

static void NotifyAlertChanged(ClientModel *clientmodel, const uint256 &hash, ChangeType status)
{
    qDebug() << "NotifyAlertChanged : " + QString::fromStdString(hash.GetHex()) + " status=" + QString::number(status);
    QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(hash.GetHex())),
                              Q_ARG(int, status));
}

static void NotifyWalletAdded(ClientModel *clientmodel, const std::string &name)
{
    QString strWalletName = QString::fromStdString(name);

    qDebug() << "NotifyWalletAdded : " + strWalletName;
    QMetaObject::invokeMethod(clientmodel, "updateWalletAdded", Qt::QueuedConnection,
                              Q_ARG(QString, strWalletName));
}

static void NotifyWalletRemoved(ClientModel *clientmodel, const std::string &name)
{
    QString strWalletName = QString::fromStdString(name);

    qDebug() <<"NotifyWalletRemoved : " + strWalletName;
    QMetaObject::invokeMethod(clientmodel, "updateWalletRemoved", Qt::QueuedConnection,
                              Q_ARG(QString, strWalletName));
}

void ClientModel::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.NotifyBlocksChanged.connect(boost::bind(NotifyBlocksChanged, this));
    uiInterface.NotifyNumConnectionsChanged.connect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertChanged, this, _1, _2));
    uiInterface.NotifyWalletAdded.connect(boost::bind(NotifyWalletAdded,this,_1));
    uiInterface.NotifyWalletRemoved.connect(boost::bind(NotifyWalletRemoved,this,_1));

}

void ClientModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.NotifyBlocksChanged.disconnect(boost::bind(NotifyBlocksChanged, this));
    uiInterface.NotifyNumConnectionsChanged.disconnect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertChanged, this, _1, _2));
    uiInterface.NotifyWalletAdded.disconnect(boost::bind(NotifyWalletAdded,this,_1));
    uiInterface.NotifyWalletRemoved.disconnect(boost::bind(NotifyWalletRemoved,this,_1));
}
