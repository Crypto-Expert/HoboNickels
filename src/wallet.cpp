// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"
#include "wallet.h"
#include "walletdb.h"
#include "crypter.h"
#include "util.h"
#include "ui_interface.h"
#include "base58.h"
#include "kernel.h"
#include "net.h"
#include "init.h"
#include "coincontrol.h"
#include <boost/algorithm/string/replace.hpp>

using namespace std;
extern int nMinerSleep;


//////////////////////////////////////////////////////////////////////////////
//
// mapWallet
//

struct CompareValueOnly
{
    bool operator()(const pair<int64_t, pair<const CWalletTx*, unsigned int> >& t1,
                    const pair<int64_t, pair<const CWalletTx*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    RandAddSeedPerfmon();
    CKey key;
    key.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = key.GetPubKey();

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKey(key))
        throw std::runtime_error("CWallet::GenerateNewKey() : AddKey failed");
    return key.GetPubKey();
}

bool CWallet::AddKey(const CKey& key)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata

    CPubKey pubkey = key.GetPubKey();

    if (!CCryptoKeyStore::AddKey(key))
        return false;
    if (!fFileBacked)
        return true;
    if (!IsCrypted())
        return CWalletDB(strWalletFile).WriteKey(pubkey, key.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);

    return true;
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey, const vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
        else
            return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::AddWatchOnly(const CScript &dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
    * that never can be redeemed. However, old wallets may still contain
    * these. Do not add them to the wallet and warn. */
   if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
   {
       std::string strAddr = CBitcoinAddress(redeemScript.GetID()).ToString();
       LogPrintf("%s: Warning: This wallet contains a redeemScript of size %u which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
           __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
       return true;
   }

   return CCryptoKeyStore::AddCScript(redeemScript);
}


bool CWallet::Unlock(const SecureString& strWalletPassphrase)
{
    if (!IsLocked())
        return false;

    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(const MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey))
                return true;
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial vMasterKey;
        BOOST_FOREACH(MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    CWalletDB walletdb(strWalletFile);
    walletdb.WriteBestBlock(loc);
}


bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked)
    {
        CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);

        if (nWalletVersion > 40000)
            pwalletdb->WriteMinVersion(nWalletVersion);
        if (!pwalletdbIn)
            delete pwalletdb;
    }

    return true;
}
void CWallet::SetCoinsDataActual(bool fCoinsDataActualSet )
{
    if (IsLocked())
       return;
    {
       LOCK(cs_wallet); // fCoinsDataActual
       fCoinsDataActual = fCoinsDataActualSet;
       return;
    }
}


bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial vMasterKey;
    RandAddSeedPerfmon();

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    RAND_bytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;

    RandAddSeedPerfmon();
    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    RAND_bytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked)
        {
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            if (!pwalletdbEncryption->TxnBegin())
                return false;
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        if (!EncryptKeys(vMasterKey))
        {
            if (fFileBacked)
                pwalletdbEncryption->TxnAbort();
            exit(1); // We now probably have half of our keys encrypted in memory, and half not...die and let the user reload their unencrypted wallet.
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        if (fFileBacked)
        {
            if (!pwalletdbEncryption->TxnCommit())
                exit(1); // We now have keys encrypted in memory, but no on disk...die to avoid confusion and let the user reload their unencrypted wallet.

            delete pwalletdbEncryption;
            pwalletdbEncryption = NULL;
        }

        Lock();
        Unlock(strWalletPassphrase);
        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);

    }
    NotifyStatusChanged(this);

    return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb) {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    } else {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

CWallet::TxItems CWallet::OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount)
{
    AssertLockHeld(cs_wallet); // mapWallet
    CWalletDB walletdb(strWalletFile);

    // First: get all CWalletTx and CAccountingEntry into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        CWalletTx* wtx = &((*it).second);
        txOrdered.insert(make_pair(wtx->nOrderPos, TxPair(wtx, (CAccountingEntry*)0)));
    }
    acentries.clear();
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    BOOST_FOREACH(CAccountingEntry& entry, acentries)
    {
        txOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));
    }

    return txOrdered;
}

void CWallet::WalletUpdateSpent(const CTransaction &tx, bool fBlock)
{
    // Anytime a signature is successfully verified, it's proof the outpoint is spent.
    // Update the wallet spent flag if it doesn't know due to wallet.dat being
    // restored from backup or the user making copies of wallet.dat.
    {
        LOCK(cs_wallet);
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
        {
            map<uint256, CWalletTx>::iterator mi = mapWallet.find(txin.prevout.hash);
            if (mi != mapWallet.end())
            {
                CWalletTx& wtx = (*mi).second;
                if (txin.prevout.n >= wtx.vout.size())
                    LogPrintf("WalletUpdateSpent: bad wtx %s\n", wtx.GetHash().ToString());
                else if (!wtx.IsSpent(txin.prevout.n) && IsMine(wtx.vout[txin.prevout.n]))
                {
                    LogPrintf("WalletUpdateSpent found spent coin %shbn %s\n", FormatMoney(wtx.GetCredit()), wtx.GetHash().ToString());
                    wtx.MarkSpent(txin.prevout.n);
                    wtx.WriteToDisk();
                    NotifyTransactionChanged(this, txin.prevout.hash, CT_UPDATED);
                }
            }
        }
    }

    if (fBlock)
    {
       uint256 hash = tx.GetHash();
       map<uint256, CWalletTx>::iterator mi = mapWallet.find(hash);
       CWalletTx& wtx = (*mi).second;

       BOOST_FOREACH(const CTxOut& txout, tx.vout)
       {
           if (IsMine(txout))
           {
              wtx.MarkUnspent(&txout - &tx.vout[0]);
              wtx.WriteToDisk();
           }
       }

    }
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
            item.second.MarkDirty();
    }
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn)
{
    uint256 hash = wtxIn.GetHash();
    {
        LOCK(cs_wallet);
        // Inserts only if not already there, returns tx inserted or tx found
        pair<map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(make_pair(hash, wtxIn));
        CWalletTx& wtx = (*ret.first).second;
        wtx.BindWallet(this);
        bool fInsertedNew = ret.second;
        if (fInsertedNew)
        {
            wtx.nTimeReceived = GetAdjustedTime();
            wtx.nOrderPos = IncOrderPosNext();

            wtx.nTimeSmart = wtx.nTimeReceived;
            if (wtxIn.hashBlock != 0)
            {
                if (mapBlockIndex.count(wtxIn.hashBlock))
                {
                    unsigned int latestNow = wtx.nTimeReceived;
                    unsigned int latestEntry = 0;
                    {
                        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
                        int64_t latestTolerated = latestNow + 300;
                        std::list<CAccountingEntry> acentries;
                        TxItems txOrdered = OrderedTxItems(acentries);
                        for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
                        {
                            CWalletTx *const pwtx = (*it).second.first;
                            if (pwtx == &wtx)
                                continue;
                            CAccountingEntry *const pacentry = (*it).second.second;
                            int64_t nSmartTime;
                            if (pwtx)
                            {
                                nSmartTime = pwtx->nTimeSmart;
                                if (!nSmartTime)
                                    nSmartTime = pwtx->nTimeReceived;
                            }
                            else
                                nSmartTime = pacentry->nTime;
                            if (nSmartTime <= latestTolerated)
                            {
                                latestEntry = nSmartTime;
                                if (nSmartTime > latestNow)
                                    latestNow = nSmartTime;
                                break;
                            }
                        }
                    }

                    unsigned int& blocktime = mapBlockIndex[wtxIn.hashBlock]->nTime;
                    wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
                }
                else
                    LogPrintf("AddToWallet() : found %s in block %s not in index\n",
                           wtxIn.GetHash().ToString().substr(0,10),
                           wtxIn.hashBlock.ToString());
            }
        }

        bool fUpdated = false;
        if (!fInsertedNew)
        {
            // Merge
            if (wtxIn.hashBlock != 0 && wtxIn.hashBlock != wtx.hashBlock)
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex))
            {
                wtx.vMerkleBranch = wtxIn.vMerkleBranch;
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
            {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
            fUpdated |= wtx.UpdateSpent(wtxIn.vfSpent);
        }

        //// debug print
        LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString().substr(0,10), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

        // Write to disk
        if (fInsertedNew || fUpdated)
            if (!wtx.WriteToDisk())
                return false;
        if (!fHaveGUI) {
            // If default receiving address gets used, replace it with a new one
            if (vchDefaultKey.IsValid()) {
                CScript scriptDefaultKey;
                scriptDefaultKey.SetDestination(vchDefaultKey.GetID());
                BOOST_FOREACH(const CTxOut& txout, wtx.vout)
                {
                    if (txout.scriptPubKey == scriptDefaultKey)
                    {
                        CPubKey newDefaultKey;
                        if (GetKeyFromPool(newDefaultKey, false))
                        {
                            SetDefaultKey(newDefaultKey);
                            SetAddressBookName(vchDefaultKey.GetID(), "");
                        }
                    }
                }
            }
        }
        // since AddToWallet is called directly for self-originating transactions, check for consumption of own coins
        WalletUpdateSpent(wtx, (wtxIn.hashBlock != 0));

        // Notify UI of new or updated transaction
        NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

        // notify an external script when a wallet transaction comes in or is updated
        std::string strCmd = GetArg("-walletnotify", "");
        if ( !strCmd.empty())
        {
            boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }

        // notify an external script when a wallet transaction is received
        std::string strCmdNewNotify = GetArg("-newtxnotify", "");
        if ( !strCmdNewNotify.empty() && fInsertedNew)
        {
             boost::replace_all(strCmdNewNotify, "%s", wtxIn.GetHash().GetHex());
             boost::thread t(runCommand, strCmdNewNotify); // thread runs free
        }

        // notify an external script when a wallet transaction is confirmed
        std::string strCmdConfNotify = GetArg("-confirmnotify", "");
        if ( !strCmdConfNotify.empty() && fUpdated)
        {
            boost::replace_all(strCmdConfNotify, "%s", wtxIn.GetHash().GetHex());
            boost::thread t(runCommand, strCmdConfNotify); // thread runs free
        }
    }
    return true;
}

// Add a transaction to the wallet, or update it.
// pblock is optional, but should be provided if the transaction is known to be in a block.
// If fUpdate is true, existing transactions will be updated.
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate, bool fFindBlock)
{
    uint256 hash = tx.GetHash();
    {
        LOCK(cs_wallet);
        bool fExisted = mapWallet.count(hash);
        if (fExisted && !fUpdate) return false;
        if (fExisted || IsMine(tx) || IsFromMe(tx))
        {
            CWalletTx wtx(this,tx);
            // Get merkle branch if transaction was found in a block
            if (pblock)
                wtx.SetMerkleBranch(pblock);
            return AddToWallet(wtx);
        }
        else
            WalletUpdateSpent(tx);
    }
    return false;
}

bool CWallet::EraseFromWallet(uint256 hash)
{
    if (!fFileBacked)
        return false;
    {
        LOCK(cs_wallet);
        if (mapWallet.erase(hash))
            CWalletDB(strWalletFile).EraseTx(hash);
    }
    return true;
}


isminetype CWallet::IsMine(const CTxIn &txin) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                return IsMine(prev.vout[txin.prevout.n]);
        }
    }
    return MINE_NO;
}

int64_t CWallet::GetDebit(const CTxIn &txin, const isminefilter& filter) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                if (IsMine(prev.vout[txin.prevout.n]) & filter)
                    return prev.vout[txin.prevout.n].nValue;
        }
    }
    return 0;
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but isn't in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, txout.scriptPubKey))
    {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
           return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int CWalletTx::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1;
    {
        LOCK(pwallet->cs_wallet);
        if (IsCoinBase() || IsCoinStake())
        {
            // Generated block
            if (hashBlock != 0)
            {
                map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                if (mi != pwallet->mapRequestCount.end())
                    nRequests = (*mi).second;
            }
        }
        else
        {
            // Did anyone request this transaction?
            map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(GetHash());
            if (mi != pwallet->mapRequestCount.end())
            {
                nRequests = (*mi).second;

                // How about the block it's in?
                if (nRequests == 0 && hashBlock != 0)
                {
                    map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                    if (mi != pwallet->mapRequestCount.end())
                        nRequests = (*mi).second;
                    else
                        nRequests = 1; // If it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

void CWalletTx::GetAmounts(list<pair<CTxDestination, int64_t> >& listReceived,
                           list<pair<CTxDestination, int64_t> >& listSent, int64_t& nFee, string& strSentAccount, const isminefilter& filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Compute fee:
    int64_t nDebit = GetDebit(filter);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        int64_t nValueOut = GetValueOut();
        nFee = nDebit - nValueOut;
    }

    // Sent/received.
    BOOST_FOREACH(const CTxOut& txout, vout)
    {
         // Skip special stake out
         if (txout.scriptPubKey.empty())
             continue;

         isminetype fIsMine = pwallet->IsMine(txout);
         // Only need to handle txouts if AT LEAST one of these is true:
         //   1) they debit from us (sent)
         //   2) the output is to us (received)
         if (nDebit > 0)
         {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
               continue;
         }
         else if (!(fIsMine & filter))
            continue;
         // In either case, we need to get the destination address


        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
        {
            LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                   this->GetHash().ToString());
            address = CNoDestination();
        }

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(make_pair(address, txout.nValue));

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
           listReceived.push_back(make_pair(address, txout.nValue));
    }

}

void CWalletTx::GetAccountAmounts(const string& strAccount, int64_t& nReceived,
                                  int64_t& nSent, int64_t& nFee, const isminefilter& filter) const
{
    nReceived = nSent = nFee = 0;

    int64_t allFee;
    string strSentAccount;
    list<pair<CTxDestination, int64_t> > listReceived;
    list<pair<CTxDestination, int64_t> > listSent;
    GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

    if (strAccount == strSentAccount)
    {
        BOOST_FOREACH(const PAIRTYPE(CTxDestination,int64_t)& s, listSent)
            nSent += s.second;
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        BOOST_FOREACH(const PAIRTYPE(CTxDestination,int64_t)& r, listReceived)
        {
            if (pwallet->mapAddressBook.count(r.first))
            {
                map<CTxDestination, string>::const_iterator mi = pwallet->mapAddressBook.find(r.first);
                if (mi != pwallet->mapAddressBook.end() && (*mi).second == strAccount)
                    nReceived += r.second;
            }
            else if (strAccount.empty())
            {
                nReceived += r.second;
            }
        }
    }
}

void CWalletTx::AddSupportingTransactions(CTxDB& txdb)
{
    vtxPrev.clear();

    const int COPY_DEPTH = 3;
    if (SetMerkleBranch() < COPY_DEPTH)
    {
        vector<uint256> vWorkQueue;
        BOOST_FOREACH(const CTxIn& txin, vin)
            vWorkQueue.push_back(txin.prevout.hash);

        // This critsect is OK because txdb is already open
        {
            LOCK(pwallet->cs_wallet);
            map<uint256, const CMerkleTx*> mapWalletPrev;
            set<uint256> setAlreadyDone;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                uint256 hash = vWorkQueue[i];
                if (setAlreadyDone.count(hash))
                    continue;
                setAlreadyDone.insert(hash);

                CMerkleTx tx;
                map<uint256, CWalletTx>::const_iterator mi = pwallet->mapWallet.find(hash);
                if (mi != pwallet->mapWallet.end())
                {
                    tx = (*mi).second;
                    BOOST_FOREACH(const CMerkleTx& txWalletPrev, (*mi).second.vtxPrev)
                        mapWalletPrev[txWalletPrev.GetHash()] = &txWalletPrev;
                }
                else if (mapWalletPrev.count(hash))
                {
                    tx = *mapWalletPrev[hash];
                }
                else if (txdb.ReadDiskTx(hash, tx))
                {
                    ;
                }
                else
                {
                    LogPrintf("ERROR: AddSupportingTransactions() : unsupported transaction\n");
                    continue;
                }

                int nDepth = tx.SetMerkleBranch();
                vtxPrev.push_back(tx);

                if (nDepth < COPY_DEPTH)
                {
                    BOOST_FOREACH(const CTxIn& txin, tx.vin)
                        vWorkQueue.push_back(txin.prevout.hash);
                }
            }
        }
    }

    reverse(vtxPrev.begin(), vtxPrev.end());
}

bool CWalletTx::WriteToDisk()
{
    return CWalletDB(pwallet->strWalletFile).WriteTx(GetHash(), *this);
}

// Scan the block chain (starting in pindexStart) for transactions
// from or to us. If fUpdate is true, found transactions that already
// exist in the wallet will be updated.
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate)
{
    int ret = 0;

    CBlockIndex* pindex = pindexStart;
    {
        LOCK2(cs_main, cs_wallet);
        while (pindex)
        {
            // no need to read and scan block, if block was created before
            // our wallet birthday (as adjusted for block time variability)
            if (nTimeFirstKey && (pindex->nTime < (nTimeFirstKey - 7200))) {
                pindex = pindex->pnext;
                continue;
            }

            CBlock block;
            block.ReadFromDisk(pindex, true);
            BOOST_FOREACH(CTransaction& tx, block.vtx)
            {
                if (AddToWalletIfInvolvingMe(tx, &block, fUpdate))
                    ret++;
            }
            pindex = pindex->pnext;
        }
    }
    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    CTxDB txdb("r");
    bool fRepeat = true;
    while (fRepeat)
    {
        LOCK2(cs_main, cs_wallet);
        fRepeat = false;
        vector<CDiskTxPos> vMissingTx;
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
        {
            CWalletTx& wtx = item.second;
            if ((wtx.IsCoinBase() && wtx.IsSpent(0)) || (wtx.IsCoinStake() && wtx.IsSpent(1)))
                continue;

            CTxIndex txindex;
            bool fUpdated = false;
            if (txdb.ReadTxIndex(wtx.GetHash(), txindex))
            {
                // Update fSpent if a tx got spent somewhere else by a copy of wallet.dat
                if (txindex.vSpent.size() != wtx.vout.size())
                {
                    LogPrintf("ERROR: ReacceptWalletTransactions() : txindex.vSpent.size() %u != wtx.vout.size() %u\n", txindex.vSpent.size(), wtx.vout.size());
                    continue;
                }
                for (unsigned int i = 0; i < txindex.vSpent.size(); i++)
                {
                    if (wtx.IsSpent(i))
                        continue;
                    if (!txindex.vSpent[i].IsNull() && IsMine(wtx.vout[i]))
                    {
                        wtx.MarkSpent(i);
                        fUpdated = true;
                        vMissingTx.push_back(txindex.vSpent[i]);
                    }
                }
                if (fUpdated)
                {
                    LogPrintf("ReacceptWalletTransactions found spent coin %shbn %s\n", FormatMoney(wtx.GetCredit()), wtx.GetHash().ToString());
                    wtx.MarkDirty();
                    wtx.WriteToDisk();
                }
            }
            else
            {
                // Re-accept any txes of ours that aren't already in a block
                if (!(wtx.IsCoinBase() || wtx.IsCoinStake()))
                    wtx.AcceptWalletTransaction(txdb);
            }
        }
        if (!vMissingTx.empty())
        {
            // TODO: optimize this to scan just part of the block chain?
            if (ScanForWalletTransactions(pindexGenesisBlock))
                fRepeat = true;  // Found missing transactions: re-do re-accept.
        }
    }
}

void CWalletTx::RelayWalletTransaction(CTxDB& txdb)
{
    BOOST_FOREACH(const CMerkleTx& tx, vtxPrev)
    {
        if (!(tx.IsCoinBase() || tx.IsCoinStake()))
        {
            uint256 hash = tx.GetHash();
            if (!txdb.ContainsTx(hash))
                RelayTransaction((CTransaction)tx, hash);
        }
    }
    if (!(IsCoinBase() || IsCoinStake()))
    {
        uint256 hash = GetHash();
        if (!txdb.ContainsTx(hash))
        {
            LogPrintf("Relaying wtx %s\n", hash.ToString().substr(0,10));
            RelayTransaction((CTransaction)*this, hash);
        }
    }
}

void CWalletTx::RelayWalletTransaction()
{
   CTxDB txdb("r");
   RelayWalletTransaction(txdb);
}

void CWallet::ResendWalletTransactions(bool fForce)
{

    if (!fForce)
    {
        // Do this infrequently and randomly to avoid giving away
        // that these are our transactions.
        static int64_t nNextTime;
        if (GetTime() < nNextTime)
            return;
        bool fFirst = (nNextTime == 0);
        nNextTime = GetTime() + GetRand(30 * 60);
        if (fFirst)
            return;

        // Only do it if there's been a new block since last time
        static int64_t nLastTime;
        if (nTimeBestReceived < nLastTime)
            return;
        nLastTime = GetTime();
     }

    // Rebroadcast any of our txes that aren't in a block yet
    LogPrintf("ResendWalletTransactions()\n");
    CTxDB txdb("r");
    {
        LOCK(cs_wallet);
        // Sort them in chronological order
        multimap<unsigned int, CWalletTx*> mapSorted;
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
        {
            CWalletTx& wtx = item.second;
            // Don't rebroadcast until it's had plenty of time that
            // it should have gotten in already by now.
            if (fForce || nTimeBestReceived - (int64_t)wtx.nTimeReceived > 5 * 60)
                mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
        }
        BOOST_FOREACH(PAIRTYPE(const unsigned int, CWalletTx*)& item, mapSorted)
        {
            CWalletTx& wtx = *item.second;
            if (wtx.CheckTransaction())
                wtx.RelayWalletTransaction(txdb);
            else
                LogPrintf("ResendWalletTransactions() : CheckTransaction failed for transaction %s\n", wtx.GetHash().ToString());
        }
    }
}






//////////////////////////////////////////////////////////////////////////////
//
// Actions
//

int64_t CWallet::GetBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableCredit();
        }
    }

    return nTotal;
}

int64_t CWallet::GetWatchOnlyBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableWatchCredit();
        }
    }

    return nTotal;
}


int64_t CWallet::GetUnconfirmedBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableCredit();
        }
    }
    return nTotal;
}

int64_t CWallet::GetUnconfirmedWatchOnlyBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK(cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (!IsFinalTx(*pcoin) || !pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableWatchCredit();
         }
     }
     return nTotal;
}

int64_t CWallet::GetImmatureBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureCredit();
        }
    }
    return nTotal;
}

int64_t CWallet::GetImmatureWatchOnlyBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK(cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureWatchOnlyCredit();
        }
    }
    return nTotal;
}

// populate vCoins with vector of spendable COutputs
void CWallet::AvailableCoins(vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl *coinControl) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            if (!IsFinalTx(*pcoin))
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            if(pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 0)
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
               isminetype mine = IsMine(pcoin->vout[i]);
               if (!(pcoin->IsSpent(i)) && mine != MINE_NO &&
                   pcoin->vout[i].nValue >= nMinimumInputValue &&
                   (!coinControl || !coinControl->HasSelected() || coinControl->IsSelected((*it).first, i)))
               {
                   vCoins.push_back(COutput(pcoin, i, nDepth, mine == MINE_SPENDABLE));
               }
            }
        }
    }
}

void CWallet::AvailableCoinsForStaking(vector<COutput>& vCoins, unsigned int nSpendTime) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            // Filtering by tx timestamp instead of block timestamp may give false positives but never false negatives
            if (pcoin->nTime + SetStakeMinAge() > nSpendTime)
                continue;

            if (pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 1)
               continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
               isminetype mine = IsMine(pcoin->vout[i]);
               if (!(pcoin->IsSpent(i)) && mine != MINE_NO && pcoin->vout[i].nValue >= nMinimumInputValue)
                  vCoins.push_back(COutput(pcoin, i, nDepth, mine == MINE_SPENDABLE));
            }
        }
    }
}

void CWallet::AvailableCoinsMinConf(vector<COutput>& vCoins, int nConf, int64_t nMinValue, int64_t nMaxValue) const
{
    vCoins.clear();

    {
        LOCK(cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            if (!IsFinalTx(*pcoin))
                continue;

            if(pcoin->GetDepthInMainChain() < nConf)
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                isminetype mine = IsMine(pcoin->vout[i]);

                // ignore coin if it was already spent or we don't own it
                if (pcoin->IsSpent(i) || mine == MINE_NO)
                   continue;

                // if coin value is between required limits then add new item to vector
                if (pcoin->vout[i].nValue >= nMinValue && pcoin->vout[i].nValue < nMaxValue)
                    vCoins.push_back(COutput(pcoin, i, pcoin->GetDepthInMainChain(), mine == MINE_SPENDABLE));
            }
        }
    }
}

static void ApproximateBestSubset(vector<pair<int64_t, pair<const CWalletTx*,unsigned int> > >vValue, int64_t nTotalLower, int64_t nTargetValue,
                                  vector<char>& vfBest, int64_t& nBest, int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    seed_insecure_rand();

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        int64_t nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                // The solver here uses a randomized algorithm,
                // the randomness serves no real security purpose but is just
                // needed to prevent degenerate behavior and it is important
                // that the rng fast. We do not use a constant random sequence,
                // because there may be some privacy improvement by making
                // the selection random.
                if (nPass == 0 ? insecure_rand()&1 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}

int64_t CWallet::GetStake() const
{
    int64_t nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
            nTotal += CWallet::GetCredit(*pcoin, MINE_ALL);
    }
    return nTotal;
}

int64_t CWallet::GetWatchOnlyStake() const
{
    int64_t nTotal = 0;
    LOCK(cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
            nTotal += CWallet::GetCredit(*pcoin, MINE_WATCH_ONLY);
    }
    return nTotal;
}


int64_t CWallet::GetNewMint() const
{
    int64_t nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
            nTotal += CWallet::GetCredit(*pcoin, MINE_ALL);
    }
    return nTotal;
}


int64_t CWallet::GetWatchOnlyNewMint() const
{
    int64_t nTotal = 0;
    LOCK(cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
            nTotal += CWallet::GetCredit(*pcoin, MINE_WATCH_ONLY);
    }
    return nTotal;
}


bool fGlobalStakeForCharity = false;
int nPrevS4CHeight = 0;

bool CWallet::StakeForCharity()
{
    if ( IsInitialBlockDownload() || IsLocked() )
        return false;

    CWalletTx wtx;
    int64_t nNet = 0;

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() == 0  && pcoin->GetDepthInMainChain() == nCoinbaseMaturity+20)
            {
                // Calculate Amount for Charity
                nNet = ( ( pcoin->GetCredit(false) - pcoin->GetDebit(MINE_SPENDABLE) ) * nStakeForCharityPercent )/100;

                // Accumlate S4C Until we get to the min
                nStakeForCharityAccum+=nNet;

                // Do not send if amount is too low
                if (nStakeForCharityAccum < nStakeForCharityMin ) {
                    LogPrintf("StakeForCharity: Accumulated Ammount %s is less than Min %s. Waiting to accumlate more before sending. \n",
                              FormatMoney(nStakeForCharityAccum),FormatMoney(nStakeForCharityMin));
                    return false;
                }
                // Truncate to max if amount is too great
                if (nStakeForCharityAccum > nStakeForCharityMax ) {
                    LogPrintf("StakeForCharity: Amount %s is greater than max %s. Truncated to max.\n",FormatMoney(nStakeForCharityAccum),FormatMoney(nStakeForCharityMax));
                    nStakeForCharityAccum = nStakeForCharityMax;
                }
                if (nBestHeight <= nPrevS4CHeight ) {
                    LogPrintf("StakeForCharity: Warning! nBestHeight %d less or equal to nPreveS4CHeight %d.\n",
                           nBestHeight,
                           nPrevS4CHeight);
                    return false;
                } else {
                    LogPrintf("StakeForCharity: Sending %s to Address %s\n", FormatMoney(nStakeForCharityAccum), strStakeForCharityAddress.ToString());
                    SendMoneyToDestination(strStakeForCharityAddress.Get(), nStakeForCharityAccum, wtx, false, true);
                    nPrevS4CHeight = nBestHeight;
                    nStakeForCharityAccum = 0;
                }
            }

        }

    }

    return true;
}

bool CWallet::SelectCoinsMinConf(int64_t nTargetValue, unsigned int nSpendTime, int nConfMine, int nConfTheirs, vector<COutput> vCoins, set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, int64_t& nValueRet) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    pair<int64_t, pair<const CWalletTx*,unsigned int> > coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<int64_t>::max();
    coinLowestLarger.second.first = NULL;
    vector<pair<int64_t, pair<const CWalletTx*,unsigned int> > > vValue;
    int64_t nTotalLower = 0;

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

    BOOST_FOREACH(const COutput &output, vCoins)
    {
        if (!output.fSpendable)
           continue;

        const CWalletTx *pcoin = output.tx;

        if (output.nDepth < (pcoin->IsFromMe(MINE_ALL) ? nConfMine : nConfTheirs))
            continue;

        int i = output.i;

        if (pcoin->nTime > nSpendTime)
            continue;  // ppcoin: timestamp must not exceed spend time

        int64_t n = pcoin->vout[i].nValue;

        pair<int64_t,pair<const CWalletTx*,unsigned int> > coin = make_pair(n,make_pair(pcoin, i));

        if (n == nTargetValue)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            return true;
        }
        else if (n < nTargetValue + CENT)
        {
            vValue.push_back(coin);
            nTotalLower += n;
        }
        else if (n < coinLowestLarger.first)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (unsigned int i = 0; i < vValue.size(); ++i)
        {
            setCoinsRet.insert(vValue[i].second);
            nValueRet += vValue[i].first;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (coinLowestLarger.second.first == NULL)
            return false;
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    int64_t nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first &&
        ((nBest != nTargetValue && nBest < nTargetValue + CENT) || coinLowestLarger.first <= nBest))
    {
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    }
    else {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
            {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
            }

        //// debug print
        LogPrint("selectcoins", "SelectCoins() best subset: ");
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
                LogPrint("selectcoins", "%s ", FormatMoney(vValue[i].first));
            LogPrint("selectcoins", "total %s\n", FormatMoney(nBest));
    }

    return true;
}

bool CWallet::SelectCoins(int64_t nTargetValue, unsigned int nSpendTime, set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, int64_t& nValueRet, const CCoinControl* coinControl) const
{
    vector<COutput> vCoins;
    AvailableCoins(vCoins, true, coinControl);

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected())
    {
       BOOST_FOREACH(const COutput& out, vCoins)
       {
          if(!out.fSpendable)
             continue;
          nValueRet += out.tx->vout[out.i].nValue;
          setCoinsRet.insert(make_pair(out.tx, out.i));
       }
       return (nValueRet >= nTargetValue);
    }

    return (SelectCoinsMinConf(nTargetValue, nSpendTime, 1, 6, vCoins, setCoinsRet, nValueRet) ||
            SelectCoinsMinConf(nTargetValue, nSpendTime, 1, 1, vCoins, setCoinsRet, nValueRet) ||
            SelectCoinsMinConf(nTargetValue, nSpendTime, 0, 1, vCoins, setCoinsRet, nValueRet));
}

// Select some coins without random shuffle or best subset approximation
bool CWallet::SelectCoinsForStaking(int64_t nTargetValue, unsigned int nSpendTime, set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, int64_t& nValueRet) const
{
    vector<COutput> vCoins;
    AvailableCoinsForStaking(vCoins, nSpendTime);

    setCoinsRet.clear();
    nValueRet = 0;

    BOOST_FOREACH(COutput output, vCoins)
    {
        if(!output.fSpendable)
           continue;


        const CWalletTx *pcoin = output.tx;
        int i = output.i;

        // Stop if we've chosen enough inputs
        if (nValueRet >= nTargetValue)
            break;

        // Follow the timestamp rules
        if (pcoin->nTime > nSpendTime)
            continue;

        int64_t n = pcoin->vout[i].nValue;

        pair<int64_t,pair<const CWalletTx*,unsigned int> > coin = make_pair(n,make_pair(pcoin, i));

        if (n >= nTargetValue)
        {
            // If input value is greater or equal to target then simply insert
            // it into the current subset and exit
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            break;
        }
        else if (n < nTargetValue + CENT)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
        }
    }

    return true;
}

// Select some coins without random shuffle or best subset approximation
bool CWallet::SelectCoinsSimple(int64_t nTargetValue, int64_t nMinValue, int64_t nMaxValue, unsigned int nSpendTime, int nMinConf, set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, int64_t& nValueRet) const
{
    vector<COutput> vCoins;
    AvailableCoinsMinConf(vCoins, nMinConf, nMinValue, nMaxValue);

    setCoinsRet.clear();
    nValueRet = 0;

    BOOST_FOREACH(COutput output, vCoins)
    {
        if(!output.fSpendable)
            continue;
        const CWalletTx *pcoin = output.tx;
        int i = output.i;

        // Ignore immature coins
        if (pcoin->GetBlocksToMaturity() > 0)
            continue;

        // Stop if we've chosen enough inputs
        if (nValueRet >= nTargetValue)
            break;

        // Follow the timestamp rules
        if (pcoin->nTime > nSpendTime)
            continue;

        int64_t n = pcoin->vout[i].nValue;

        pair<int64_t,pair<const CWalletTx*,unsigned int> > coin = make_pair(n,make_pair(pcoin, i));

        if (n >= nTargetValue)
        {
            // If input value is greater or equal to target then simply insert
            //    it into the current subset and exit
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            break;
        }
        else if (n < nTargetValue + CENT)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
        }
    }

    return true;
}

bool CWallet::CreateTransaction(const vector<pair<CScript, int64_t> >& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey, int64_t& nFeeRet, bool fAllowS4C, const CCoinControl* coinControl)
{
    int64_t nValue = 0;
    BOOST_FOREACH (const PAIRTYPE(CScript, int64_t)& s, vecSend)
    {
        if (nValue < 0)
            return false;
        nValue += s.second;
    }
    if (vecSend.empty() || nValue < 0)
        return false;

    wtxNew.BindWallet(this);

    {
        LOCK2(cs_main, cs_wallet);
        // txdb must be opened before the mapWallet lock
        CTxDB txdb("r");
        {
            nFeeRet = nTransactionFee;
            while (true)
            {
                wtxNew.vin.clear();
                wtxNew.vout.clear();
                wtxNew.fFromMe = true;

                int64_t nTotalValue = nValue + nFeeRet;
                double dPriority = 0;
                // vouts to the payees
                BOOST_FOREACH (const PAIRTYPE(CScript, int64_t)& s, vecSend)
                    wtxNew.vout.push_back(CTxOut(s.second, s.first));

                // Choose coins to use
                set<pair<const CWalletTx*,unsigned int> > setCoins;
                int64_t nValueIn = 0;
                if (!SelectCoins(nTotalValue, wtxNew.nTime, setCoins, nValueIn, coinControl))
                    return false;
                BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
                {
                    int64_t nCredit = pcoin.first->vout[pcoin.second].nValue;
                    dPriority += (double)nCredit * pcoin.first->GetDepthInMainChain();
                }

                int64_t nChange = nValueIn - nValue - nFeeRet;
                // if sub-cent change is required, the fee must be raised to at least MIN_TX_FEE
                // or until nChange becomes zero
                // NOTE: this depends on the exact behaviour of GetMinFee
                if (nFeeRet < MIN_TX_FEE && nChange > 0 && nChange < CENT)
                {
                    int64_t nMoveToFee = min(nChange, MIN_TX_FEE - nFeeRet);
                    nChange -= nMoveToFee;
                    nFeeRet += nMoveToFee;
                }

                // ppcoin: sub-cent change is moved to fee
                if (nChange > 0 && nChange < MIN_TXOUT_AMOUNT)
                {
                    nFeeRet += nChange;
                    nChange = 0;
                }

                if (nChange > 0)
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptChange;

                    // Stake For Charity: send change to custom address
                    if (fAllowS4C && strStakeForCharityChangeAddress.IsValid()) {
                            scriptChange.SetDestination(strStakeForCharityChangeAddress.Get());
                    }
                    // coin control: send change to custom address
                    else if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange)) {
                        scriptChange.SetDestination(coinControl->destChange);
                    }
                    else // no coin control: send change to newly generated address
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        assert(reservekey.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked

                        scriptChange.SetDestination(vchPubKey.GetID());
                    }

                    // Insert change txn at random position:
                    vector<CTxOut>::iterator position = wtxNew.vout.begin()+GetRandInt(wtxNew.vout.size());
                    wtxNew.vout.insert(position, CTxOut(nChange, scriptChange));
                }
                else
                    reservekey.ReturnKey();

                // Fill vin
                BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
                    wtxNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second));

                // Sign
                int nIn = 0;
                BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
                    if (!SignSignature(*this, *coin.first, wtxNew, nIn++))
                        return false;

                // Limit size
                unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION);
                if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
                    return false;
                dPriority /= nBytes;

                // Check that enough fee is included
                int64_t nPayFee = nTransactionFee * (1 + (int64_t)nBytes / 1000);
                int64_t nMinFee = wtxNew.GetMinFee(1, false, GMF_SEND, nBytes);

                if (nFeeRet < max(nPayFee, nMinFee))
                {
                    nFeeRet = max(nPayFee, nMinFee);
                    continue;
                }

                // Fill vtxPrev by copying from previous transactions vtxPrev
                wtxNew.AddSupportingTransactions(txdb);
                wtxNew.fTimeReceivedIsTxTime = true;

                break;
            }
        }
    }
    return true;
}

bool CWallet::GetStakeWeightFromValue(const int64_t& nTime, const int64_t& nValue, uint64_t& nWeight)
{
  // This is a negative value when there is no weight. But set it to zero
  // so the user is not confused. Used in reporting in Coin Control.
  // Descisions based on this function should be used with care.
  int64_t nTimeWeight = GetWeight(nTime, (int64_t)GetTime());

  if (nTimeWeight < 0 )
      nTimeWeight=0;

  CBigNum bnCoinDayWeight = CBigNum(nValue) * nTimeWeight / COIN / (24 * 60 * 60);
  nWeight = bnCoinDayWeight.getuint64();

  return true;
}

bool CWallet::GetStakeWeight(const CKeyStore& keystore, uint64_t& nMinWeight, uint64_t& nMaxWeight, uint64_t& nWeight)
{
    // Choose coins to use
    int64_t nBalance = GetBalance();

    nMinWeight = nMaxWeight = nWeight = 0;

    if (nBalance <= nReserveBalance)
        return false;

    CTxDB txdb("r");
    {
        LOCK2(cs_main, cs_wallet);
        // Cache outputs unless best block or wallet transaction set changed
        if (!fCoinsDataActual && !IsLocked())
        {
            mapMeta.clear();
            int64_t nValueIn = 0;
            CoinsSet setCoins;
            if (!SelectCoinsForStaking(nBalance - nReserveBalance, GetAdjustedTime(),setCoins, nValueIn))
                return false;

            if (setCoins.empty())
                return false;

            {
                CTxIndex txindex;
                CBlock block;
                for(CoinsSet::iterator pcoin = setCoins.begin(); pcoin != setCoins.end(); pcoin++)
                {
                    // Load transaction index item
                    if (!txdb.ReadTxIndex(pcoin->first->GetHash(), txindex))
                        continue;

                    // Read block header
                    if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                        continue;

                    uint64_t nStakeModifier = 0;
                    if (!GetKernelStakeModifier(block.GetHash(), nStakeModifier))
                        continue;

                    // Add meta record
                    // (txid, vout.n) => ((txindex, (tx, vout.n)), (block, modifier))
                    mapMeta[make_pair(pcoin->first->GetHash(), pcoin->second)] = make_pair(make_pair(txindex, *pcoin), make_pair(block, nStakeModifier));
                }
            }
            LogPrint("coinstake", "----GetStakeWeight: %zu meta items loaded for %zu coins for wallet %s-----\n", mapMeta.size(), setCoins.size(),strWalletFile.c_str());
            fCoinsDataActual = true;
        }
    }


    // (txid, vout.n) => ((txindex, (tx, vout.n)), (block, modifier))
    for(MetaMap::const_iterator meta_item = mapMeta.begin(); meta_item != mapMeta.end(); meta_item++)
    {
        // Get coin
        CoinsSet::value_type pcoin = meta_item->second.first.second;

        int64_t nTimeWeight = GetWeight((int64_t)pcoin.first->nTime, (int64_t)GetTime());
        CBigNum bnCoinDayWeight = CBigNum(pcoin.first->vout[pcoin.second].nValue) * nTimeWeight / COIN / (24 * 60 * 60);

        // Weight is greater than zero
        if (nTimeWeight > 0)
            nWeight += bnCoinDayWeight.getuint64();

        // Weight is greater than zero, but the maximum value isn't reached yet
        if (nTimeWeight > 0 && nTimeWeight < nStakeMaxAge)
            nMinWeight += bnCoinDayWeight.getuint64();

        // Maximum weight was reached
        if (nTimeWeight == nStakeMaxAge)
            nMaxWeight += bnCoinDayWeight.getuint64();
    }

    return true;
}

bool CWallet::MergeCoins(const int64_t& nAmount, const int64_t& nMinValue, const int64_t& nOutputValue, std::list<uint256>& listMerged)
{
    int64_t nBalance = GetBalance();

    if (nAmount > nBalance)
        return false;

    listMerged.clear();
    int64_t nValueIn = 0;
    set<pair<const CWalletTx*,unsigned int> > setCoins;

    // Simple coins selection - no randomization
    if (!SelectCoinsSimple(nAmount, nMinValue, nOutputValue, GetTime(), 1, setCoins, nValueIn))
        return false;

    if (setCoins.empty())
        return false;

    CWalletTx wtxNew;
    vector<const CWalletTx*> vwtxPrev;

    // Reserve a new key pair from key pool

    CReserveKey reservekey(this);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey))
       return false;

    // Output script
    CScript scriptOutput;
    scriptOutput.SetDestination(vchPubKey.GetID());

    // Insert output
    wtxNew.vout.push_back(CTxOut(0, scriptOutput));

    double dWeight = 0;

    BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
    {
        int64_t nCredit = pcoin.first->vout[pcoin.second].nValue;

        // Add current coin to inputs list and add its credit to transaction output
        wtxNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
        wtxNew.vout[0].nValue += nCredit;
        vwtxPrev.push_back(pcoin.first);

        // Assuming that average scriptsig size is 110 bytes
        int64_t nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION) + wtxNew.vin.size() * 110;
        dWeight += (double)nCredit * pcoin.first->GetDepthInMainChain();

        double dFinalPriority = dWeight /= nBytes;
        bool fAllowFree = CTransaction::AllowFree(dFinalPriority);

        // Get actual transaction fee according to its estimated size and priority
        int64_t nMinFee = wtxNew.GetMinFee(1, fAllowFree, GMF_SEND, nBytes);

        // Prepare transaction for commit if sum is enough ot its size is too big
        if (nBytes >= MAX_BLOCK_SIZE_GEN/6 || wtxNew.vout[0].nValue >= nOutputValue)
        {
            wtxNew.vout[0].nValue -= nMinFee; // Set actual fee

            for (unsigned int i = 0; i < wtxNew.vin.size(); i++) {
                const CWalletTx *txin = vwtxPrev[i];

                // Sign all scripts
                if (!SignSignature(*this, *txin, wtxNew, i))
                    return false;
            }

            // Try to commit, return false on failure
            if (!CommitTransaction(wtxNew, reservekey))
                return false;

            listMerged.push_back(wtxNew.GetHash()); // Add to hashes list

            dWeight = 0;  // Reset all temporary values
            vwtxPrev.clear();
            wtxNew.SetNull();
            wtxNew.vout.push_back(CTxOut(0, scriptOutput));
        }
    }

    // Create transactions if there are some unhandled coins left
    if (wtxNew.vout[0].nValue > 0) {
        int64_t nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION) + wtxNew.vin.size() * 110;

        double dFinalPriority = dWeight /= nBytes;
        bool fAllowFree = CTransaction::AllowFree(dFinalPriority);

        // Get actual transaction fee according to its size and priority
        int64_t nMinFee = wtxNew.GetMinFee(1, fAllowFree, GMF_SEND, nBytes);

        wtxNew.vout[0].nValue -= nMinFee; // Set actual fee

        if (wtxNew.vout[0].nValue <= 0)
            return false;

        for (unsigned int i = 0; i < wtxNew.vin.size(); i++) {
            const CWalletTx *txin = vwtxPrev[i];

            // Sign all scripts again
            if (!SignSignature(*this, *txin, wtxNew, i))
                return false;
        }

        // Try to commit, return false on failure
        if (!CommitTransaction(wtxNew, reservekey))
            return false;

        listMerged.push_back(wtxNew.GetHash()); // Add to hashes list
    }

    return true;
}

bool CWallet::CreateTransaction(CScript scriptPubKey, int64_t nValue, CWalletTx& wtxNew, CReserveKey& reservekey, int64_t& nFeeRet, bool fAllowS4C, const CCoinControl* coinControl)
{
    vector< pair<CScript, int64_t> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));
    return CreateTransaction(vecSend, wtxNew, reservekey, nFeeRet, fAllowS4C, coinControl);
}

bool CWallet::CreateCoinStake(const CKeyStore& keystore, unsigned int nBits, int64_t nSearchInterval, CTransaction& txNew, CKey& key)
{
    // The following combine threshold is important to security
    // Should not be adjusted if you don't understand the consequences

    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    txNew.vin.clear();
    txNew.vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    int64_t nBalance = GetBalance();

    if (nBalance <= nReserveBalance)
        return false;

    vector<const CWalletTx*> vwtxPrev;

    CTxDB txdb("r");
    {
        LOCK2(cs_main, cs_wallet);
        // Cache outputs unless best block or wallet transaction set changed
        if (!fCoinsDataActual && !IsLocked())
        {
            mapMeta.clear();
            int64_t nValueIn = 0;
            CoinsSet setCoins;
            if (!SelectCoinsForStaking(nBalance - nReserveBalance, GetAdjustedTime(),setCoins, nValueIn))
                return false;

            if (setCoins.empty())
                return false;

            {
                CTxIndex txindex;
                CBlock block;
                for(CoinsSet::iterator pcoin = setCoins.begin(); pcoin != setCoins.end(); pcoin++)
                {
                    // Load transaction index item
                    if (!txdb.ReadTxIndex(pcoin->first->GetHash(), txindex))
                        continue;

                    // Read block header
                    if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                        continue;

                    uint64_t nStakeModifier = 0;
                    if (!GetKernelStakeModifier(block.GetHash(), nStakeModifier))
                        continue;

                    // Add meta record
                    // (txid, vout.n) => ((txindex, (tx, vout.n)), (block, modifier))
                    mapMeta[make_pair(pcoin->first->GetHash(), pcoin->second)] = make_pair(make_pair(txindex, *pcoin), make_pair(block, nStakeModifier));
                }
            }

            LogPrint("coinstake", "----CreateCoinStake: %zu meta items loaded for %zu coins for wallet %s-----\n", mapMeta.size(), setCoins.size(),strWalletFile.c_str());
            fCoinsDataActual = true;
        }
    }

    int64_t nCredit = 0;
    CScript scriptPubKeyKernel;

    KernelSearchSettings settings;
    settings.nBits = nBits;
    settings.nTime = txNew.nTime;
    settings.nOffset = 0;
    settings.nLimit = mapMeta.size();
    settings.nSearchInterval = nSearchInterval;

    unsigned int nTimeTx, nBlockTime;
    CoinsSet::value_type kernelcoin;

    if (ScanForStakeKernelHash(mapMeta, settings, kernelcoin, nTimeTx, nBlockTime, this))
    {
        // Found a kernel
        LogPrint("coinstake","CreateCoinStake : kernel found\n");
        vector<valtype> vSolutions;
        txnouttype whichType;
        CScript scriptPubKeyOut;
        scriptPubKeyKernel = kernelcoin.first->vout[kernelcoin.second].scriptPubKey;
        if (!Solver(scriptPubKeyKernel, whichType, vSolutions))
        {
            LogPrint("coinstake", "CreateCoinStake : failed to parse kernel\n");
            return false;
        }
        LogPrint("coinstake","CreateCoinStake : parsed kernel type=%d\n", whichType);
        if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH)
        {
            LogPrint("coinstake", "CreateCoinStake : no support for kernel type=%d\n", whichType);
            return false;  // only support pay to public key and pay to address
        }
        if (whichType == TX_PUBKEYHASH) // pay to address type
        {
            // convert to pay to public key type
            if (!keystore.GetKey(uint160(vSolutions[0]), key))
            {
                LogPrint("coinstake", "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                return false;  // unable to find corresponding public key
            }
            scriptPubKeyOut << key.GetPubKey() << OP_CHECKSIG;
        }
        if (whichType == TX_PUBKEY)
        {
            valtype& vchPubKey = vSolutions[0];
            if (!keystore.GetKey(Hash160(vchPubKey), key))
            {
                LogPrint("coinstake", "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                return false;  // unable to find corresponding public key
            }
            if (key.GetPubKey() != vchPubKey)
            {
                LogPrint("coinstake", "CreateCoinStake : invalid key for kernel type=%d\n", whichType);
                return false; // keys mismatch
            }

            scriptPubKeyOut = scriptPubKeyKernel;
        }

        txNew.nTime = nTimeTx;
        txNew.vin.push_back(CTxIn(kernelcoin.first->GetHash(), kernelcoin.second));
        nCredit += kernelcoin.first->vout[kernelcoin.second].nValue;
        vwtxPrev.push_back(kernelcoin.first);
        txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));

        if((nCredit >= nSplitThreshold) && (GetWeight((int64_t)nBlockTime, (int64_t)txNew.nTime) < nStakeMaxAge))
            txNew.vout.push_back(CTxOut(0, scriptPubKeyOut)); //split stake
        LogPrint("coinstake", "CreateCoinStake : added kernel type=%d\n", whichType);
    }

    if (nCredit == 0 || nCredit > nBalance - nReserveBalance)
        return false;

    // (txid, vout.n) => ((txindex, (tx, vout.n)), (block, modifier))
    for(MetaMap::const_iterator meta_item = mapMeta.begin(); meta_item != mapMeta.end(); meta_item++)
    {
        // Get coin
        CoinsSet::value_type pcoin = meta_item->second.first.second;

        // Attempt to add more inputs
        // Only add coins of the same key/address as kernel
        if (txNew.vout.size() == 2 && ((pcoin.first->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel || pcoin.first->vout[pcoin.second].scriptPubKey == txNew.vout[1].scriptPubKey))
            && pcoin.first->GetHash() != txNew.vin[0].prevout.hash)
        {
            int64_t nTimeWeight = GetWeight((int64_t)pcoin.first->nTime, (int64_t)txNew.nTime);

            // Stop adding more inputs if already too many inputs
            if (txNew.vin.size() >= 100)
                break;
            // Stop adding more inputs if value is already pretty significant
            if (nCredit > nCombineThreshold)
                break;
            // Stop adding inputs if reached reserve limit
            if (nCredit + pcoin.first->vout[pcoin.second].nValue > nBalance - nReserveBalance)
                break;
            // Do not add additional significant input
            if (pcoin.first->vout[pcoin.second].nValue > nCombineThreshold)
                continue;
            // Do not add input that is still too young
            if (nTimeWeight < nStakeMaxAge)
                continue;

            txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nCredit += pcoin.first->vout[pcoin.second].nValue;
            vwtxPrev.push_back(pcoin.first);
        }
    }

    // Calculate coin age reward
    {
        uint64_t nCoinAge;
        CTxDB txdb("r");
        if (!txNew.GetCoinAge(txdb, nCoinAge))
            return error("CreateCoinStake : failed to calculate coin age");
        nCredit += GetProofOfStakeReward(nCoinAge, nBits, txNew.nTime);
    }

    int64_t nMinFee = 0;
    while (true)
    {
        // Set output amount
        if (txNew.vout.size() == 3)
        {
            txNew.vout[1].nValue = ((nCredit - nMinFee) / 2 / CENT) * CENT;
            txNew.vout[2].nValue = nCredit - nMinFee - txNew.vout[1].nValue;
        }
        else
            txNew.vout[1].nValue = nCredit - nMinFee;

        // Sign
        int nIn = 0;
        BOOST_FOREACH(const CWalletTx* pcoin, vwtxPrev)
        {
            if (!SignSignature(*this, *pcoin, txNew, nIn++))
                return error("CreateCoinStake : failed to sign coinstake");
        }

        // Limit size
        unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
        if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
            return error("CreateCoinStake : exceeded coinstake size limit");

        // Check enough fee is paid
        if (nMinFee < txNew.GetMinFee() - MIN_TX_FEE)
        {
            nMinFee = txNew.GetMinFee() - MIN_TX_FEE;
            continue; // try signing again
        }
        else
        {
            LogPrint("coinstake", "CreateCoinStake : fee for coinstake %s\n", FormatMoney(nMinFee).c_str());
            break;
        }
    }

    // Successfully generated coinstake
    return true;
}

// Call after CreateTransaction unless you want to abort
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew);

            // Mark old coins as spent
            set<CWalletTx*> setCoins;
            BOOST_FOREACH(const CTxIn& txin, wtxNew.vin)
            {
                CWalletTx &coin = mapWallet[txin.prevout.hash];
                coin.BindWallet(this);
                coin.MarkSpent(txin.prevout.n);
                coin.WriteToDisk();
                NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Track how many getdata requests our transaction gets
        mapRequestCount[wtxNew.GetHash()] = 0;

        // Broadcast
        if (!wtxNew.AcceptToMemoryPool())
        {
            // This must not fail. The transaction has already been signed and recorded.
            LogPrintf("CommitTransaction() : Error: Transaction not valid");
            return false;
        }
        wtxNew.RelayWalletTransaction();
    }
    return true;
}


string CWallet::SendMoney(CScript scriptPubKey, int64_t nValue, CWalletTx& wtxNew, bool fAskFee, bool fAllowS4C)
{
    CReserveKey reservekey(this);
    int64_t nFeeRequired;

    if (IsLocked())
    {
        string strError = _("Error: Wallet locked, unable to create transaction  ");
        LogPrintf("SendMoney() : %s", strError);
        return strError;
    }

    // Stake For Charity is the only allowable option to send coins when the UnlockMintOnly flag is set.
    if ( fWalletUnlockMintOnly && !fAllowS4C )
    {
        string strError = _("Error: Wallet unlocked for staking and mining only, unable to create transaction.");
        LogPrintf("SendMoney() : %s", strError);
        return strError;
    }

    if (!CreateTransaction(scriptPubKey, nValue, wtxNew, reservekey, nFeeRequired, fAllowS4C))
    {
        string strError;
        if (nValue + nFeeRequired > GetBalance())
            strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds  "), FormatMoney(nFeeRequired));
        else
            strError = _("Error: Transaction creation failed  ");
        LogPrintf("SendMoney() : %s", strError);
        return strError;
    }

    if (fAskFee && !uiInterface.ThreadSafeAskFee(nFeeRequired, _("Sending...")))
        return "ABORTED";

    if (!CommitTransaction(wtxNew, reservekey))
        return _("Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    return "";
}


string CWallet::SendMoneyToDestination(const CTxDestination& address, int64_t nValue, CWalletTx& wtxNew, bool fAskFee, bool fAllowS4C)
{
    // Check amount
    if (nValue <= 0)
        return _("Invalid amount");
    if (nValue + nTransactionFee > GetBalance())
        return _("Insufficient funds");

    // Parse Bitcoin address
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address);

    return SendMoney(scriptPubKey, nValue, wtxNew, fAskFee, fAllowS4C);
}




DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // the requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    NewThread(ThreadFlushWalletDB, &strWalletFile);
    return DB_LOAD_OK;
}

DBErrors CWallet::ZapWalletTx()
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nZapWalletTxRet = CWalletDB(strWalletFile,"cr+").ZapWalletTx(this);
    if (nZapWalletTxRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // the requires a new key.
        }
    }

    if (nZapWalletTxRet != DB_LOAD_OK)
        return nZapWalletTxRet;

    return DB_LOAD_OK;
}

bool CWallet::SetAddressBookName(const CTxDestination& address, const string& strName)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, std::string>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address] = strName;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address),
                             (fUpdated ? CT_UPDATED : CT_NEW) );

    if (!fFileBacked)
        return false;
    return CWalletDB(strWalletFile).WriteName(CBitcoinAddress(address).ToString(), strName);
}

bool CWallet::DelAddressBookName(const CTxDestination& address)
{
    {
        LOCK(cs_wallet); // mapAddressBook

        mapAddressBook.erase(address);
    }
    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address), CT_DELETED);

    if (!fFileBacked)
        return false;
    return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address).ToString());
}


void CWallet::PrintWallet(const CBlock& block)
{
    {
        AssertLockHeld(cs_wallet);
        if (block.IsProofOfWork() && mapWallet.count(block.vtx[0].GetHash()))
        {
            CWalletTx& wtx = mapWallet[block.vtx[0].GetHash()];
            LogPrintf("    mine:  %d  %d  %d", wtx.GetDepthInMainChain(), wtx.GetBlocksToMaturity(), wtx.GetCredit());
        }
        if (block.IsProofOfStake() && mapWallet.count(block.vtx[1].GetHash()))
        {
            CWalletTx& wtx = mapWallet[block.vtx[1].GetHash()];
            LogPrintf("    stake: %d  %d  %d", wtx.GetDepthInMainChain(), wtx.GetBlocksToMaturity(), wtx.GetCredit());
         }

    }
    LogPrintf("\n");
}

bool CWallet::GetTransaction(const uint256 &hashTx, CWalletTx& wtx)
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::iterator mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end())
        {
            wtx = (*mi).second;
            return true;
        }
    }
    return false;
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
    if (fFileBacked)
    {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

bool GetWalletFile(CWallet* pwallet, string &strWalletFileOut)
{
    if (!pwallet->fFileBacked)
        return false;
    strWalletFileOut = pwallet->strWalletFile;
    return true;
}

//
// Mark old keypool keys as used,
// and generate all new keys
//
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        CWalletDB walletdb(strWalletFile);
        BOOST_FOREACH(int64_t nIndex, setKeyPool)
            walletdb.ErasePool(nIndex);
        setKeyPool.clear();

        if (IsLocked())
            return false;

        int64_t nKeys = max(GetArg("-keypool", 100), (int64_t)0);
        for (int i = 0; i < nKeys; i++)
        {
            int64_t nIndex = i+1;
            walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
            setKeyPool.insert(nIndex);
        }
        LogPrint("keypool", "CWallet::NewKeyPool wrote %d new keys\n", nKeys);
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int nSize)
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        CWalletDB walletdb(strWalletFile);

        // Top up key pool
        unsigned int nTargetSize;
        if (nSize > 0)
            nTargetSize = nSize;
        else
            nTargetSize = max(GetArg("-keypool", 100), (int64_t)0);;

        while (setKeyPool.size() < (nTargetSize + 1))
        {
            int64_t nEnd = 1;
            if (!setKeyPool.empty())
                nEnd = *(--setKeyPool.end()) + 1;
            if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
                throw runtime_error("TopUpKeyPool() : writing generated key failed");
            setKeyPool.insert(nEnd);
            LogPrint("keypool", "keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked())
            TopUpKeyPool();

        // Get the oldest key
        if(setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());
        if (!walletdb.ReadPool(nIndex, keypool))
            throw runtime_error("ReserveKeyFromKeyPool() : read failed");
        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool() : unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        LogPrint("keypool", "keypool reserve %d\n", nIndex);
    }
}

int64_t CWallet::AddReserveKey(const CKeyPool& keypool)
{
    {
        LOCK2(cs_main, cs_wallet);
        CWalletDB walletdb(strWalletFile);

        int64_t nIndex = 1 + *(--setKeyPool.end());
        if (!walletdb.WritePool(nIndex, keypool))
            throw runtime_error("AddReserveKey() : writing added key failed");
        setKeyPool.insert(nIndex);
        return nIndex;
    }
    return -1;
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked)
    {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LogPrint("keypool", "keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    LogPrint("keypool", "keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result, bool fAllowReuse)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1)
        {
            if (fAllowReuse && vchDefaultKey.IsValid())
            {
                result = vchDefaultKey;
                return true;
            }
            if (IsLocked()) return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

std::map<CTxDestination, int64_t> CWallet::GetAddressBalances()
{
    map<CTxDestination, int64_t> balances;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet)
        {
            CWalletTx *pcoin = &walletEntry.second;

            if (!IsFinalTx(*pcoin) || !pcoin->IsTrusted())
                continue;

            if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe(MINE_ALL) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->vout[i]))
                    continue;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                int64_t n = pcoin->IsSpent(i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

set< set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    set< set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    BOOST_FOREACH(PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet)
    {
        CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0 && IsMine(pcoin->vin[0]))
        {
            // group all input addresses with each other
            BOOST_FOREACH(CTxIn txin, pcoin->vin)
            {
                CTxDestination address;
                if(!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
            }

            // group change with input addresses
            BOOST_FOREACH(CTxOut txout, pcoin->vout)
                if (IsChange(txout))
                {
                    CWalletTx tx = mapWallet[pcoin->vin[0].prevout.hash];
                    CTxDestination txoutAddr;
                    if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                        continue;
                    grouping.insert(txoutAddr);
                }
            groupings.insert(grouping);
            grouping.clear();
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            if (IsMine(pcoin->vout[i]))
            {
                CTxDestination address;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    set< set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    map< CTxDestination, set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    BOOST_FOREACH(set<CTxDestination> grouping, groupings)
    {
        // make a set of all the groups hit by this new group
        set< set<CTxDestination>* > hits;
        map< CTxDestination, set<CTxDestination>* >::iterator it;
        BOOST_FOREACH(CTxDestination address, grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination>* merged = new set<CTxDestination>(grouping);
        BOOST_FOREACH(set<CTxDestination>* hit, hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        BOOST_FOREACH(CTxDestination element, *merged)
            setmap[element] = merged;
    }

    set< set<CTxDestination> > ret;
    BOOST_FOREACH(set<CTxDestination>* uniqueGrouping, uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

// check 'spent' consistency between wallet and txindex
// fix wallet spent state according to txindex
// remove orphan Coinbase and Coinstake
void CWallet::FixSpentCoins(int& nMismatchFound, int64_t& nBalanceInQuestion, int& nOrphansFound, bool fCheckOnly)
{
    nMismatchFound = 0;
    nBalanceInQuestion = 0;
    nOrphansFound = 0;

    LOCK(cs_wallet);
    vector<CWalletTx*> vCoins;
    vCoins.reserve(mapWallet.size());
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        vCoins.push_back(&(*it).second);

    CTxDB txdb("r");
    BOOST_FOREACH(CWalletTx* pcoin, vCoins)
    {
        uint256 hash = pcoin->GetHash();
        // Find the corresponding transaction index
        CTxIndex txindex;
        if (!txdb.ReadTxIndex(hash, txindex) && !(pcoin->IsCoinBase() || pcoin->IsCoinStake()))
            continue;

        for (unsigned int n=0; n < pcoin->vout.size(); n++)
        {
            bool fUpdated = false;
            if (IsMine(pcoin->vout[n]) && pcoin->IsSpent(n) && (txindex.vSpent.size() <= n || txindex.vSpent[n].IsNull()))
            {
                LogPrintf("FixSpentCoins found lost coin %shbn %s[%d], %s\n",
                    FormatMoney(pcoin->vout[n].nValue), hash.ToString(), n, fCheckOnly? "repair not attempted" : "repairing");
                nMismatchFound++;
                nBalanceInQuestion += pcoin->vout[n].nValue;
                if (!fCheckOnly)
                {
                    fUpdated = true;
                    pcoin->MarkUnspent(n);
                    pcoin->WriteToDisk();
                }
            }
            else if (IsMine(pcoin->vout[n]) && !pcoin->IsSpent(n) && (txindex.vSpent.size() > n && !txindex.vSpent[n].IsNull()))
            {
                LogPrintf("FixSpentCoins found spent coin %shbn %s[%d], %s\n",
                    FormatMoney(pcoin->vout[n].nValue), hash.ToString(), n, fCheckOnly? "repair not attempted" : "repairing");
                nMismatchFound++;
                nBalanceInQuestion += pcoin->vout[n].nValue;
                if (!fCheckOnly)
                {
                    fUpdated = true;
                    pcoin->MarkSpent(n);
                    pcoin->WriteToDisk();
                }
            }
            if (fUpdated)
                NotifyTransactionChanged(this, hash, CT_UPDATED);
        }

        if((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetDepthInMainChain() < 0)
        {
           nOrphansFound++;
           if (!fCheckOnly)
           {
             EraseFromWallet(hash);
             NotifyTransactionChanged(this, hash, CT_DELETED);
           }
           LogPrintf("FixSpentCoins %s orphaned generation tx %s\n", fCheckOnly ? "found" : "removed", hash.ToString());
        }
     }
}

// ppcoin: disable transaction (only for coinstake)
void CWallet::DisableTransaction(const CTransaction &tx)
{
    if (!tx.IsCoinStake() || !IsFromMe(tx))
        return; // only disconnecting coinstake requires marking input unspent

    LOCK(cs_wallet);
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        map<uint256, CWalletTx>::iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size() && IsMine(prev.vout[txin.prevout.n]))
            {
                prev.MarkUnspent(txin.prevout.n);
                prev.WriteToDisk();
            }
        }
    }
}

bool CWallet::TimedLock(int64_t seconds)
{
    if (IsLocked())
    {
        ResetLockTime();
        return false;
    }

    time_t rawtime = time(NULL) + seconds;
    rawtime += seconds;
    nLockTime = rawtime;
    struct tm* timeinfo = gmtime(&rawtime);

    char buffer[80];
    strftime(buffer ,80, "%Y-%m-%d %H:%M:%S (UTC)", timeinfo);
    strLockTime = buffer;

    lockJob.Schedule(boost::posix_time::seconds(seconds));
    return true;
}

void CWalletLockJob::Run()
{
    LogPrintf("---------CWalletLockJob::Run() called------------\n");

    pWallet->Lock();

    if (!fShutdown)
    {
      LogPrintf ("Halting Stake Mining while we lock wallet(s)\n");
      fStopStaking = true;
      MilliSleep(1000);
    }

    pWalletManager->RestartStakeMiner();

    pWallet->ResetLockTime();
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            if (pwallet->vchDefaultKey.IsValid()) {
                LogPrintf("CReserveKey::GetReservedKey(): Warning: Using default key instead of a new key, top up your keypool!");
                vchPubKey = pwallet->vchDefaultKey;
            } else
                return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    BOOST_FOREACH(const int64_t& id, setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw runtime_error("GetAllReserveKeyHashes() : read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes() : unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = FindBlockByHeight(std::max(0, nBestHeight - 144)); // the tip can be reorganised; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    BOOST_FOREACH(const CKeyID &keyid, setKeys) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++) {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = (*it).second;
        std::map<uint256, CBlockIndex*>::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && blit->second->IsInMainChain()) {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            BOOST_FOREACH(const CTxOut &txout, wtx.vout) {
                // iterate over all their outputs
                ::ExtractAffectedKeys(*this, txout.scriptPubKey, vAffected);
                BOOST_FOREACH(const CKeyID &keyid, vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
        mapKeyBirth[it->first] = it->second->nTime - 7200; // block times can be 2h off
}

void CWallet::UpdatedTransaction(const uint256 &hashTx)
{
    {
        LOCK(cs_wallet);
        // Only notify UI if this transaction is in this wallet
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end())
            NotifyTransactionChanged(this, hashTx, CT_UPDATED);
    }
}

void CWallet::LockCoin(COutPoint& output)
{
    setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint& output)
{
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts)
{
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}

// TODO: Remove these functions
bool static InitError(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

// TODO: Remove dependencies for I/O on LogPrintf to debug.log, InitError, and InitWarning
// TODO: Fix error handling.
bool CWalletManager::LoadWallet(const string& strName, ostringstream& strErrors, bool fRescan, bool fUpgrade, bool fZapWallet, int nMaxVersion)
{
    // Check that the wallet name is valid
    if (!CWalletManager::IsValidName(strName))
    {
        strErrors << _("Wallet name may only contain letters, numbers, and underscores.");
        return false;
    }

    ENTER_CRITICAL_SECTION(cs_WalletManager);

    // Check that wallet is not already loaded
    if (wallets.count(strName) > 0)
    {
        LEAVE_CRITICAL_SECTION(cs_WalletManager);
        strErrors << _("A wallet with that name is already loaded.");
        return false;
    }

    // Wallet file name for wallet foo will be wallet-foo.dat
    // The empty string is reserved for the default wallet whose file is wallet.dat
    string strFile = "wallet";
    if (strName.size() > 0)
        strFile += "-" + strName;
    strFile += ".dat";

    LogPrintf("Loading wallet \"%s\" from %s...\n", strName, strFile);
    int64_t nStart = GetTimeMillis();
    bool fFirstRun = true;
    CWallet* pWallet;
    DBErrors nLoadWalletRet;

    if (fZapWallet) {
        uiInterface.InitMessage(_("Zapping all transactions from wallet..."));
        pWallet = new CWallet(strFile);
        DBErrors nZapWalletRet = pWallet->ZapWalletTx();
        if (nZapWalletRet != DB_LOAD_OK) {
            uiInterface.InitMessage(_("Error loading wallet.dat: Wallet corrupted"));
            return false;
        }
            delete pWallet;
            pWallet = NULL;
            fRescan = true;
    }

    try
    {
        pWallet = new CWallet(strFile);
        nLoadWalletRet = pWallet->LoadWallet(fFirstRun);
    }
    catch (const exception& e)
    {
        LEAVE_CRITICAL_SECTION(cs_WalletManager);
        strErrors << _("Critical error loading wallet \"") << strName << "\" " << _("from ") << strFile << ": " << e.what();
        return false;
    }
    catch (...)
    {
        LEAVE_CRITICAL_SECTION(cs_WalletManager);
        strErrors << _("Critical error loading wallet \"") << strName << "\" " << _("from ") << strFile;
        return false;
    }

    if (nLoadWalletRet != DB_LOAD_OK)
    {
        if (nLoadWalletRet == DB_CORRUPT)
        {
            LEAVE_CRITICAL_SECTION(cs_WalletManager);
            strErrors << _("Error loading ") << strFile << _(": Wallet corrupted") << "\n";
            delete pWallet;
            return false;
        }
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
        {
            string msg(_("Warning: error reading "));
            msg += strFile + _("! All keys read correctly, but transaction data"
                               " or address book entries might be missing or incorrect.");
            InitWarning(msg);
        }
        else if (nLoadWalletRet == DB_TOO_NEW)
            strErrors << _("Error loading ") << strFile << _(": Wallet requires newer version of HoboNickels") << "\n";
        else if (nLoadWalletRet == DB_NEED_REWRITE)
        {
            LEAVE_CRITICAL_SECTION(cs_WalletManager);
            strErrors << _("Wallet needed to be rewritten: restart HoboNickels to complete") << "\n";
            LogPrintf("%s", strErrors.str());
            return InitError(strErrors.str());
        }
        else
            strErrors << _("Error loading ") << strFile << "\n";
    }



    if (fFirstRun || fUpgrade)
    {
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
            nMaxVersion = CLIENT_VERSION;
            pWallet->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        }
        else
            LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < pWallet->GetVersion())
            strErrors << _("Cannot downgrade wallet") << "\n";
        pWallet->SetMaxVersion(nMaxVersion);
    }

    if (fFirstRun)
    {
        // Create new keyUser and set as default key
        RandAddSeedPerfmon();

        CPubKey newDefaultKey;
        if (pWallet->GetKeyFromPool(newDefaultKey, false)) {
            pWallet->SetDefaultKey(newDefaultKey);
            if (!pWallet->SetAddressBookName(pWallet->vchDefaultKey.GetID(), ""))
                strErrors << _("Cannot write default address") << "\n";
          }
    }

    LogPrintf("%s", strErrors.str());
    LogPrintf(" wallet %15dms\n", GetTimeMillis() - nStart);

    boost::shared_ptr<CWallet> spWallet(pWallet);
    this->wallets[strName] = spWallet;
    RegisterWallet(pWallet);

    LEAVE_CRITICAL_SECTION(cs_WalletManager);

    CBlockIndex *pindexRescan = pindexBest;
    if (fRescan)
        pindexRescan = pindexGenesisBlock;
    else
    {
        CWalletDB walletdb(strFile);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = locator.GetBlockIndex();
    }
    if (pindexBest && pindexBest != pindexRescan)
    {
        uiInterface.InitMessage(_("Rescanning..."));
        LogPrintf("Rescanning last %i blocks (from block %i)...\n", pindexBest->nHeight - pindexRescan->nHeight, pindexRescan->nHeight);
        nStart = GetTimeMillis();
        pWallet->ScanForWalletTransactions(pindexRescan, true);
        LogPrintf(" rescan %15dms\n", GetTimeMillis() - nStart);
    }

    // Tell GUI a wallet was loaded so it can be added to the stack
    uiInterface.NotifyWalletAdded(strName);

    return true;
}

bool CWalletManager::LoadWalletFromFile(const string& strFile, string& strName, ostringstream& strErrors, bool fRescan, bool fUpgrade, bool fZapWallet, int nMaxVersion)
{
    // Get wallet file name minus extension.
    const boost::regex STRIP_DIR_AND_EXTENSION_REGEX(".*/wallet-([a-zA-Z0-9_]+)\\.dat");
    boost::cmatch match;
    if (!boost::regex_match(strFile.c_str(), match, STRIP_DIR_AND_EXTENSION_REGEX))
    {
        strErrors << _("CWalletManager::LoadWalletFromFile() - invalid filename ") << strFile;
        return false;
    }

    strName = string(match[1].first, match[1].second);

    ENTER_CRITICAL_SECTION(cs_WalletManager);

    // Check that wallet is not already loaded
    if (wallets.count(strName) > 0)
    {
        LEAVE_CRITICAL_SECTION(cs_WalletManager);
        strErrors << _("A wallet with that name is already loaded.");
        return false;
    }

    LogPrintf("Loading wallet \"%s\" from %s...\n", strName, strFile);
    int64_t nStart = GetTimeMillis();
    bool fFirstRun = true;
    CWallet* pWallet;
    DBErrors nLoadWalletRet;

    try
    {
        pWallet = new CWallet(strFile);
        nLoadWalletRet = pWallet->LoadWallet(fFirstRun);
    }
    catch (const exception& e)
    {
        LEAVE_CRITICAL_SECTION(cs_WalletManager);
        strErrors << _("Critical error loading wallet \"") << strName << "\" " << _("from ") << strFile << ": " << e.what();
        return false;
    }
    catch (...)
    {
        LEAVE_CRITICAL_SECTION(cs_WalletManager);
        strErrors << _("Critical error loading wallet \"") << strName << "\" " << _("from ") << strFile;
        return false;
    }

    if (nLoadWalletRet != DB_LOAD_OK)
    {
        if (nLoadWalletRet == DB_CORRUPT)
        {
            LEAVE_CRITICAL_SECTION(cs_WalletManager);
            strErrors << _("Error loading ") << strFile << _(": Wallet corrupted") << "\n";
            delete pWallet;
            return false;
        }
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
        {
            string msg(_("Warning: error reading "));
            msg += strFile + _("! All keys read correctly, but transaction data"
                               " or address book entries might be missing or incorrect.");
            InitWarning(msg);
        }
        else if (nLoadWalletRet == DB_TOO_NEW)
            strErrors << _("Error loading ") << strFile << _(": Wallet requires newer version of HoboNickels") << "\n";
        else if (nLoadWalletRet == DB_NEED_REWRITE)
        {
            LEAVE_CRITICAL_SECTION(cs_WalletManager);
            strErrors << _("Wallet needed to be rewritten: restart HoboNickels to complete") << "\n";
            LogPrintf("%s", strErrors.str());
            return InitError(strErrors.str());
        }
        else
            strErrors << _("Error loading ") << strFile << "\n";
    }

    if (fZapWallet) {
        uiInterface.InitMessage(_("Zapping all transactions from wallet..."));
        pWallet = new CWallet(strFile);
        DBErrors nZapWalletRet = pWallet->ZapWalletTx();
        if (nZapWalletRet != DB_LOAD_OK) {
            uiInterface.InitMessage(_("Error loading wallet.dat: Wallet corrupted"));
            return false;
        }
            delete pWallet;
            pWallet = NULL;
            fRescan = true;
    }

    if (fFirstRun || fUpgrade)
    {
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
            nMaxVersion = CLIENT_VERSION;
            pWallet->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        }
        else
            LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < pWallet->GetVersion())
            strErrors << _("Cannot downgrade wallet") << "\n";
        pWallet->SetMaxVersion(nMaxVersion);
    }

    if (fFirstRun)
    {
        // Create new keyUser and set as default key
        RandAddSeedPerfmon();

        CPubKey newDefaultKey;
        if (!pWallet->GetKeyFromPool(newDefaultKey, false))
            strErrors << _("Cannot initialize keypool") << "\n";
        pWallet->SetDefaultKey(newDefaultKey);
        if (!pWallet->SetAddressBookName(pWallet->vchDefaultKey.GetID(), ""))
            strErrors << _("Cannot write default address") << "\n";
    }

    LogPrintf("%s", strErrors.str());
    LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

    boost::shared_ptr<CWallet> spWallet(pWallet);
    this->wallets[strName] = spWallet;
    RegisterWallet(pWallet);

    LEAVE_CRITICAL_SECTION(cs_WalletManager);

    CBlockIndex *pindexRescan = pindexBest;
    if (fRescan)
        pindexRescan = pindexGenesisBlock;
    else
    {
        CWalletDB walletdb(strFile);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = locator.GetBlockIndex();
    }
    if (pindexBest && pindexBest != pindexRescan)
    {
        uiInterface.InitMessage(_("Rescanning..."));
        LogPrintf("Rescanning last %i blocks (from block %i)...\n", pindexBest->nHeight - pindexRescan->nHeight, pindexRescan->nHeight);
        nStart = GetTimeMillis();
        pWallet->ScanForWalletTransactions(pindexRescan, true);
        LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
    }

    if ( !pWallet->IsCrypted() )
    {
       if (!NewThread(ThreadStakeMinter, pWallet))
          LogPrintf("Error: NewThread(ThreadStakeMinter) failed\n");
    }
    else
      LogPrintf("Skipped ThreadStakeMinter for wallet: %s due to encryption\n", pWallet->strWalletFile);

    return true;
}

void CWalletManager::RestartStakeMiner()
{
    {
       AssertLockHeld(cs_WalletManager);
       if (!fShutdown)
       {
         fStopStaking = true;
         MilliSleep(nMinerSleep > 500 ? nMinerSleep * 2 : 1000);
       }
       // Re-Start Stake for the remaining wallets
       if (!fShutdown)
       {
         fStopStaking = false;
         MilliSleep(500);
         vector<string> vstrNames;
         vector<boost::shared_ptr<CWallet> > vpWallets;

         BOOST_FOREACH(const wallet_map::value_type& item, wallets)
         {
            vstrNames.push_back(item.first);
            vpWallets.push_back(item.second);
         }
         for (unsigned int i = 0; i < vstrNames.size(); i++)
         {
             if ( !vpWallets[i].get()->IsCrypted() || !vpWallets[i].get()->IsLocked() )
              {
                 LogPrintf ("Restarting ThreadStakeMinter for: %s\n", vstrNames[i]);
                 if (!NewThread(ThreadStakeMinter, vpWallets[i].get()))
                    LogPrintf("Error: NewThread(ThreadStakeMinter) failed\n");
              }
              else
                LogPrintf("Skipped ThreadStakeMinter for wallet: %s due to encryption\n", vstrNames[i]);
         }
       }
    }
}

void CWalletManager::StakeForCharity()
{
    {
        AssertLockHeld(cs_WalletManager);
        if (fShutdown)
            return;

        bool fStakeForCharityRunning = false;
        vector<string> vstrNames;
        vector<boost::shared_ptr<CWallet> > vpWallets;

        BOOST_FOREACH(const wallet_map::value_type& item, wallets)
        {
           vstrNames.push_back(item.first);
           vpWallets.push_back(item.second);
        }
        for (unsigned int i = 0; i < vstrNames.size(); i++)
        {
            if (vpWallets[i].get()->fStakeForCharity  && !vpWallets[i].get()->IsLocked())
            {
                if ( !vpWallets[i].get()->StakeForCharity() )
                    LogPrintf("ERROR While trying to send portion of stake to charity, for wallet %s\n",vstrNames[i] );
                fStakeForCharityRunning = true;
            }

        }
        // If no wallets are running s4c we want to turn off the global switch
        if (!fStakeForCharityRunning)
            fGlobalStakeForCharity=false;
     }
}

int64_t CWalletManager::GetTotalBalance()
{
    AssertLockHeld(cs_WalletManager);
    int64_t nTotBalance = 0;

    if (!fShutdown) {
       vector<string> vstrNames;
       vector<boost::shared_ptr<CWallet> > vpWallets;

       BOOST_FOREACH(const wallet_map::value_type& item, wallets)
       {
          vstrNames.push_back(item.first);
          vpWallets.push_back(item.second);
       }

       for (unsigned int i = 0; i < vstrNames.size(); i++)
          nTotBalance += vpWallets[i].get()->GetBalance();
    }

    return nTotBalance;
}

bool CWalletManager::UnloadWallet(const std::string& strName)
{

    {
        LOCK(cs_WalletManager);
        if (!wallets.count(strName)) return false;
        if (!fShutdown)
        {
          LogPrintf ("Halting Stake Mining while we unload wallet(s)\n");
          fStopStaking = true;
          MilliSleep(nMinerSleep > 500 ? nMinerSleep * 2 : 1000);
        }
        boost::shared_ptr<CWallet> spWallet(wallets[strName]);
        LogPrintf("Unloading wallet %s\n", strName);
        {
            LOCK(spWallet->cs_wallet);
            UnregisterWallet(spWallet.get());
            wallets.erase(strName);
        }

        CWalletManager::RestartStakeMiner();
     }
  return true;
}

void CWalletManager::UnloadAllWallets()
{
    {
        LOCK(cs_WalletManager);
        vector<string> vstrNames;
        vector<boost::shared_ptr<CWallet> > vpWallets;
        BOOST_FOREACH(const wallet_map::value_type& item, wallets)
        {
            vstrNames.push_back(item.first);
            vpWallets.push_back(item.second);
        }

        for (unsigned int i = 0; i < vstrNames.size(); i++)
        {
            LogPrintf("Unloading wallet %s\n", vstrNames[i]);
            {
                LOCK(vpWallets[i]->cs_wallet);
                UnregisterWallet(vpWallets[i].get());
                wallets.erase(vstrNames[i]);
            }
        }
    }
}

boost::shared_ptr<CWallet> CWalletManager::GetWallet(const string& strName)
{
    {
        AssertLockHeld(cs_WalletManager);
        if (!wallets.count(strName))
            throw CWalletManagerException(CWalletManagerException::WALLET_NOT_LOADED,
                                          "CWalletManager::GetWallet() - Wallet not loaded.");
        return wallets[strName];
    }
}

const boost::regex CWalletManager::WALLET_NAME_REGEX("[a-zA-Z0-9_]*");
const boost::regex CWalletManager::WALLET_FILE_REGEX("wallet-([a-zA-Z0-9_]+)\\.dat");

bool CWalletManager::IsValidName(const string& strName)
{
    return boost::regex_match(strName, CWalletManager::WALLET_NAME_REGEX);
}

vector<string> CWalletManager::GetWalletsAtPath(const boost::filesystem::path& pathWallets)
{
    vector<string> vstrFiles = GetFilesAtPath(pathWallets, file_option_flags::REGULAR_FILES);
    vector<string> vstrNames;
    boost::cmatch match;
    BOOST_FOREACH(const string& strFile, vstrFiles)
    {
        if (boost::regex_match(strFile.c_str(), match, CWalletManager::WALLET_FILE_REGEX))
            vstrNames.push_back(string(match[1].first, match[1].second));
    }
    return vstrNames;
}
