#ifndef BLOCKBROWSER_H
#define BLOCKBROWSER_H

#include "clientmodel.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"
#include <QDialog>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QStringList>
#include <QMap>
#include <QSettings>
#include <QSlider>

double getTxTotalValue(std::string);
double convertCoins(int64);
int64 getBlockTime(int64);
int64 getBlocknBits(int64);
int64 getBlockNonce(int64);
int64 blocksInPastHours(int64);
int64 getBlockHashrate(int64);
std::string getInputs(std::string);
std::string getOutputs(std::string);
std::string getBlockHash(int64);
std::string getBlockMerkle(int64);
bool addnode(std::string);
const CBlockIndex* getBlockIndex(int64);


namespace Ui {
class BlockBrowser;
}
class ClientModel;

class BlockBrowser : public QDialog
{
    Q_OBJECT

public:
    explicit BlockBrowser(QWidget *parent = 0);
    ~BlockBrowser();

    void setTransactionId(const QString &transactionId);
    void setModel(ClientModel *model);


public slots:

    void blockClicked();
    void txClicked();
    void updateExplorer(bool);
    double getTxFees(std::string);


private slots:

private:
    Ui::BlockBrowser *ui;
    ClientModel *model;

};

#endif // BLOCKBROWSER_H
