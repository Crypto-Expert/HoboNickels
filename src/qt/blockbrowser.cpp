#include "blockbrowser.h"
#include "ui_blockbrowser.h"
#include "main.h"
#include "base58.h"
#include "clientmodel.h"
#include "txdb.h"

double GetPoSKernelPS(const CBlockIndex* blockindex);
double GetDifficulty(const CBlockIndex* blockindex);
double GetPoWMHashPS(const CBlockIndex* blockindex);

using namespace std;

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
    connect(ui->closeButton, SIGNAL(pressed()), this, SLOT(close()));
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

        ui->heightLabelBE1->setText(QString::number(height));
        ui->hashBox->setText(QString::fromUtf8(getBlockHash(height).c_str()));
        ui->merkleBox->setText(QString::fromUtf8(getBlockMerkle(height).c_str()));
        ui->bitsBox->setText(QString::number(getBlocknBits(height)));
        ui->nonceBox->setText(QString::number(getBlockNonce(height)));
        ui->timeBox->setText(QString::fromUtf8(DateTimeStrFormat(getBlockTime(height)).c_str()));
        ui->diffBox->setText(QString::number(GetDifficulty(pindex), 'f', 6));
        if (pindex->IsProofOfStake()) {
            ui->hashRateLabel->setText("Block Network Stake Weight:");
            ui->diffLabel->setText("PoS Block Difficulty:");
            ui->hashRateBox->setText(QString::number(GetPoSKernelPS(pindex), 'f', 3) + " ");
        }
        else {
            ui->hashRateLabel->setText("Block Hash Rate:");
            ui->diffLabel->setText("PoW Block Difficulty:");
            ui->hashRateBox->setText(QString::number(GetPoWMHashPS(pindex), 'f', 3) + " MH/s");
        }
    }
    else {
        std::string txid = ui->txBox->text().toUtf8().constData();

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
