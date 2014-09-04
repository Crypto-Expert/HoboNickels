#include "blockbrowser.h"
#include "ui_blockbrowser.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"
#include "clientmodel.h"
#include "transactionrecord.h"
#include "txdb.h"

#include <sstream>
#include <string>

double GetPoSKernelPS(const CBlockIndex* blockindex);
double GetDifficulty(const CBlockIndex* blockindex);
double GetPoWMHashPS(const CBlockIndex* blockindex);

using namespace std;

double getBlockHardness(int64 height)
{
    const CBlockIndex* blockindex = getBlockIndex(height);

    int64 nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

const CBlockIndex* getBlockIndex(int64 height)
{
    std::string hex = getBlockHash(height);
    uint256 hash(hex);
    return mapBlockIndex[hash];
}

std::string getBlockHash(int64 Height)
{
    if(Height > pindexBest->nHeight) { return ""; }
    if(Height < 0) { return ""; }
    int64 desiredheight;
    desiredheight = Height;
    if (desiredheight < 0 || desiredheight > nBestHeight)
        return 0;

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > desiredheight)
        pblockindex = pblockindex->pprev;
    return  pblockindex->GetBlockHash().GetHex(); // pblockindex->phashBlock->GetHex();
}

int64 getBlockTime(int64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->nTime;
}

std::string getBlockMerkle(int64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->hashMerkleRoot.ToString();//.substr(0,10).c_str();
}

int64 getBlocknBits(int64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->nBits;
}

int64 getBlockNonce(int64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->nNonce;
}

std::string getBlockDebug(int64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->ToString();
}

int64 blocksInPastHours(int64 hours)
{
    int64 wayback = hours * 3600;
    bool check = true;
    int64 height = pindexBest->nHeight;
    int64 heightHour = pindexBest->nHeight;
    int64 utime = (int64)time(NULL);
    int64 target = utime - wayback;

    while(check)
    {
        if(getBlockTime(heightHour) < target)
        {
            check = false;
            return height - heightHour;
        } else {
            heightHour = heightHour - 1;
        }
    }

    return 0;
}

double getTxTotalValue(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        return 0;

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;

    double value = 0;
    double buffer = 0;
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        buffer = value + convertCoins(txout.nValue);
        value = buffer;
    }

    return value;
}

double convertCoins(int64 amount)
{
    // Tranz needs to use options model.
    return (double)amount / (double)COIN;
}

std::string getOutputs(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        return "N/A";

    std::string str = "";
    for (unsigned int i = (tx.IsCoinStake() ? 1 : 0); i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address) )
            address = CNoDestination();

        double buffer = convertCoins(txout.nValue);
        std::string amount = boost::to_string(buffer);
        str.append(CBitcoinAddress(address).ToString());
        str.append(": ");
        str.append(amount);
        str.append(" HBN");
        str.append("\n");
    }

    return str;
}

std::string getInputs(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        return "N/A";

    std::string str = "";
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        uint256 hash;
        const CTxIn& vin = tx.vin[i];

        hash.SetHex(vin.prevout.hash.ToString());
        CTransaction wtxPrev;
        uint256 hashBlock = 0;
        if (!GetTransaction(hash, wtxPrev, hashBlock))
             return "N/A";

        CTxDestination address;
        if (!ExtractDestination(wtxPrev.vout[vin.prevout.n].scriptPubKey, address) )
            address = CNoDestination();

        double buffer = convertCoins(wtxPrev.vout[vin.prevout.n].nValue);
        std::string amount = boost::to_string(buffer);
        str.append(CBitcoinAddress(address).ToString());
        str.append(": ");
        str.append(amount);
        str.append(" HBN");
        str.append("\n");
    }

    return str;
}

double BlockBrowser::getTxFees(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    CTxDB txdb("r");

    if (!GetTransaction(hash, tx, hashBlock))
        return convertCoins(MIN_TX_FEE);

    MapPrevTx mapInputs;
    map<uint256, CTxIndex> mapUnused;
    bool fInvalid;

    if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        return convertCoins(MIN_TX_FEE);

    int64 nTxFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();

    if(tx.IsCoinStake() || tx.IsCoinBase()) {
        ui->feesLabel->setText(QString("Reward"));
        nTxFees *= -1;
    }
    else
        ui->feesLabel->setText(QString("Fees"));

    return convertCoins(nTxFees);
}


BlockBrowser::BlockBrowser(QWidget *parent) :
    QDialog(parent, (Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint)),
    ui(new Ui::BlockBrowser)
{
    ui->setupUi(this);

    setBaseSize(850, 500);

    connect(ui->blockButton, SIGNAL(pressed()), this, SLOT(blockClicked()));
    connect(ui->txButton, SIGNAL(pressed()), this, SLOT(txClicked()));
}

void BlockBrowser::updateExplorer(bool block)
{
    if(block)
    {
        int64 height = ui->heightBox->value();
        if (height > pindexBest->nHeight)
        {
            ui->heightBox->setValue(pindexBest->nHeight);
            height = pindexBest->nHeight;
        }

        const CBlockIndex* pindex = getBlockIndex(height);

        ui->heightLabelBE1->show();
        ui->heightLabelBE1->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->heightLabelBE2->show();
        ui->heightLabelBE1->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->hashLabel->show();
        ui->hashLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->hashBox->show();
        ui->hashBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->merkleLabel->show();
        ui->merkleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->merkleBox->show();
        ui->merkleBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->nonceLabel->show();
        ui->nonceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->nonceBox->show();
        ui->nonceBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->bitsLabel->show();
        ui->bitsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->bitsBox->show();
        ui->bitsBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->timeLabel->show();
        ui->timeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->timeBox->show();
        ui->timeBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->hardLabel->show();
        ui->hardLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->hardBox->show();;
        ui->hardBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->pawLabel->show();
        ui->pawLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->pawBox->show();
        ui->pawBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->heightLabelBE1->setText(QString::number(height));
        ui->hashBox->setText(QString::fromUtf8(getBlockHash(height).c_str()));
        ui->merkleBox->setText(QString::fromUtf8(getBlockMerkle(height).c_str()));
        ui->bitsBox->setText(QString::number(getBlocknBits(height)));
        ui->nonceBox->setText(QString::number(getBlockNonce(height)));
        ui->timeBox->setText(QString::fromUtf8(DateTimeStrFormat(getBlockTime(height)).c_str()));
        ui->hardBox->setText(QString::number(GetDifficulty(pindex), 'f', 6)); // Tranz change name
        if (pindex->IsProofOfStake()) {
            ui->pawLabel->setText("Block Network Stake Weight");
            ui->pawBox->setText(QString::number(GetPoSKernelPS(pindex), 'f', 3) + " "); //Tranz change name
        }
        else {
            ui->pawLabel->setText("Block Hash Rate");
            ui->pawBox->setText(QString::number(GetPoWMHashPS(pindex), 'f', 3) + " MH/s");
        }
    }

    if(block == false) {
        std::string txid = ui->txBox->text().toUtf8().constData();
        ui->txID->show();
        ui->txID->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->txLabel->show();
        ui->txLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->valueLabel->show();
        ui->valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->valueBox->show();
        ui->valueBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->inputLabel->show();
        ui->inputLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->inputBox->show();
        ui->inputBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->outputLabel->show();
        ui->outputLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->outputBox->show();
        ui->outputBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->feesLabel->show();
        ui->feesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->feesBox->show();
        ui->feesBox->setTextInteractionFlags(Qt::TextSelectableByMouse);

        ui->valueBox->setText(QString::number(getTxTotalValue(txid), 'f', 6) + " HBN");
        ui->txID->setText(QString::fromUtf8(txid.c_str()));
        ui->outputBox->setText(QString::fromUtf8(getOutputs(txid).c_str()));
        ui->inputBox->setText(QString::fromUtf8(getInputs(txid).c_str()));
        ui->feesBox->setText(QString::number(getTxFees(txid), 'f', 6) + " HBN");
    }
}

void BlockBrowser::setTransactionId(const QString &transactionId)
{
    ui->txBox->setText(transactionId);
    ui->txBox->setFocus();
    updateExplorer(false);

    uint256 hash;
    hash.SetHex(transactionId.toStdString());

    CTransaction tx;
    uint256 hashBlock = 0;
    if (GetTransaction(hash, tx, hashBlock))
    {
        CBlockIndex* pblockindex = mapBlockIndex[hashBlock];
        ui->heightBox->setValue(pblockindex->nHeight);
        updateExplorer(true);
    }
}

void BlockBrowser::txClicked()
{
    updateExplorer(false);
}

void BlockBrowser::blockClicked()
{
    updateExplorer(true);
}

void BlockBrowser::setModel(ClientModel *model)
{
    this->model = model;
}

BlockBrowser::~BlockBrowser()
{
    delete ui;
}
