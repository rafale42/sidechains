// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sidechainclient.h>

#include <bmmcache.h>
#include <chainparams.h>
#include <core_io.h>
#include <miner.h>
#include <sidechain.h>
#include <streams.h>
#include <uint256.h>
#include <univalue.h>
#include <utilmoneystr.h>
#include <utilstrencodings.h>
#include <util.h>

#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>

using boost::asio::ip::tcp;

SidechainClient::SidechainClient()
{

}

bool SidechainClient::BroadcastWTPrime(const std::string& hex)
{
    // JSON for sending the WT^ to mainchain via HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"receivewtprime\", \"params\": ");
    json.append("[");
    json.append(UniValue(SIDECHAIN_TEST).write());
    json.append(",\"");
    json.append(hex);
    json.append("\"] }");

    // TODO Read result
    // the mainchain will return the txid if WT^ has been received
    boost::property_tree::ptree ptree;
    return SendRequestToMainchain(json, ptree);
}

// TODO return bool & state / fail string
std::vector<SidechainDeposit> SidechainClient::UpdateDeposits(const std::string& strAddressBytes, const uint256& hashLastDeposit, uint32_t n)
{
    // List of deposits in sidechain format for DB
    std::vector<SidechainDeposit> incoming;

    // JSON for requesting sidechain deposits via mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"listsidechaindeposits\", \"params\": ");
    json.append("[\"");
    json.append(strAddressBytes);
    if (hashLastDeposit.IsNull()) {
        json.append("\"] }");
    } else {
        json.append("\",");
        json.append("\"");
        json.append(hashLastDeposit.ToString());
        json.append("\",");
        json.append(UniValue(uint64_t(n)).write());
        json.append("] }");
    }

    // Try to request deposits from mainchain
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request new deposits\n");
        return incoming;  // TODO return false
    }

    // Process deposits
    BOOST_FOREACH(boost::property_tree::ptree::value_type &value, ptree.get_child("result")) {
        // Looping through list of deposits
        SidechainDeposit deposit;
        uint32_t n = 0;
        BOOST_FOREACH(boost::property_tree::ptree::value_type &v, value.second.get_child("")) {
            // Looping through this deposit's members
            if (v.first == "nsidechain") {
                // Read sidechain number
                std::string data = v.second.data();
                if (!data.length())
                    continue;
                uint8_t nSidechain = std::stoi(data);
                if (nSidechain != SIDECHAIN_TEST)
                    continue;

                deposit.nSidechain = nSidechain;
            }
            else
            if (v.first == "strdest") {
                // Read destination string
                std::string strDest = v.second.data();
                if (strDest.empty())
                    continue;

                deposit.strDest = strDest;
            }
            else
            if (v.first == "txhex") {
                // Read deposit transaction hex
                std::string data = v.second.data();
                if (!data.length())
                    continue;
                if (!IsHex(data))
                    continue;
                if (!DecodeHexTx(deposit.dtx, data))
                    continue;
            }
            else
            if (v.first == "n") {
                // Read deposit output index
                std::string data = v.second.data();
                if (!data.length())
                    continue;

                n = (unsigned int)std::stoi(data);
            }
            else
            if (v.first == "proofhex") {
                // Read serialized merkleblock txout proof
                std::string data = v.second.data();
                if (!data.length())
                    continue;
                if (!IsHex(data))
                    continue;

                CDataStream ssMB(ParseHex(data), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
                ssMB >> deposit.proof;
            }
        }

        if (!(deposit.dtx.vout.size() > n)) {
            LogPrintf("%s: Error invalid deposit output index!\n", __func__);
            continue;
        }

        // Get the user payout amount from the deposit output
        deposit.amtUserPayout = deposit.dtx.vout[n].nValue;
        deposit.n = n;

        // TODO check the deposit output 'N' scriptPubKey (compare to THIS_SIDECHAIN)

        // Add this deposit to the list
        incoming.push_back(deposit);
    }
    // TODO LogPrintf("Sidechain client received %d deposits\n", incoming.size());

    // The deposits are sent in reverse order. Putting the deposits back in
    // order should make sorting faster.
    std::reverse(incoming.begin(), incoming.end());

    // return valid (in terms of format) deposits in sidechain format
    return incoming;
}

bool SidechainClient::VerifyCriticalHashProof(const std::string& criticalProof, uint256 &txid)
{
    // JSON for verifying critical hash
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"verifytxoutproof\", \"params\": ");
    json.append("[\"");
    json.append(criticalProof);
    json.append("\"] }");

    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree))
        return false;

    BOOST_FOREACH(boost::property_tree::ptree::value_type &value, ptree.get_child("result")) {
        std::string data = value.second.data();
        if (data.size() != 64)
            continue;
        txid = uint256S(data);
    }
    return true;
}

bool SidechainClient::RequestBMMProof(const uint256& hashMainBlock, const uint256& hashBMMBlock, SidechainBMMProof& proof)
{
    // JSON for requesting BMM proof via mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"getbmmproof\", \"params\": ");
    json.append("[\"");
    json.append(hashMainBlock.ToString());
    json.append("\",\"");
    json.append(hashBMMBlock.ToString());
    json.append("\"");
    json.append("] }");

    // Try to request BMM proof from mainchain
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        // Can be enabled for debug -- too noisy
        // LogPrintf("ERROR Sidechain client failed to request BMM proof\n");
        return false;
    }

    // Process result
    BOOST_FOREACH(boost::property_tree::ptree::value_type &value, ptree.get_child("result")) {
        BOOST_FOREACH(boost::property_tree::ptree::value_type &v, value.second.get_child("")) {
            // Looping through members
            if (v.first == "coinbasehex") {
                // Read coinbase hex
                std::string data = v.second.data();
                if (!data.length())
                    continue;

                proof.coinbaseHex = data;
            }
            else
            if (v.first == "proof") {
                // Read TxOut proof
                std::string data = v.second.data();
                if (!data.length())
                    continue;

                proof.txOutProof = data;
            }
        }
    }

    if (proof.HasProof()) {
        LogPrintf("Sidechain client received BMM proof for: %s\n", hashBMMBlock.ToString());
        return true;
    } else {
        LogPrintf("Sidechain client received no BMM proof.\n");
        return false;
    }
}

// TODO rename
uint256 SidechainClient::SendBMMCriticalDataRequest(const uint256& hashCritical, const uint256& hashBlockMain, int nHeight, CAmount amount)
{
    uint256 txid = uint256();
    std::string strPrevHash = hashBlockMain.ToString();

    if (amount == CAmount(0))
        amount = DEFAULT_CRITICAL_DATA_AMOUNT;

    // JSON for sending critical data request to mainchain via mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"createbmmcriticaldatatx\", \"params\": ");
    json.append("[\"");
    // TODO use amount
    json.append(ValueFromAmount(amount).write());
    json.append("\",");
    json.append(UniValue(nHeight).write());
    json.append(",\"");
    json.append(hashCritical.ToString());
    json.append("\",");
    json.append(UniValue(SIDECHAIN_TEST).write());
    json.append(",");
    json.append(UniValue(0).write());
    json.append(",\"");
    json.append(strPrevHash.substr(strPrevHash.size() - 4, strPrevHash.size() - 1));
    json.append("\"");
    json.append("] }");

    // Try to send critical data request to mainchain
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to send critical data request!\n");
        return txid; // TODO
    }

    // Process result
    BOOST_FOREACH(boost::property_tree::ptree::value_type &value, ptree.get_child("result")) {
        BOOST_FOREACH(boost::property_tree::ptree::value_type &v, value.second.get_child("")) {
            // Looping through members
            if (v.first == "txid") {
                // Read txid
                std::string data = v.second.data();
                if (!data.length())
                    continue;

                txid = uint256S(data);
            }
        }
    }
    if (!txid.IsNull())
        LogPrintf("Sidechain client created critical data request. TXID: %s\n", txid.ToString());

    return txid;
}

bool SidechainClient::GetCTIP(std::pair<uint256, uint32_t>& ctip)
{
    // JSON for requesting sidechain CTIP via mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"listsidechainctip\", \"params\": ");
    json.append("[");
    json.append(UniValue(SIDECHAIN_TEST).write());
    json.append("] }");

    // Try to request CTIP from mainchain
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        // TODO LogPrintf("ERROR Sidechain client failed to request CTIP\n");
        return false;
    }

    // Process CTIP
    uint256 txid;
    uint32_t n;
    BOOST_FOREACH(boost::property_tree::ptree::value_type &value, ptree.get_child("result")) {
        if (value.first == "n") {
            // Read n
            std::string data = value.second.data();
            if (!data.length())
                continue;
            n = std::stoi(data);
        }
        else
        if (value.first == "txid") {
            // Read TXID
            std::string data = value.second.data();
            if (!data.length())
                continue;

            txid = uint256S(data);
        }
    }
    // TODO LogPrintf("Sidechain client received CTIP\n");

    ctip = std::make_pair(txid, n);

    return true;
}

bool SidechainClient::RefreshBMM(const CAmount& amount, std::string& strError, uint256& hashCreated, uint256& hashConnected, uint256& hashConnectedBlind, uint256& txid, int& nTxn, CAmount& nFees, bool fCreateNew, const uint256& hashPrevBlock)
{
    //
    // A cache of recent mainchain block hashes and the mainchain tip is created
    // and updated.
    //
    // BMM blocks will be created if we haven't created one yet for the current
    // mainchain tip or if the mainchain tip has been updated since the last
    // time we created a BMM block. These BMM blocks do not have the critical
    // hash proof included yet as that requires a commit in a mainchain
    // coinbase.
    //
    // If a new BMM block is created then a BMM request will be sent via RPC to
    // the local mainchain node. This creates a transaction which pays miners to
    // include the critical hash required for our BMM block to be connected to
    // the sidechain.
    //
    // Then, the recent mainchain blocks including the tip will be scanned for
    // critical hash commitments for BMM blocks that we have created previously.
    //
    // If a critical hash commit is found in a mainchain block for one of the
    // BMM blocks we have created, a critical hash commit proof will be added
    // to our BMM block and then it will be submitted to the sidechain.
    //

    // Get list of the most recent mainchain blocks from the cache
    std::vector<uint256> vHashMainBlock = bmmCache.GetRecentMainBlockHashes();

    if (vHashMainBlock.empty()) {
        strError = "Failed to request new mainchain block hashes!";
        return false;
    }

    // Get our cached BMM blocks
    std::vector<CBlock> vBMMCache = bmmCache.GetBMMBlockCache();
    if (vBMMCache.empty() && fCreateNew) {
        // If we don't have any existing BMM requests cached, create our first
        CBlock block;
        if (CreateBMMBlock(block, strError, nFees, hashPrevBlock)) {
            nTxn = block.vtx.size();
            hashCreated = block.GetBlindHash();
            txid = SendBMMCriticalDataRequest(hashCreated, vHashMainBlock.back(), 0, amount);
            bmmCache.StorePrevBlockBMMCreated(vHashMainBlock.back());
            return true;
        } else {
            return false;
        }
    }

    // TODO this could be more efficient
    // Check new main:blocks for our bmm requests
    for (const uint256& u : vHashMainBlock) {
        // Skip if we've already checked this block
        if (bmmCache.MainBlockChecked(u))
            continue;
        // Record that we are going to check this mainchain block
        bmmCache.AddCheckedMainBlock(u);

        // Check main:block for any of our current BMM requests
        for (const CBlock& b : vBMMCache) {
            const uint256& hashBMMBlock = b.GetBlindHash();
            // Send 'getbmmproof' rpc request to mainchain
            SidechainBMMProof proof;
            proof.hashBMMBlock = hashBMMBlock;
            if (RequestBMMProof(u, hashBMMBlock, proof)) {
                CBlock block = b;

                block.criticalProof = proof.txOutProof;

                if (!DecodeHexTx(block.criticalTx, proof.coinbaseHex))
                    continue;

                // Submit BMM block
                if (SubmitBMMBlock(block)) {
                    hashConnected = block.GetHash();
                    hashConnectedBlind = block.GetBlindHash();
                }
            }
        }
    }

    // Was there a new mainchain block since the last request we made?
    if (!bmmCache.HaveBMMRequestForPrevBlock(vHashMainBlock.back())) {
        // Clear out the bmm cache, the old requests are invalid now as they
        // were created for the old mainchain tip.
        bmmCache.ClearBMMBlocks();

        // Create a new BMM request (old ones have expired)
        CBlock block;
        if (fCreateNew && CreateBMMBlock(block, strError, nFees, hashPrevBlock)) {
            // Create BMM critical data request
            nTxn = block.vtx.size();
            hashCreated = block.GetBlindHash();
            txid = SendBMMCriticalDataRequest(block.GetBlindHash(), vHashMainBlock.back(), 0, amount);
            bmmCache.StorePrevBlockBMMCreated(vHashMainBlock.back());
        } else {
            if (fCreateNew) {
                strError = "Failed to create a new BMM request!";
                return false;
            }
        }
    } else {
        if (fCreateNew)
            strError = "Can't create new BMM request - already created for current mainchain tip!";
    }
    return true;
}

bool SidechainClient::CreateBMMBlock(CBlock& block, std::string& strError, CAmount& nFees, const uint256& hashPrevBlock)
{
    if (!BlockAssembler(Params()).GenerateBMMBlock(block, strError, &nFees,
                std::vector<CMutableTransaction>(), hashPrevBlock)) {
        return false;
    }

    if (!bmmCache.StoreBMMBlock(block)) {
        // Failed to store BMM candidate block
        strError = "Failed to store BMM block!\n";
        return false;
    }

    return true;
}

bool SidechainClient::SubmitBMMBlock(const CBlock& block)
{
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
    return ProcessNewBlock(Params(), shared_pblock, true, NULL);
}

bool SidechainClient::GetAverageFees(int nBlocks, int nStartHeight, CAmount& nAverageFee)
{
    // JSON for 'getaveragefees' mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"getaveragefee\", \"params\": ");
    json.append("[");
    json.append(UniValue(nBlocks).write());
    json.append(",");
    json.append(UniValue(nStartHeight).write());
    json.append("]");
    json.append("}");

    // Try to request average fees from mainchain
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request average fees\n");
        return false;
    }

    // Process result
    BOOST_FOREACH(boost::property_tree::ptree::value_type &value, ptree.get_child("result")) {
        // Looping through members
        if (value.first == "feeaverage") {
            // Read
            std::string data = value.second.data();
            if (!data.length()) {
                LogPrintf("ERROR Sidechain client received invalid data\n");
                return false;
            }

            if (ParseMoney(data, nAverageFee)) {
                LogPrintf("Sidechain client received average mainchain fee: %d.\n", nAverageFee);
                return true;
            }
        }
    }
    return false;
}

bool SidechainClient::GetBlockCount(int& nBlocks)
{
    // JSON for 'getblockcount' mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"getblockcount\", \"params\": ");
    json.append("[] }");

    // Try to request mainchain block count
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request block count\n");
        return false;
    }

    // Process result
    nBlocks = ptree.get("result", 0);

    return nBlocks >= 0;
}

bool SidechainClient::GetWorkScore(const uint256& hashWTPrime, int& nWorkScore)
{
    // JSON for 'getworkscore' mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"getworkscore\", \"params\": ");
    json.append("[");
    json.append(UniValue(SIDECHAIN_TEST).write());
    json.append(",");
    json.append("\"");
    json.append(hashWTPrime.ToString());
    json.append("\"");
    json.append("] }");

    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request workscore\n");
        return false;
    }

    // Process result, note that starting workscore on mainchain is 1
    nWorkScore = ptree.get("result", -1);

    return nWorkScore >= 0;
}

bool SidechainClient::ListWTPrimeStatus(std::vector<uint256>& vHashWTPrime)
{
    // TODO for now this function is only being used to see if there are any
    // WT^(s) for nSidechain. The rest of the results could be useful for the
    // GUI though.

    // JSON for 'listwtprimestatus' mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"listwtprimestatus\", \"params\": ");
    json.append("[");
    json.append(UniValue(SIDECHAIN_TEST).write());
    json.append("] }");

    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request WT^ status\n");
        return false;
    }

    // Process result
    BOOST_FOREACH(boost::property_tree::ptree::value_type &value, ptree.get_child("result")) {
        BOOST_FOREACH(boost::property_tree::ptree::value_type &v, value.second.get_child("")) {
            // Looping through members
            if (v.first == "hashwtprime") {
                // Read txid
                std::string data = v.second.data();
                if (!data.length())
                    continue;

                uint256 hash = uint256S(data);
                if (!hash.IsNull())
                    vHashWTPrime.push_back(hash);
            }
        }
    }

    return vHashWTPrime.size() > 0;
}

bool SidechainClient::GetBlockHash(int nHeight, uint256& hashBlock)
{
    // JSON for 'getblockhash' mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"getblockhash\", \"params\": ");
    json.append("[");
    json.append(UniValue(nHeight).write());
    json.append("] }");

    // Try to request mainchain block hash
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request block hash!\n");
        return false;
    }

    std::string strHash = ptree.get("result", "");
    hashBlock = uint256S(strHash);

    return (!hashBlock.IsNull());
}

bool SidechainClient::HaveSpentWTPrime(const uint256& hashWTPrime)
{
    // JSON for 'havespentwtprime' mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"havespentwtprime\", \"params\": ");
    json.append("[");
    json.append("\"");
    json.append(hashWTPrime.ToString());
    json.append("\"");
    json.append(",");
    json.append(UniValue(SIDECHAIN_TEST).write());
    json.append("] }");

    // Try to request mainchain block hash
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request spent WT^!\n");
        return false;
    }

    bool fSpent = ptree.get("result", false);

    return fSpent;
}

bool SidechainClient::HaveFailedWTPrime(const uint256& hashWTPrime)
{
    // JSON for 'havefailedwtprime' mainchain HTTP-RPC
    std::string json;
    json.append("{\"jsonrpc\": \"1.0\", \"id\":\"SidechainClient\", ");
    json.append("\"method\": \"havefailedwtprime\", \"params\": ");
    json.append("[");
    json.append("\"");
    json.append(hashWTPrime.ToString());
    json.append("\"");
    json.append(",");
    json.append(UniValue(SIDECHAIN_TEST).write());
    json.append("] }");

    // Try to request mainchain block hash
    boost::property_tree::ptree ptree;
    if (!SendRequestToMainchain(json, ptree)) {
        LogPrintf("ERROR Sidechain client failed to request failed WT^!\n");
        return false;
    }

    bool fFailed = ptree.get("result", false);

    return fFailed;
}

bool SidechainClient::SendRequestToMainchain(const std::string& json, boost::property_tree::ptree &ptree)
{
    // Format user:pass for authentication
    std::string auth = gArgs.GetArg("-rpcuser", "") + ":" + gArgs.GetArg("-rpcpassword", "");
    if (auth == ":")
        return false;

    // Mainnet RPC = 8332
    // Testnet RPC = 18332
    // Regtest RPC = 18443
    //
    bool fRegtest = gArgs.GetBoolArg("-regtest", false);
    int port = fRegtest ? 18443 : 8332;

    try {
        // Setup BOOST ASIO for a synchronus call to the mainchain
        boost::asio::io_service io_service;
        tcp::resolver resolver(io_service);
        tcp::resolver::query query("127.0.0.1", std::to_string(port));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        tcp::resolver::iterator end;

        tcp::socket socket(io_service);
        boost::system::error_code error = boost::asio::error::host_not_found;

        // Try to connect
        while (error && endpoint_iterator != end)
        {
          socket.close();
          socket.connect(*endpoint_iterator++, error);
        }

        if (error) throw boost::system::system_error(error);

        // HTTP request (package the json for sending)
        boost::asio::streambuf output;
        std::ostream os(&output);
        os << "POST / HTTP/1.1\n";
        os << "Host: 127.0.0.1\n";
        os << "Content-Type: application/json\n";
        os << "Authorization: Basic " << EncodeBase64(auth) << std::endl;
        os << "Connection: close\n";
        os << "Content-Length: " << json.size() << "\n\n";
        os << json;

        // Send the request
        boost::asio::write(socket, output);

        // Read the reponse
        std::string data;
        for (;;)
        {
            boost::array<char, 4096> buf;

            // Read until end of file (socket closed)
            boost::system::error_code e;
            size_t sz = socket.read_some(boost::asio::buffer(buf), e);

            data.insert(data.size(), buf.data(), sz);

            if (e == boost::asio::error::eof)
                break; // socket closed
            else if (e)
                throw boost::system::system_error(e);
        }

        std::stringstream ss;
        ss << data;

        // Get response code
        ss.ignore(std::numeric_limits<std::streamsize>::max(), ' ');
        int code;
        ss >> code;

        // Check response code
        if (code != 200)
            return false;

        // Skip the rest of the header
        for (size_t i = 0; i < 5; i++)
            ss.ignore(std::numeric_limits<std::streamsize>::max(), '\r');

        // Parse json response;
        std::string JSON;
        ss >> JSON;
        std::stringstream jss;
        jss << JSON;
        boost::property_tree::json_parser::read_json(jss, ptree);
    } catch (std::exception &exception) {
        LogPrintf("ERROR Sidechain client (sendRequestToMainchain): %s\n", exception.what());
        return false;
    }
    return true;
}
