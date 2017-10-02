  // Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>

#include "kernel.h"
#include "txdb.h"

using namespace std;

unsigned int GetModiferInterval()
{
   // MODIFIER_INTERVAL: time to elapse before new modifier is computed
   unsigned int nStakeModiferInterval = (fTestNet ? 20 * 60 : 6 * 60 * 60); // Modifier interval - 20min TestNet, 6 hours Mainnet(OLD)

   //if (nBestHeight + 1 > (fTestNet ? MODIFER_TARGET_SWITCH_TESTNET : MODIFER_TARGET_SWITCH))
   //      nStakeModiferInterval = 10 * 60; // set to 10 mins for both (NEW)

   return nStakeModiferInterval;
}

// Hard checkpoints of stake modifiers to ensure they are deterministic
static std::map<int, unsigned int> mapStakeModifierCheckpoints =
    boost::assign::map_list_of
    ( 0,       0x0e00670bu )
    ( 261031,  0x08c12bdb8 )
    ( 435735,  0x0b956cfa0 )
    ( 750000,  0x0b86ffee4 )
    ( 1000000, 0x0a68f1279 )
    ( 2000000, 0x0029173a3 )
    ( 3000000, 0x0ecd4d6c7 )
    ( 4000000, 0x0a655151a )
    ( 5000000, 0x0e02d611e )
    ( 5499330, 0x03026e5af )
    ( 5499331, 0x00e5f8ce1 )
    ( 5499332, 0x091436a27 )
    ( 5500000, 0x0a053fcf3 )
    ( 5520100, 0x05c7752ee )
    ;

int64_t GetWeight(int64_t nIntervalBeginning, int64_t nIntervalEnd)
{
    // Kernel hash weight starts from 0 at the 10-day min age
    // this change increases active coins participating the hash and helps
    // to secure the network when proof-of-stake difficulty is low
    //
    // Maximum TimeWeight is 30 days.

    if ( nIntervalEnd > VERSION1_5_SWITCH_TIME )
        return min(nIntervalEnd - nIntervalBeginning - GetStakeMinAge(), (int64_t)nStakeMaxAge);
    else
        return min(nIntervalEnd - nIntervalBeginning, (int64_t)nStakeMaxAge) - GetStakeMinAge();
}

// Get the last stake modifier and its generation time from a given block
static bool GetLastStakeModifier(const CBlockIndex* pindex, uint64_t& nStakeModifier, int64_t& nModifierTime)
{
    if (!pindex)
        return error("GetLastStakeModifier: null pindex");
    while (pindex && pindex->pprev && !pindex->GeneratedStakeModifier())
        pindex = pindex->pprev;
    if (!pindex->GeneratedStakeModifier())
        return error("GetLastStakeModifier: no generation at genesis block");
    nStakeModifier = pindex->nStakeModifier;
    nModifierTime = pindex->GetBlockTime();
    return true;
}

// Get selection interval section (in seconds)
static int64_t GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert (nSection >= 0 && nSection < 64);
    return (GetModiferInterval() * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1))));
}

// Get stake modifier selection interval (in seconds)
static int64_t GetStakeModifierSelectionInterval()
{
    int64_t nSelectionInterval = 0;
    for (int nSection=0; nSection<64; nSection++)
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    return nSelectionInterval;
}

// select a block from the candidate blocks in vSortedByTimestamp, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// nSelectionIntervalStop.
static bool SelectBlockFromCandidates(vector<pair<int64_t, uint256> >& vSortedByTimestamp, map<uint256, const CBlockIndex*>& mapSelectedBlocks,
    int64_t nSelectionIntervalStop, uint64_t nStakeModifierPrev, const CBlockIndex** pindexSelected)
{
    bool fSelected = false;
    uint256 hashBest = 0;
    *pindexSelected = (const CBlockIndex*) 0;
    BOOST_FOREACH(const PAIRTYPE(int64_t, uint256)& item, vSortedByTimestamp)
    {
        if (!mapBlockIndex.count(item.second))
            return error("SelectBlockFromCandidates: failed to find block index for candidate block %s", item.second.ToString());
        const CBlockIndex* pindex = mapBlockIndex[item.second];
        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop)
            break;
        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0)
            continue;
        // compute the selection hash by hashing its proof-hash and the
        // previous proof-of-stake modifier
        uint256 hashProof = pindex->IsProofOfStake()? pindex->hashProofOfStake : pindex->GetBlockHash();
        CDataStream ss(SER_GETHASH, 0);
        ss << hashProof << nStakeModifierPrev;
        uint256 hashSelection = Hash(ss.begin(), ss.end());
        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        if (pindex->IsProofOfStake())
            hashSelection >>= 32;
        if (fSelected && hashSelection < hashBest)
        {
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*) pindex;
        }
        else if (!fSelected)
        {
            fSelected = true;
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*) pindex;
        }
    }
    LogPrint("stakemodifier", "SelectBlockFromCandidates: selection hash=%s\n", hashBest.ToString());
    return fSelected;
}

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
// Stake modifier consists of bits each of which is contributed from a
// selected block of a given block group in the past.
// The selection of a block is based on a hash of the block's proof-hash and
// the previous stake modifier.
// Stake modifier is recomputed at a fixed time interval instead of every 
// block. This is to make it difficult for an attacker to gain control of
// additional bits in the stake modifier, even after generating a chain of
// blocks.
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier)
{
    nStakeModifier = 0;
    unsigned int nStakeModifierInterval = GetModiferInterval();
    fGeneratedStakeModifier = false;
    if (!pindexPrev)
    {
        fGeneratedStakeModifier = true;
        return true;  // genesis block's modifier is 0
    }
    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    int64_t nModifierTime = 0;
    if (!GetLastStakeModifier(pindexPrev, nStakeModifier, nModifierTime))
        return error("ComputeNextStakeModifier: unable to get last modifier");

    LogPrint("stakemodifier", "ComputeNextStakeModifier: prev modifier=0x%016x time=%s\n", nStakeModifier, DateTimeStrFormat(nModifierTime));

    if (nModifierTime / nStakeModifierInterval >= pindexPrev->GetBlockTime() / nStakeModifierInterval)
        return true;

    // Sort candidate blocks by timestamp
    vector<pair<int64_t, uint256> > vSortedByTimestamp;
    vSortedByTimestamp.reserve(64 * nStakeModifierInterval / GetTargetSpacing());
    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / nStakeModifierInterval) * nStakeModifierInterval - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;
    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart)
    {
        vSortedByTimestamp.push_back(make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->pprev;
    }
    int nHeightFirstCandidate = pindex ? (pindex->nHeight + 1) : 0;
    reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end());
    sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    // Select 64 blocks from candidate blocks to generate stake modifier
    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = nSelectionIntervalStart;
    map<uint256, const CBlockIndex*> mapSelectedBlocks;
    for (int nRound=0; nRound<min(64, (int)vSortedByTimestamp.size()); nRound++)
    {
        // add an interval section to the current selection round
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);
        // select a block from the candidates of current round
        if (!SelectBlockFromCandidates(vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier, &pindex))
            return error("ComputeNextStakeModifier: unable to select block at round %d", nRound);
        // write the entropy bit of the selected block
        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);
        // add the selected block from candidates to selected list
        mapSelectedBlocks.insert(make_pair(pindex->GetBlockHash(), pindex));
        LogPrint("stakemodifier", "ComputeNextStakeModifier: selected round %d stop=%s height=%d bit=%d\n",
            nRound, DateTimeStrFormat(nSelectionIntervalStop), pindex->nHeight, pindex->GetStakeEntropyBit());
    }

    // Print selection map for visualization of the selected blocks
    if (LogAcceptCategory("stakemodifier"))
    {
        string strSelectionMap = "";
        // '-' indicates proof-of-work blocks not selected
        strSelectionMap.insert(0, pindexPrev->nHeight - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;
        while (pindex && pindex->nHeight >= nHeightFirstCandidate)
        {
            // '=' indicates proof-of-stake blocks not selected
            if (pindex->IsProofOfStake())
                strSelectionMap.replace(pindex->nHeight - nHeightFirstCandidate, 1, "=");
            pindex = pindex->pprev;
        }
        BOOST_FOREACH(const PAIRTYPE(uint256, const CBlockIndex*)& item, mapSelectedBlocks)
        {
            // 'S' indicates selected proof-of-stake blocks
            // 'W' indicates selected proof-of-work blocks
            strSelectionMap.replace(item.second->nHeight - nHeightFirstCandidate, 1, item.second->IsProofOfStake()? "S" : "W");
        }
        LogPrintf("ComputeNextStakeModifier: selection height [%d, %d] map %s\n", nHeightFirstCandidate, pindexPrev->nHeight, strSelectionMap);
    }
    LogPrint("stakemodifier", "ComputeNextStakeModifier: new modifier=0x%016x time=%s\n", nStakeModifierNew, DateTimeStrFormat(pindexPrev->GetBlockTime()));

    nStakeModifier = nStakeModifierNew;
    fGeneratedStakeModifier = true;
    return true;
}

// The stake modifier used to hash for a stake kernel is chosen as the stake
// modifier about a selection interval later than the coin generating the kernel
static bool GetKernelStakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime, bool fPrintProofOfStake)
{
    nStakeModifier = 0;
    if (!mapBlockIndex.count(hashBlockFrom))
        return error("GetKernelStakeModifier() : block not indexed");
    const CBlockIndex* pindexFrom = mapBlockIndex[hashBlockFrom];
    nStakeModifierHeight = pindexFrom->nHeight;
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = pindexFrom;
    // loop to find the stake modifier later by a selection interval
    while (nStakeModifierTime < pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval)
    {
        if (!pindex->pnext)
        {   // reached best block; may happen if node is behind on block chain
            if (fPrintProofOfStake || (pindex->GetBlockTime() + GetStakeMinAge() - nStakeModifierSelectionInterval > GetAdjustedTime()))
                return error("GetKernelStakeModifier() : reached best block %s at height %d from block %s",
                    pindex->GetBlockHash().ToString(), pindex->nHeight, hashBlockFrom.ToString());
            else
                return false;
        }
        pindex = pindex->pnext;
        if (pindex->GeneratedStakeModifier())
        {
            nStakeModifierHeight = pindex->nHeight;
            nStakeModifierTime = pindex->GetBlockTime();
        }
    }
    nStakeModifier = pindex->nStakeModifier;
    return true;
}

bool GetKernelStakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier)
{
    int nStakeModifierHeight;
    int64_t nStakeModifierTime;

    return GetKernelStakeModifier(hashBlockFrom, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, false);
}

// ppcoin kernel protocol
// coinstake must meet hash target according to the protocol:
// kernel (input 0) must meet the formula
//     hash(nStakeModifier + txPrev.block.nTime + txPrev.offset + txPrev.nTime + txPrev.vout.n + nTime) < bnTarget * nCoinDayWeight
// this ensures that the chance of getting a coinstake is proportional to the
// amount of coin age one owns.
// The reason this hash is chosen is the following:
//   nStakeModifier: scrambles computation to make it very difficult to precompute
//                   future proof-of-stake at the time of the coin's confirmation
//   txPrev.block.nTime: prevent nodes from guessing a good timestamp to
//                       generate transaction for future advantage
//   txPrev.offset: offset of txPrev inside block, to reduce the chance of 
//                  nodes generating coinstake at the same time
//   txPrev.nTime: reduce the chance of nodes generating coinstake at the same
//                 time

//   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
//                  generating coinstake at the same time
//   block/tx hash should not be used here as they can be generated in vast
//   quantities so as to generate blocks faster, degrading the system back into
//   a proof-of-work situation.
//
bool CheckStakeKernelHash(unsigned int nBits, const CBlock& blockFrom, unsigned int nTxPrevOffset, const CTransaction& txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, uint256& targetProofOfStake, bool fPrintProofOfStake)
{
    if (nTimeTx < txPrev.nTime)  // Transaction timestamp violation
        return error("CheckStakeKernelHash() : nTime violation");

    unsigned int nTimeBlockFrom = blockFrom.GetBlockTime();
    if (nTimeBlockFrom + GetStakeMinAge() > nTimeTx) // Min age requirement
        return error("CheckStakeKernelHash() : min age violation");

    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    int64_t nValueIn = txPrev.vout[prevout.n].nValue;

    uint256 hashBlockFrom = blockFrom.GetHash();

    CBigNum bnCoinDayWeight = CBigNum(nValueIn) * GetWeight((int64_t)txPrev.nTime, (int64_t)nTimeTx) / COIN / (24 * 60 * 60);
    targetProofOfStake = CBigNum(bnCoinDayWeight * bnTargetPerCoinDay).getuint256();

    // Calculate hash
    CDataStream ss(SER_GETHASH, 0);
    uint64_t nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64_t nStakeModifierTime = 0;

    if (!GetKernelStakeModifier(hashBlockFrom, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake))
        return false;
    ss << nStakeModifier;

    ss << nTimeBlockFrom << nTxPrevOffset << txPrev.nTime << prevout.n << nTimeTx;
    hashProofOfStake = Hash(ss.begin(), ss.end());
    if (fPrintProofOfStake)
    {
        LogPrintf("CheckStakeKernelHash() : using modifier 0x%016x at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
            nStakeModifier, nStakeModifierHeight,
            DateTimeStrFormat(nStakeModifierTime),
            mapBlockIndex[hashBlockFrom]->nHeight,
            DateTimeStrFormat(blockFrom.GetBlockTime()));
        LogPrintf("CheckStakeKernelHash() : check protocol=%s modifier=0x%016x nTimeBlockFrom=%u nTxPrevOffset=%u nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
            "0.3",
            nStakeModifier,
            nTimeBlockFrom, nTxPrevOffset, txPrev.nTime, prevout.n, nTimeTx,
            hashProofOfStake.ToString());
    }

    // Now check if proof-of-stake hash meets target protocol
    if (CBigNum(hashProofOfStake) > bnCoinDayWeight * bnTargetPerCoinDay)
        return false;
    if (fDebug && !fPrintProofOfStake)
    {
        LogPrintf("CheckStakeKernelHash() : using modifier 0x%016x at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
            nStakeModifier, nStakeModifierHeight, 
            DateTimeStrFormat(nStakeModifierTime),
            mapBlockIndex[hashBlockFrom]->nHeight,
            DateTimeStrFormat(blockFrom.GetBlockTime()));
        LogPrintf("CheckStakeKernelHash() : pass protocol=%s modifier=0x%016x nTimeBlockFrom=%u nTxPrevOffset=%u nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
            "0.3",
            nStakeModifier,
            nTimeBlockFrom, nTxPrevOffset, txPrev.nTime, prevout.n, nTimeTx,
            hashProofOfStake.ToString());
    }
    return true;
}

// Scan given coins set for kernel solution
bool ScanForStakeKernelHash(MetaMap &mapMeta, KernelSearchSettings &settings, CoinsSet::value_type &kernelcoin, unsigned int &nTimeTx, unsigned int &nBlockTime, CWallet* pwallet)
{
    uint256 hashProofOfStake = 0;

    // (txid, vout.n) => ((txindex, (tx, vout.n)), (block, modifier))
    for(MetaMap::const_iterator meta_item = mapMeta.begin(); meta_item != mapMeta.end(); meta_item++)
    {
        if (!pwallet->GetCoinsDataActual())
            break;

        CTxIndex txindex = (*meta_item).second.first.first;
        CBlock block = (*meta_item).second.second.first;
        uint64_t nStakeModifier = (*meta_item).second.second.second;

        // Get coin
        CoinsSet::value_type pcoin = meta_item->second.first.second;

        static int nMaxStakeSearchInterval = 60;

        // only count coins meeting min age requirement
        if (GetStakeMinAge() + block.nTime > settings.nTime - nMaxStakeSearchInterval)
            continue;

        // Transaction offset inside block
        unsigned int nTxOffset = txindex.pos.nTxPos - txindex.pos.nBlockPos;

        // Current timestamp scanning interval
        unsigned int nCurrentSearchInterval = min((int64_t)settings.nSearchInterval, (int64_t)nMaxStakeSearchInterval);

        nBlockTime = block.nTime;
        CBigNum bnTargetPerCoinDay;
        bnTargetPerCoinDay.SetCompact(settings.nBits);
        int64_t nValueIn = pcoin.first->vout[pcoin.second].nValue;

        // Search backward in time from the given timestamp
        // Search nSearchInterval seconds back up to nMaxStakeSearchInterval
        // Stopping search in case of shutting down or cache invalidation
        for (unsigned int n=0; n<nCurrentSearchInterval && pwallet->GetCoinsDataActual() && !fShutdown; n++)
        {
            nTimeTx = settings.nTime - n;
            CBigNum bnCoinDayWeight = CBigNum(nValueIn) * GetWeight((int64_t)pcoin.first->nTime, (int64_t)nTimeTx) / COIN / (24 * 60 * 60);
            CBigNum bnTargetProofOfStake = bnCoinDayWeight * bnTargetPerCoinDay;

            // Build kernel
            CDataStream ss(SER_GETHASH, 0);
            ss << nStakeModifier;
            ss << nBlockTime << nTxOffset << pcoin.first->nTime << pcoin.second << nTimeTx;

            // Calculate kernel hash
            hashProofOfStake = Hash(ss.begin(), ss.end());

            if (bnTargetProofOfStake >= CBigNum(hashProofOfStake))
            {
                LogPrint("coinstake", "nStakeModifier=0x%016x, nBlockTime=%u nTxOffset=%u nTxPrevTime=%u nVout=%u nTimeTx=%u hashProofOfStake=%s Success=true\n",
                    nStakeModifier, nBlockTime, nTxOffset, pcoin.first->nTime, pcoin.second, nTimeTx, hashProofOfStake.GetHex().c_str());

                kernelcoin = pcoin;
                return true;
            }
            //LogPrint("coinstakedeep", "n=%d,nStakeModifier=0x%016x, nBlockTime=%u nTxOffset=%u nTxPrevTime=%u nVout=%u nTimeTx=%u hashProofOfStake=%s wallet=%s Success=false\n",
            //    n, nStakeModifier, nBlockTime, nTxOffset, pcoin.first->nTime, pcoin.second, nTimeTx, hashProofOfStake.GetHex().c_str(),pwallet->strWalletFile.c_str());
        }
    }

    return false;
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(const CTransaction& tx, unsigned int nBits, uint256& hashProofOfStake, uint256& targetProofOfStake)
{
    if (!tx.IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx.GetHash().ToString());

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn& txin = tx.vin[0];

    // First try finding the previous transaction in database
    CTxDB txdb("r");
    CTransaction txPrev;
    CTxIndex txindex;
    if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
        return tx.DoS(1, error("CheckProofOfStake() : INFO: read txPrev failed"));  // previous transaction not in main chain, may occur during initial download
#ifndef USE_LEVELDB
     txdb.Close();
#endif
    // Verify signature
    if (!VerifySignature(txPrev, tx, 0, MANDATORY_SCRIPT_VERIFY_FLAGS, 0))
        return tx.DoS(100, error("CheckProofOfStake() : VerifySignature failed on coinstake %s", tx.GetHash().ToString()));

    // Read block header
    CBlock block;
    if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
        return fDebug? error("CheckProofOfStake() : read block failed") : false; // unable to read block of previous transaction

    if (!CheckStakeKernelHash(nBits, block, txindex.pos.nTxPos - txindex.pos.nBlockPos, txPrev, txin.prevout, tx.nTime, hashProofOfStake, targetProofOfStake, fDebug))
        return tx.DoS(1, error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s", tx.GetHash().ToString(), hashProofOfStake.ToString())); // may occur during initial download or if behind on block chain sync

    return true;
}

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx)
{
    // v0.3 protocol
    return (nTimeBlock == nTimeTx);
}

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex, bool fLoad)
{
    assert (pindex->pprev || pindex->GetBlockHash() == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet));
    // Hash previous checksum with flags, hashProofOfStake and nStakeModifier
    CDataStream ss(SER_GETHASH, 0);
    if (pindex->pprev)
        ss << pindex->pprev->nStakeModifierChecksum;
    ss << pindex->nFlags << pindex->hashProofOfStake << pindex->nStakeModifier;
    uint256 hashChecksum = Hash(ss.begin(), ss.end());
    hashChecksum >>= (256 - 32);
    if(!fLoad)
        LogPrint("stakechecksum", "GetStakeModifierChecksum: StakeCheckSum=0x%09x, at height=%d\n",
            hashChecksum.Get64(), pindex->nHeight );
    return hashChecksum.Get64();
}

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum)
{
    if (fTestNet) return true; // Testnet has no checkpoints
    if (mapStakeModifierCheckpoints.count(nHeight))
        return nStakeModifierChecksum == mapStakeModifierCheckpoints[nHeight];
    return true;
}
