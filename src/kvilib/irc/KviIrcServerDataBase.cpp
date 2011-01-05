//=============================================================================
//
//   File : KviIrcServerDataBase.cpp
//   Creation date : Mon Jul 10 2000 14:25:00 by Szymon Stefanek
//
//   This file is part of the KVIrc irc client distribution
//   Copyright (C) 2000-2010 Szymon Stefanek (pragma at kvirc dot net)
//
//   This program is FREE software. You can redistribute it and/or
//   modify it under the terms of the GNU General Public License
//   as published by the Free Software Foundation; either version 2
//   of the License, or (at your opinion) any later version.
//
//   This program is distributed in the HOPE that it will be USEFUL,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//   See the GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, write to the Free Software Foundation,
//   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//=============================================================================


#include "KviIrcServerDataBase.h"
#include "KviConfigurationFile.h"
#include "KviLocale.h"
#include "KviNetUtils.h"
#include "KviNickServRuleSet.h"

#include <QApplication>
#include <QLayout>
#include <QMessageBox>
#include <QCheckBox>

KviIrcServerDataBase::KviIrcServerDataBase()
{
	m_pRecords = new KviPointerHashTable<QString,KviIrcNetwork>(17,false);
	m_pRecords->setAutoDelete(true);
	m_pAutoConnectOnStartupServers = 0;
	m_pAutoConnectOnStartupNetworks = 0;
}

KviIrcServerDataBase::~KviIrcServerDataBase()
{
	delete m_pRecords;
	if(m_pAutoConnectOnStartupServers)
		delete m_pAutoConnectOnStartupServers;
	if(m_pAutoConnectOnStartupNetworks)
		delete m_pAutoConnectOnStartupNetworks;
}

void KviIrcServerDataBase::clearAutoConnectOnStartupServers()
{
	if(!m_pAutoConnectOnStartupServers)
		return;

	delete m_pAutoConnectOnStartupServers;
	m_pAutoConnectOnStartupServers = 0;
}

void KviIrcServerDataBase::clearAutoConnectOnStartupNetworks()
{
	if(!m_pAutoConnectOnStartupNetworks)
		return;

	delete m_pAutoConnectOnStartupNetworks;
	m_pAutoConnectOnStartupNetworks = 0;
}

void KviIrcServerDataBase::clear()
{
	m_pRecords->clear();
	m_szCurrentNetwork = "";
}

void KviIrcServerDataBase::addNetwork(KviIrcNetwork * pNet)
{
	m_pRecords->replace(pNet->name(),pNet);
}

KviIrcNetwork * KviIrcServerDataBase::findNetwork(const QString & szName)
{
	KviIrcNetwork * pNet = m_pRecords->find(szName);
	return pNet;
}

unsigned int KviIrcServerDataBase::networkCount() const
{
	return m_pRecords->count();
}

KviIrcNetwork * KviIrcServerDataBase::currentNetwork()
{
	KviIrcNetwork * pNet = 0;
	if(!m_szCurrentNetwork.isEmpty())
		pNet = m_pRecords->find(m_szCurrentNetwork);
	if(pNet)
		return pNet;

	return 0;
}

bool KviIrcServerDataBase::makeCurrentBestServerInNetwork(const QString & szNetName, KviIrcNetwork * pNet, QString & szError)
{
	m_szCurrentNetwork = szNetName;
	// find a round-robin server in that network

	if(pNet->m_pServerList->isEmpty())
	{
		szError = __tr2qs("The specified network has no server entries");
		return false;
	}

	for(KviIrcServer * pServ = pNet->m_pServerList->first(); pServ; pServ = pNet->m_pServerList->next())
	{
		if(pServ->m_szDescription.contains("random",Qt::CaseInsensitive) ||
			(pServ->m_szDescription.contains("round",Qt::CaseInsensitive) &&
			(pServ->m_szDescription.contains("robin",Qt::CaseInsensitive))))
		{
			pNet->setCurrentServer(pServ);
			return true;
		}
	}

	// no explicit round robin... try some common names

	QString szTryAlso1, szTryAlso2, szTryAlso3;

	KviQString::sprintf(szTryAlso1,"irc.%Q.org",&szNetName);
	KviQString::sprintf(szTryAlso2,"irc.%Q.net",&szNetName);
	KviQString::sprintf(szTryAlso3,"irc.%Q.com",&szNetName);

	for(KviIrcServer * pServer = pNet->m_pServerList->first(); pServer; pServer = pNet->m_pServerList->next())
	{
		if(KviQString::equalCI(pServer->m_szHostname,szTryAlso1) ||
			KviQString::equalCI(pServer->m_szHostname,szTryAlso2) ||
			KviQString::equalCI(pServer->m_szHostname,szTryAlso3))
		{
			pNet->setCurrentServer(pServer);
			return true;
		}
	}

	// a random one in this network
	return true;
}

bool KviIrcServerDataBase::makeCurrentServer(KviIrcServerDefinition * pDef, QString & szError)
{
	KviIrcServer * pServer = 0;

	KviPointerHashTableIterator<QString,KviIrcNetwork> it(*m_pRecords);
	KviIrcNetwork * pNet = 0;
	KviIrcServer * pServ;

	if(KviQString::equalCIN(pDef->szServer,"net:",4))
	{
		// net:networkname form
		QString szNet = pDef->szServer;
		szNet.remove(0,4);
		KviIrcNetwork * pNet = m_pRecords->find(szNet);
		if(pNet)
			return makeCurrentBestServerInNetwork(szNet,pNet,szError);
		szError = __tr2qs("The server specification seems to be in the net:<string> but the network couln't be found in the database");
		return false;
	}

	if(KviQString::equalCIN(pDef->szServer,"id:",3))
	{
		// id:serverid form
		QString szId = pDef->szServer;
		szId.remove(0,3);

		while((pNet = it.current()))
		{
			for(pServ = pNet->serverList()->first(); pServ && (!pServer); pServ = pNet->serverList()->next())
			{
				if(KviQString::equalCI(pServ->id(),szId))
				{
					pServer = pServ;
					goto search_finished;
				}
			}
			++it;
		}
		szError = __tr2qs("The server specification seems to be in the id:<string> form but the identifier coulnd't be found in the database");
		return false;
	}

	it.toFirst();

	while((pNet = it.current()))
	{
		for(pServ = pNet->serverList()->first(); pServ && (!pServer); pServ = pNet->serverList()->next())
		{
			if(KviQString::equalCI(pServ->hostName(),pDef->szServer))
			{
				if(pDef->szId.isEmpty() || KviQString::equalCI(pServ->id(),pDef->szId))
				{
					if(pDef->bIPv6 == pServ->isIPv6())
					{
						if(pDef->bSSL == pServ->useSSL())
						{
							if(pDef->bPortIsValid)
							{
								// must match the port
								if(pDef->uPort == pServ->port())
								{
									// port matches
									if(!pDef->szLinkFilter.isEmpty())
									{
										// must match the link filter
										if(KviQString::equalCI(pDef->szLinkFilter,pServ->linkFilter()))
										{
											// link filter matches
											pServer = pServ;
											goto search_finished;
										} // else link filter doesn't match
									} else {
										// no need to match the link filter
										pServer = pServ;
										goto search_finished;
									}
								} // else port doesn't match
							} else {
								// no need to match the port
								if(!pDef->szLinkFilter.isEmpty())
								{
									// must match the link filter
									if(KviQString::equalCI(pDef->szLinkFilter,pServ->linkFilter()))
									{
										// link filter matches
										pServer = pServ;
										goto search_finished;
									} // else link filter doesn't match
								} else {
									// no need to match the link filter
									pServer = pServ;
									goto search_finished;
								}
							}
						}
					}
				}
			}
		}
		++it;
	}

search_finished:

	if(pNet && pServer)
	{
		m_szCurrentNetwork = pNet->name();
		pNet->setCurrentServer(pServer);
		return true;
	}

	// no such server: is it a valid ip address or hostname ?
	bool bIsValidIPv4 = KviNetUtils::isValidStringIp(pDef->szServer);
#ifdef COMPILE_IPV6_SUPPORT
	bool bIsValidIPv6 = KviNetUtils::isValidStringIPv6(pDef->szServer);
#else
	bool bIsValidIPv6 = false;
#endif

	if(!(bIsValidIPv4 || bIsValidIPv6))
	{
		// is it a valid hostname ? (must contain at least one dot)
		if(!pDef->szServer.contains('.'))
		{
			// assume it is a network name!
			KviIrcNetwork * pNet = m_pRecords->find(pDef->szServer);
			if(pNet)
				return makeCurrentBestServerInNetwork(pDef->szServer,pNet,szError);
			// else probably not a network name
		}
	}

	// a valid hostname or ip address, not found in list : add it and make it current

	pNet = m_pRecords->find(__tr2qs("Standalone Servers"));
	if(!pNet)
	{
		pNet = new KviIrcNetwork(__tr2qs("Standalone Servers"));
		m_pRecords->replace(pNet->name(),pNet);
	}

	KviIrcServer * pSrv = new KviIrcServer();
	pSrv->m_szHostname = pDef->szServer;
	if(bIsValidIPv4)
	{
		pSrv->m_szIp = pDef->szServer;
		pSrv->setCacheIp(true);
#ifdef COMPILE_IPV6_SUPPORT
	} else {
		if(bIsValidIPv6)
		{
			pSrv->m_szIp = pDef->szServer;
			pSrv->setCacheIp(true);
			pDef->bIPv6 = true;
		}
	}
#else
	}
#endif
	pSrv->m_uPort = pDef->bPortIsValid ? pDef->uPort : 6667;
	pSrv->setLinkFilter(pDef->szLinkFilter);
	pSrv->m_szPass = pDef->szPass;
	pSrv->m_szNick = pDef->szNick;
	pSrv->m_szInitUMode = pDef->szInitUMode;
	pSrv->setIPv6(pDef->bIPv6);
	pSrv->setUseSSL(pDef->bSSL);
	pSrv->setEnabledSTARTTLS(pDef->bSTARTTLS);
	pNet->insertServer(pSrv);
	m_szCurrentNetwork = pNet->name();
	pNet->setCurrentServer(pSrv);

	return true;
}

void parseMircServerRecord(QString szEntry, QString & szNet,
QString & szDescription, QString & szHost, QString & szPort, bool & bSsl, kvi_u32_t & uPort)
{
	bSsl = false;
	int iIdx = KviQString::find(szEntry,"SERVER:");
	if(iIdx != -1)
	{
		szDescription = szEntry.left(iIdx);
		szNet = szDescription.section(':',0,0);
		szDescription = szDescription.section(':',1,1);

		szEntry.remove(0,iIdx + 7);
		iIdx = KviQString::find(szEntry,"GROUP:");
		if(iIdx != -1)
		{
			szHost = szEntry.left(iIdx);
		} else {
			szHost = szEntry;
		}

		szPort = szHost.section(':',1,1);
		if(szPort[0]=='+')
		{
			bSsl = true;
			szPort.remove(0,1);
		}
		szHost = szHost.section(':',0,0);

		bool bOk;
		uPort = szPort.toUInt(&bOk);
		if(!bOk)
			uPort = 6667;
	}
}

void KviIrcServerDataBase::importFromMircIni(const QString & szFilename, const QString & szMircIni, QStringList & recentServers)
{
	clear();
	recentServers.clear();
	QString szDefaultServer;
	KviConfigurationFile mircCfg(szMircIni,KviConfigurationFile::Read,true);
	if(mircCfg.hasGroup("mirc"))
	{
		mircCfg.setGroup("mirc");
		szDefaultServer = mircCfg.readEntry("host");
	}

	KviConfigurationFile cfg(szFilename,KviConfigurationFile::Read,true);
	int i = 0;

	QString szEntry;
	QString szKey;
	if(cfg.hasGroup("recent"))
	{
		cfg.setGroup("recent");
		do {
			KviQString::sprintf(szKey,"n%d",i);
			szEntry = cfg.readEntry(szKey);
			if(!szEntry.isEmpty())
			{
				QString szNet;
				QString szDescription;
				QString szHost;
				QString szPort;
				bool bSsl = false;
				kvi_u32_t uPort = 0;

				parseMircServerRecord(szEntry,szNet,
					   szDescription,szHost,szPort,bSsl,uPort);

				recentServers << (bSsl ? "ircs://" : "irc://" ) + szHost + ":" + szPort;
			}
			i++;
		} while(!szEntry.isEmpty());
	}

	i = 0;
	if(cfg.hasGroup("servers"))
	{
		cfg.setGroup("servers");
		do {
			KviQString::sprintf(szKey,"n%d",i);
			szEntry = cfg.readEntry(szKey);
			if(!szEntry.isEmpty())
			{
				bool bDefault = false;
				QString szNet;
				QString szDescription;
				QString szHost;
				QString szPort;
				bool bSsl = false;
				kvi_u32_t uPort = 0;
				// <net>:<description>SERVER:<server:port>GROUP:<group???>
				if(szEntry == szDefaultServer)
					bDefault = true;

				parseMircServerRecord(szEntry,szNet,
						   szDescription,szHost,szPort,bSsl,uPort);

				KviIrcNetwork * pNet = findNetwork(szNet);

				if(!pNet)
				{
					pNet = new KviIrcNetwork(szNet);
					addNetwork(pNet);
				}

				KviIrcServer * pServ = new KviIrcServer();
				pServ->m_szHostname = szHost;
				pServ->m_szDescription = szDescription;
				pServ->m_uPort = uPort;


				pNet->m_pServerList->append(pServ);
				if(bDefault)
				{
					m_szCurrentNetwork = szNet;
				}
			}
			i++;
		} while(!szEntry.isEmpty());
	}
}

void KviIrcServerDataBase::load(const QString & szFilename)
{
	clear();
	KviConfigurationFile cfg(szFilename,KviConfigurationFile::Read);

	KviConfigurationFileIterator it(*(cfg.dict()));

	QString szTmp;

	while(it.current())
	{
		if(it.current()->count() > 0)
		{
			KviIrcNetwork * pNewNet = new KviIrcNetwork(it.currentKey());
			addNetwork(pNewNet);
			cfg.setGroup(it.currentKey());
			pNewNet->m_szEncoding = cfg.readEntry("Encoding");
			pNewNet->m_szTextEncoding = cfg.readEntry("TextEncoding");
			pNewNet->m_szDescription = cfg.readEntry("Description");
			pNewNet->m_szNickName = cfg.readEntry("NickName");
			pNewNet->m_szRealName = cfg.readEntry("RealName");
			pNewNet->m_szUserName = cfg.readEntry("UserName");
			pNewNet->m_szPass = cfg.readEntry("Pass");
			pNewNet->m_szOnConnectCommand = cfg.readEntry("OnConnectCommand");
			pNewNet->m_szOnLoginCommand = cfg.readEntry("OnLoginCommand");
			pNewNet->m_pNickServRuleSet = KviNickServRuleSet::load(&cfg,QString());
			pNewNet->m_bAutoConnect = cfg.readBoolEntry("AutoConnect",false);
			pNewNet->m_szUserIdentityId = cfg.readEntry("UserIdentityId");
			if(pNewNet->m_bAutoConnect)
			{
				if(!m_pAutoConnectOnStartupNetworks)
				{
					m_pAutoConnectOnStartupNetworks = new KviPointerList<KviIrcNetwork>;
					m_pAutoConnectOnStartupNetworks->setAutoDelete(false);
				}
				m_pAutoConnectOnStartupNetworks->append(pNewNet);
			}
			QStringList l = cfg.readStringListEntry("AutoJoinChannels",QStringList());
			if(l.count() > 0)
				pNewNet->setAutoJoinChannelList(new QStringList(l));

			if(cfg.readBoolEntry("Current",false))
				m_szCurrentNetwork = it.currentKey();

			int nServers = cfg.readIntEntry("NServers",0);
			for(int i=0; i < nServers; i++)
			{
				KviIrcServer * pServ = new KviIrcServer();
				KviQString::sprintf(szTmp,"%d_",i);
				if(pServ->load(&cfg,szTmp))
				{
					pNewNet->m_pServerList->append(pServ);
					KviQString::sprintf(szTmp,"%d_Current",i);
					if(cfg.readBoolEntry(szTmp,false))
						pNewNet->m_pCurrentServer = pServ;
					if(pServ->autoConnect())
					{
						if(!m_pAutoConnectOnStartupServers)
						{
							m_pAutoConnectOnStartupServers = new KviPointerList<KviIrcServer>;
							m_pAutoConnectOnStartupServers->setAutoDelete(false);
						}
						m_pAutoConnectOnStartupServers->append(pServ);
					}
				} else delete pServ;
			}
			if(!pNewNet->m_pCurrentServer)
				pNewNet->m_pCurrentServer = pNewNet->m_pServerList->first();
		}
		++it;
	}
}

void KviIrcServerDataBase::save(const QString & szFilename)
{
	KviConfigurationFile cfg(szFilename,KviConfigurationFile::Write);

	cfg.clear(); // clear any old entry

	KviPointerHashTableIterator<QString,KviIrcNetwork> it(*m_pRecords);

	QString szTmp;

	while(KviIrcNetwork * pNetwork = it.current())
	{
		cfg.setGroup(pNetwork->name());
		cfg.writeEntry("NServers",pNetwork->m_pServerList->count());
		if(pNetwork->m_bAutoConnect)
			cfg.writeEntry("AutoConnect",true);
		if(!pNetwork->m_szEncoding.isEmpty())
			cfg.writeEntry("Encoding",pNetwork->m_szEncoding);
		if(!pNetwork->m_szTextEncoding.isEmpty())
			cfg.writeEntry("TextEncoding",pNetwork->m_szTextEncoding);
		if(!pNetwork->m_szDescription.isEmpty())
			cfg.writeEntry("Description",pNetwork->m_szDescription);
		if(!pNetwork->m_szNickName.isEmpty())
			cfg.writeEntry("NickName",pNetwork->m_szNickName);
		if(!pNetwork->m_szRealName.isEmpty())
			cfg.writeEntry("RealName",pNetwork->m_szRealName);
		if(!pNetwork->m_szUserName.isEmpty())
			cfg.writeEntry("UserName",pNetwork->m_szUserName);
		if(!pNetwork->m_szPass.isEmpty())
			cfg.writeEntry("Pass",pNetwork->m_szPass);
		if(!pNetwork->m_szOnConnectCommand.isEmpty())
			cfg.writeEntry("OnConnectCommand",pNetwork->m_szOnConnectCommand);
		if(!pNetwork->m_szOnLoginCommand.isEmpty())
			cfg.writeEntry("OnLoginCommand",pNetwork->m_szOnLoginCommand);
		if(pNetwork->m_pNickServRuleSet)pNetwork->m_pNickServRuleSet->save(&cfg,QString());
		if(pNetwork->autoJoinChannelList())
			cfg.writeEntry("AutoJoinChannels",*(pNetwork->autoJoinChannelList()));
		if(pNetwork->m_szName == m_szCurrentNetwork)cfg.writeEntry("Current",true);
		if(!pNetwork->m_szUserIdentityId.isEmpty())
			cfg.writeEntry("UserIdentityId",pNetwork->m_szUserIdentityId);
		int i=0;
		for(KviIrcServer * pServ = pNetwork->m_pServerList->first(); pServ; pServ = pNetwork->m_pServerList->next())
		{
			KviQString::sprintf(szTmp,"%d_",i);
			pServ->save(&cfg,szTmp);

			if(pServ == pNetwork->m_pCurrentServer)
			{
				KviQString::sprintf(szTmp,"%d_Current",i);
				cfg.writeEntry(szTmp,true);
			}

			i++;
		}
		++it;
	}
}