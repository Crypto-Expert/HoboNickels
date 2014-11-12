// Copyright (c) 2009-2012 Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bitcoinrpc.h"
#include "ui_interface.h"
#include "base58.h"
#include "init.h"

using namespace json_spirit;
using namespace std;

void EnsureWalletIsUnlocked(CWallet* pWallet);

class CTxDump
{
public:
    CBlockIndex *pindex;
    int64_t nValue;
    bool fSpent;
    CWalletTx* ptx;
    int nOut;
    CTxDump(CWalletTx* ptx = NULL, int nOut = -1)
    {
        pindex = NULL;
        nValue = 0;
        fSpent = false;
        this->ptx = ptx;
        this->nOut = nOut;
    }
};

Value importprivkey(CWallet* pWallet, const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "importprivkey <HoboNickelsprivkey> [label]\n"
            "Adds a private key (as returned by dumpprivkey) to your wallet.");

    string strSecret = params[0].get_str();
    string strLabel = "";
    if (params.size() > 1)
        strLabel = params[1].get_str();
    CBitcoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strSecret);

    if (!fGood) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    if (pWallet->fWalletUnlockMintOnly)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for minting only.");

    CKey key;
    bool fCompressed;
    CSecret secret = vchSecret.GetSecret(fCompressed);
    key.SetSecret(secret, fCompressed);
    CKeyID vchAddress = key.GetPubKey().GetID();
    {
        LOCK2(cs_main, pWallet->cs_wallet);

        pWallet->MarkDirty();
        pWallet->SetAddressBookName(vchAddress, strLabel);

        // Don't throw error in case a key is already there
        if (pWallet->HaveKey(vchAddress))
            return Value::null;

        pWallet ->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pWallet->AddKey(key))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pWallet->nTimeFirstKey = 1; // 0 would be considered 'no value'

        pWallet->ScanForWalletTransactions(pindexGenesisBlock, true);
        pWallet->ReacceptWalletTransactions();
    }

    return Value::null;
}

Value importwallet(CWallet* pWallet, const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "importwallet <filename>\n"
            "Imports keys from a wallet dump file (see dumpwallet)."
            + HelpRequiringPassphrase(pWallet));

    EnsureWalletIsUnlocked(pWallet);

    if (pWallet->fWalletUnlockMintOnly) // no importwallet in mint-only mode
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for minting only.");

    if(!ImportWallet(pWallet,params[0].get_str().c_str()))
       throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return Value::null;
}

Value dumpprivkey(CWallet* pWallet, const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "dumpprivkey <HoboNickelsaddress>\n"
            "Reveals the private key corresponding to <HoboNickelsaddress>."
            + HelpRequiringPassphrase(pWallet));

    EnsureWalletIsUnlocked(pWallet);


    string strAddress = params[0].get_str();
    CBitcoinAddress address;
    if (!address.SetString(strAddress))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid HoboNickels address");
    if (pWallet->fWalletUnlockMintOnly)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for minting only.");
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CSecret vchSecret;
    bool fCompressed;
    if (!pWallet->GetSecret(keyID, vchSecret, fCompressed))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    return CBitcoinSecret(vchSecret, fCompressed).ToString();
}

Value dumpwallet(CWallet* pWallet, const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "dumpwallet <filename>\n"
            "Dumps all wallet keys in a human-readable format."
            + HelpRequiringPassphrase(pWallet));

    EnsureWalletIsUnlocked(pWallet);

    if (pWallet->fWalletUnlockMintOnly) // no dumpwallet in mint-only mode
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for minting only.");

    if(!DumpWallet(pWallet, params[0].get_str().c_str() ))
      throw JSONRPCError(RPC_WALLET_ERROR, "Error dumping wallet keys to file");

    return Value::null;
}
