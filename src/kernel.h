// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef PPCOIN_KERNEL_H
#define PPCOIN_KERNEL_H

#include "main.h"
#include "wallet.h"

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;

unsigned int GetModiferInterval();

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);

// The stake modifier used to hash for a stake kernel is chosen as the stake
// modifier about a selection interval later than the coin generating the kernel
bool GetKernelStakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
bool CheckStakeKernelHash(unsigned int nBits, const CBlock& blockFrom, unsigned int nTxPrevOffset, const CTransaction& txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, uint256& targetProofOfStake, bool fPrintProofOfStake=false);

// Coins scanning options
typedef struct KernelSearchSettings {
    unsigned int nBits;           // Packed difficulty
    unsigned int nTime;           // Basic time
    unsigned int nOffset;         // Offset inside CoinsSet (isn't used yet)
    unsigned int nLimit;          // Coins to scan (isn't used yet)
    unsigned int nSearchInterval; // Number of seconds allowed to go into the past
} KernelSearchSettings;

typedef std::set<std::pair<const CWalletTx*,unsigned int> > CoinsSet;

// Preloaded coins metadata
// (txid, vout.n) => ((txindex, (tx, vout.n)), (block, modifier))
typedef std::map<std::pair<uint256, unsigned int>, std::pair<std::pair<CTxIndex, std::pair<const CWalletTx*,unsigned int> >, std::pair<CBlock, uint64_t> > > MetaMap;

// Scan given coins set for kernel solution
bool ScanForStakeKernelHash(MetaMap &mapMeta, KernelSearchSettings &settings, CoinsSet::value_type &kernelcoin, unsigned int &nTimeTx, unsigned int &nBlockTime, CWallet* pwallet);


// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(const CTransaction& tx, unsigned int nBits, uint256& hashProofOfStake, uint256& targetProofOfStake);

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex, bool fLoad=false);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum);
// Get time weight using supplied timestamps
int64_t GetWeight(int64_t nIntervalBeginning, int64_t nIntervalEnd);

#endif // PPCOIN_KERNEL_H
