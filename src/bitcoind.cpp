// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "clientversion.h"
#include "rpcserver.h"
#include "init.h"
#include "noui.h"
#include "scheduler.h"
#include "util.h"
#include "httpserver.h"
#include "httprpc.h"
#include "rpcserver.h"
#include "unlimited.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include <stdio.h>

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Bitcoin (https://www.bitcoin.org/),
 * which enables instant payments to anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static bool fDaemon;
char *komodo_issuemethod(char *method,char *params,uint16_t port);
extern char ASSETCHAINS_SYMBOL[16],USERPASS[];

void WaitForShutdown(boost::thread_group* threadGroup)
{
    char *retstr; uint32_t counter=0; bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        if ( (counter++ % 100) == 0 )
        {
            if ( (retstr= komodo_issuemethod((char *)"getinfo",0,7771)) != 0 )
            {
                //printf("GETINFO.%s (%s) USERPASS.%s\n",ASSETCHAINS_SYMBOL,retstr,USERPASS);
                free(retstr);
            }
        }
        fShutdown = ShutdownRequested();
    }
    if (threadGroup)
    {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
int32_t iguana_rwnum(int32_t rwflag,uint8_t *serialized,int32_t len,void *endianedp);
uint32_t calc_crc32(uint32_t crc,const void *buf,size_t size);
void komodo_configfile(char *symbol,uint16_t port);

bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    //
    // Parameters
    //
    // If Qt is used, parameters/bitcoin.conf are parsed in qt/bitcoin.cpp's main()
    ParseParameters(argc, argv);
    extern uint16_t ASSETCHAINS_PORT; extern uint32_t ASSETCHAINS_MAGIC,ASSETCHAINS_TIMESTAMP,ASSETCHAIN_INIT;
    extern char ASSETCHAINS_SYMBOL[16]; extern uint64_t ASSETCHAINS_SUPPLY; std::string name;
    
    //ASSETCHAINS_MAGIC = GetArg("-ac_magic",ASSETCHAINS_MAGIC);
    ASSETCHAINS_TIMESTAMP = GetArg("-ac_timestamp",ASSETCHAINS_TIMESTAMP);
    ASSETCHAINS_SUPPLY = GetArg("-ac_supply",10);
    name = GetArg("-ac_name","REVS");
    strncpy(ASSETCHAINS_SYMBOL,name.c_str(),sizeof(ASSETCHAINS_SYMBOL)-1);
    uint8_t buf[512]; int32_t len;
    len = iguana_rwnum(1,buf,sizeof(ASSETCHAINS_TIMESTAMP),(void *)&ASSETCHAINS_TIMESTAMP);
    len += iguana_rwnum(1,&buf[len],sizeof(ASSETCHAINS_SUPPLY),(void *)&ASSETCHAINS_SUPPLY);
    strcpy((char *)&buf[len],ASSETCHAINS_SYMBOL);
    len += strlen(ASSETCHAINS_SYMBOL);
    ASSETCHAINS_MAGIC = calc_crc32(0,buf,len);
    ASSETCHAINS_PORT = GetArg("-ac_port",8000 + (ASSETCHAINS_MAGIC % 7777));
    fprintf(stderr,"after args: %s port.%u magic.%08x timestamp.%u supply.%u\n",ASSETCHAINS_SYMBOL,ASSETCHAINS_PORT,ASSETCHAINS_MAGIC,ASSETCHAINS_TIMESTAMP,(int32_t)ASSETCHAINS_SUPPLY);
    while ( ASSETCHAIN_INIT == 0 )
    {
        sleep(1);
    }
    fprintf(stderr,"%s chain params initialized\n",ASSETCHAINS_SYMBOL);
    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") ||  mapArgs.count("-help") || mapArgs.count("-version"))
    {
        std::string strUsage = _("Bitcoin Unlimited Daemon") + " " + _("version") + " " + FormatFullVersion() + "\n";

        if (mapArgs.count("-version"))
        {
            strUsage += LicenseInfo();
        }
        else
        {
            strUsage += "\n" + _("Usage:") + "\n" +
                  "  assetchaind [options]                     " + _("Start Bitcoin Unlimited Daemon") + "\n";

            strUsage += "\n" + HelpMessage(HMM_BITCOIND);
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return false;
    }

    try
    {
        if (!boost::filesystem::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
            return false;
        }
        try
        {
            ReadConfigFile(mapArgs, mapMultiArgs);
        } catch (const std::exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(ChainNameFromCommandLine());
        } catch (const std::exception& e) {
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "bitcoin:"))
                fCommandLine = true;

        if (fCommandLine)
        {
            fprintf(stderr, "Error: There is no RPC client functionality in assetchaind anymore. Use the bitcoin-cli utility instead.\n");
            exit(1);
        }
#ifndef WIN32
        fDaemon = GetBoolArg("-daemon", false);
        if (fDaemon)
        {
            fprintf(stdout, "Bitcoin server starting\n");

            // Daemonize
            pid_t pid = fork();
            if (pid < 0)
            {
                fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
                return false;
            }
            if (pid > 0) // Parent process, pid is child process id
            {
                return true;
            }
            // Child process falls through to rest of initialization

            pid_t sid = setsid();
            if (sid < 0)
                fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
        }
#endif
        SoftSetBoolArg("-server", true);

        // Set this early so that parameter interactions go to console
        //BaseParams().nRPCPort = ASSETCHAINS_PORT+1;
        InitLogging();
        InitParameterInteraction();
        fRet = AppInit2(threadGroup, scheduler);
        komodo_configfile(ASSETCHAINS_SYMBOL,ASSETCHAINS_PORT + 1);//BaseParams().nRPCPort);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }

    UnlimitedSetup();
    
    if (!fRet)
    {
        Interrupt(threadGroup);
        // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
        // the startup-failure cases to make sure they don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case
    } else {
        WaitForShutdown(&threadGroup);
    }
    Shutdown();

    return fRet;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    // Connect assetchaind signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? 0 : 1);
}
