/*
 * Copyright (C) 2004-2009  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "stdafx.hpp"
#include "znc.h"
#include "Chan.h"
#include "User.h"
#include "Modules.h"

#define REQUIRESSL		1

class CDevice {
public:
	CDevice(const CString& sToken, CModule& Parent)
			: m_Parent(Parent), m_sToken(sToken) {
				m_uPort = 0;
				m_bNew = false;
				m_uFlags = 0;
	}

	virtual ~CDevice() {}

	void RemoveClient(CClient* pClient) {
		m_spClients.erase(pClient);
	}

	bool RemKeyword(const CString& sWord) {
		return (m_ssKeywords.erase(sWord) && Save());
	}

	bool AddKeyword(const CString& sWord) {
		if (!sWord.empty()) {
			m_ssKeywords.insert(sWord);

			return Save();
		}

		return false;
	}

	bool Save() {
		CString sStr(Serialize());

		if (!m_Parent.SetNV("device::" + GetToken(), sStr)) {
			DEBUG("ERROR while saving colloquy info!");
			return false;
		}

		DEBUG("SAVED [" + GetToken() + "]");
		return true;
	}

	bool Push(const CString& sNick, const CString& sMessage, const CString& sChannel, bool bHilite, int iBadge) {
		if (m_sToken.empty()) {
			DEBUG("---- Push(\"" + sNick + "\", \"" + sMessage + "\", \"" + sChannel + "\", " + CString(bHilite) + ", " + CString(iBadge) + ")");
			return false;
		}

		if (!m_uPort || m_sHost.empty()) {
			DEBUG("---- Push() undefined host or port!");
		}

		CString sPayload;

		sPayload = "{";

		sPayload += "\"device-token\":\"" + CollEscape(m_sToken) + "\"";

		if (!sMessage.empty()) {
			sPayload += ",\"message\":\"" + CollEscape(sMessage) + "\"";
		}
		// action
		if (!sNick.empty()) {
			sPayload += ",\"sender\":\"" + CollEscape(sNick) + "\"";
		}

		// this handles the badge
		// 0 resets, other values add/subtract
		if ( iBadge == 0 )
		{
			sPayload += ",\"badge\": \"reset\"";
		}
		else
		{
			sPayload += ",\"badge\": " + CString(iBadge);
		}
		
		if (!sChannel.empty()) {
			sPayload += ",\"room\":\"" + CollEscape(sChannel) + "\"";
		}

		if (!m_sConnectionToken.empty()) {
			sPayload += ",\"connection\":\"" + CollEscape(m_sConnectionToken) + "\"";
		}

		if (!m_sConnectionName.empty()) {
			sPayload += ",\"server\":\"" + CollEscape(m_sConnectionName) + "\"";
		}

		if (bHilite && !m_sHiliteSound.empty()) {
			sPayload += ",\"sound\":\"" + CollEscape(m_sHiliteSound) + "\"";
		} else if (!bHilite && !m_sMessageSound.empty() && iBadge != 0) {
			sPayload += ",\"sound\":\"" + CollEscape(m_sMessageSound) + "\"";
		}

		sPayload += "}";

		DEBUG("Connecting to [" << m_sHost << ":" << m_uPort << "] to send...");
		DEBUG("----------------------------------------------------------------------------");
		DEBUG(sPayload);
		DEBUG("----------------------------------------------------------------------------");

		CSocket *pSock = new CSocket(&m_Parent);
		pSock->Connect(m_sHost, m_uPort, true);
		pSock->Write(sPayload);
		pSock->Close(Csock::CLT_AFTERWRITE);
		m_Parent.AddSocket(pSock);

		return true;
	}

	CString CollEscape(const CString& sStr) const {
		CString sRet(sStr);
		// @todo improve this and eventually support unicode

		sRet.Replace("\\", "\\\\");
		sRet.Replace("\r", "\\r");
		sRet.Replace("\n", "\\n");
		sRet.Replace("\t", "\\t");
		sRet.Replace("\a", "\\a");
		sRet.Replace("\b", "\\b");
#ifndef _WIN32
		sRet.Replace("\e", "\\e");
#endif
		sRet.Replace("\f", "\\f");
		sRet.Replace("\"", "\\\"");

		set<char> ssBadChars; // hack so we don't have to mess around with modifying the string while iterating through it

		for (CString::iterator it = sRet.begin(); it != sRet.end(); it++) {
			if (!isprint(*it)) {
				ssBadChars.insert(*it);
			}
		}

		for (set<char>::iterator b = ssBadChars.begin(); b != ssBadChars.end(); b++) {
			sRet.Replace(CString(*b), ToHex(*b));
		}

		return sRet;
	}

	CString ToHex(const char c) const {
		return "\\u00" + CString(c).Escape_n(CString::EURL).TrimPrefix_n("%");
	}

	bool Parse(const CString& sStr) {
		VCString vsLines;
		sStr.Split("\n", vsLines);

		if (vsLines.size() != 9) {
			DEBUG("Wrong number of lines [" << vsLines.size() << "] [" + sStr + "]");
			for (unsigned int a = 0; a < vsLines.size(); a++) {
				DEBUG("=============== [" + vsLines[a] + "]");
			}

			return false;
		}

		m_sToken = vsLines[0];
		m_sName = vsLines[1];
		m_sHost = vsLines[2].Token(0, false, ":");
		m_uPort = vsLines[2].Token(1, false, ":").ToUInt();
		m_uFlags = vsLines[3].ToUInt();
		m_sConnectionToken = vsLines[4];
		m_sConnectionName = vsLines[5];
		m_sMessageSound = vsLines[6];
		m_sHiliteSound = vsLines[7];
		vsLines[8].Split("\t", m_ssKeywords, false);

		return true;
	}

	CString Serialize() const {
		CString sRet(m_sToken.FirstLine() + "\n"
			+ m_sName.FirstLine() + "\n"
			+ m_sHost.FirstLine() + ":" + CString(m_uPort) + "\n"
			+ CString(m_uFlags) + "\n"
			+ m_sConnectionToken.FirstLine() + "\n"
			+ m_sConnectionName.FirstLine() + "\n"
			+ m_sMessageSound.FirstLine() + "\n"
			+ m_sHiliteSound.FirstLine() + "\n");

		for (SCString::const_iterator it = m_ssKeywords.begin(); it != m_ssKeywords.end(); it++) {
			if (it != m_ssKeywords.begin()) {
				sRet += "\t";
			}

			sRet += (*it).FirstLine();
		}

		sRet += "\t"; // Hack to work around a bug, @todo remove once fixed

		return sRet;
	}

	// Getters
	CString GetToken() const { return m_sToken; }
	CString GetName() const { return m_sName; }
	CString GetConnectionToken() const { return m_sConnectionToken; }
	CString GetConnectionName() const { return m_sConnectionName; }
	CString GetMessageSound() const { return m_sMessageSound; }
	CString GetHiliteSound() const { return m_sHiliteSound; }
	const SCString& GetKeywords() const { return m_ssKeywords; }
	bool IsConnected() const { return !m_spClients.empty(); }
	bool HasClient(CClient* p) const { return m_spClients.find(p) != m_spClients.end(); }
	CString GetHost() const { return m_sHost; }
	unsigned short GetPort() const { return m_uPort; }
	bool IsNew() const { return m_bNew; }

	// Setters
	void SetToken(const CString& s) { m_sToken = s; }
	void SetName(const CString& s) { m_sName = s; }
	void SetConnectionToken(const CString& s) { m_sConnectionToken = s; }
	void SetConnectionName(const CString& s) { m_sConnectionName = s; }
	void SetMessageSound(const CString& s) { m_sMessageSound = s; }
	void SetHiliteSound(const CString& s) { m_sHiliteSound = s; }
	void AddClient(CClient* p) { m_spClients.insert(p); }
	void SetHost(const CString& s) { m_sHost = s; }
	void SetPort(unsigned short u) { m_uPort = u; }
	void SetNew(bool b = true) { m_bNew = b; }

	// Flags
	void SetFlag(unsigned int u) { m_uFlags |= u; }
	void UnsetFlag(unsigned int u) { m_uFlags &= ~u; }
	bool HasFlag(unsigned int u) const { return m_uFlags & u; }

	enum EFlags {
		Disabled     = 1 << 0,
		IncludeMsg   = 1 << 1,
		IncludeNick  = 1 << 2,
		IncludeChan  = 1 << 3
	};
	// !Flags

	void Reset() {
		m_sToken.clear();
		m_sName.clear();
		m_sConnectionToken.clear();
		m_sConnectionName.clear();
		m_sMessageSound.clear();
		m_sHiliteSound.clear();
		m_sHost.clear();
		m_uPort = 0;
		m_uFlags = 0;
		m_ssKeywords.clear();
	}

private:
	set<CClient*>  m_spClients;
	CModule&       m_Parent;
	bool           m_bNew;
	CString        m_sToken;
	CString        m_sName;
	CString        m_sConnectionToken;
	CString        m_sConnectionName;
	CString        m_sMessageSound;
	CString        m_sHiliteSound;
	SCString       m_ssKeywords;
	CString        m_sHost;
	unsigned short m_uPort;
	unsigned int   m_uFlags;
};


class CColloquyMod : public CModule {
public:
	MODCONSTRUCTOR(CColloquyMod) {
		LoadRegistry();
 
		for (MCString::iterator it = BeginNV(); it != EndNV(); it++) {
			CString sKey(it->first);

			if (sKey.TrimPrefix("device::")) {
				CDevice* pDevice = new CDevice(sKey, *this);

				if (!pDevice->Parse(it->second)) {
					DEBUG("  --- Error while parsing device [" + sKey + "]");
					delete pDevice;
					continue;
				}

				m_mspDevices[pDevice->GetToken()] = pDevice;
			} else {
				DEBUG("   --- Unknown registry entry: [" << it->first << "]");
			}
		}
	}

	virtual ~CColloquyMod() {
		for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
			it->second->Save();
			delete it->second;
		}
	}

	virtual EModRet OnUserRaw(CString& sLine) {
		// Trap "ISON *modname" and fake a reply or else colloquy won't let you communicate with *status or *module
		// This is a hack in that it doesn't support multiple users
		const CString& sStatusPrefix(CZNC::Get().GetStatusPrefix());

		if (sLine.Equals("ISON " + sStatusPrefix, false, 5 + sStatusPrefix.size())) {
			PutUser(":" + m_pUser->GetIRCServer() + " 303 " + m_pUser->GetIRCNick().GetNick() + " :" + sLine.Token(1, true));

			return HALTCORE;
		}

		// Trap the PUSH messages that colloquy sends to give us info about the client
		if (sLine.TrimPrefix("PUSH ")) {
			if (sLine.TrimPrefix("add-device ")) {
				CString sToken(sLine.Token(0));
				CDevice* pDevice = FindDevice(sToken);

				if (!pDevice) {
					pDevice = new CDevice(sToken, *this);
					pDevice->SetNew();
					m_mspDevices[pDevice->GetToken()] = pDevice;
				}

				pDevice->Reset();
				pDevice->SetToken(sToken);
				pDevice->SetName(sLine.Token(1, true).TrimPrefix_n(":"));
				pDevice->AddClient(m_pClient);
			} else if (sLine.TrimPrefix("remove-device :")) {
				CDevice* pDevice = FindDevice(sLine);

				if (pDevice) {
					pDevice->SetFlag(CDevice::Disabled);
					//PutModule("Disabled phone [" + pDevice->GetName() + "]");
					m_pUser->PutModule(GetModName(), "Disabled phone [" + pDevice->GetName() + "]", NULL, m_pClient);
				}
			} else {
				CDevice* pDevice = FindDevice(m_pClient);

				if (pDevice) {
					if (sLine.TrimPrefix("connection ")) {
						pDevice->SetConnectionToken(sLine.Token(0));
						pDevice->SetConnectionName(sLine.Token(1, true).TrimPrefix_n(":"));
					} else if (sLine.TrimPrefix("service ")) {
						pDevice->SetHost(sLine.Token(0));
						pDevice->SetPort(sLine.Token(1).ToUInt());
					} else if (sLine.TrimPrefix("highlight-word :")) {
						pDevice->AddKeyword(sLine);
					} else if (sLine.TrimPrefix("highlight-sound :")) {
						pDevice->SetHiliteSound(sLine);
					} else if (sLine.TrimPrefix("message-sound :")) {
						pDevice->SetMessageSound(sLine);
					} else if (sLine.Equals("end-device")) {
						if (!pDevice->Save()) {
							PutModule("Unable to save phone [" + pDevice->GetName() + "]");
						} else {
							if (pDevice->IsNew()) {
								pDevice->SetNew(false);
								m_pUser->PutModule(GetModName(), "Added new phone [" + pDevice->GetName() + "] to the system", NULL, m_pClient);
							}
						}
					} else {
						DEBUG("---------------------------------------------------------------------- PUSH ERROR [" + sLine + "]");
					}
				} else {
					DEBUG("No pDevice defined for this client!");
				}
			}

			return HALT;
		}

		return CONTINUE;
	}

	CDevice* FindDevice(const CString& sToken) {
		map<CString, CDevice*>::iterator it = m_mspDevices.find(sToken);

		if (it != m_mspDevices.end()) {
			return it->second;
		}

		return NULL;
	}

	CDevice* FindDevice(CClient* pClient) {
		for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
			if (it->second->HasClient(pClient)) {
				return it->second;
			}
		}

		return NULL;
	}

	virtual EModRet OnPrivNotice(CNick& Nick, CString& sMessage) {
		Push(Nick.GetNick(), sMessage, "", false, 1);
		return CONTINUE;
	}

	virtual EModRet OnChanNotice(CNick& Nick, CChan& Channel, CString& sMessage) {
		Push(Nick.GetNick(), sMessage, Channel.GetName(), true, 1);
		return CONTINUE;
	}

	virtual EModRet OnPrivMsg(CNick& Nick, CString& sMessage) {
		Push(Nick.GetNick(), sMessage, "", false, 1);
		return CONTINUE;
	}

	virtual EModRet OnChanMsg(CNick& Nick, CChan& Channel, CString& sMessage) {
		Push(Nick.GetNick(), sMessage, Channel.GetName(), true, 1);
		return CONTINUE;
	}

	virtual void OnModCommand(const CString& sCommand) {
		if (sCommand.Equals("HELP")) {
			PutModule("Commands: HELP, LIST");
		} else if (sCommand.Equals("LIST")) {
			if (m_mspDevices.empty()) {
				PutModule("You have no saved devices...");
				PutModule("Connect to znc using your mobile colloquy client...");
			   	PutModule("Make sure to enable push if it isn't already!");
			} else {
				CTable Table;
				Table.AddColumn("Phone");
				Table.AddColumn("Connection");
				Table.AddColumn("MsgSound");
				Table.AddColumn("HiliteSound");
				Table.AddColumn("Status");
				Table.AddColumn("Keywords");

				for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
					CDevice* pDevice = it->second;

					Table.AddRow();
					Table.SetCell("Phone", pDevice->GetName());
					Table.SetCell("Connection", pDevice->GetConnectionName());
					Table.SetCell("MsgSound", pDevice->GetMessageSound());
					Table.SetCell("HiliteSound", pDevice->GetHiliteSound());
					Table.SetCell("Status", pDevice->HasFlag(CDevice::Disabled) ? "Disabled" : (pDevice->IsConnected() ? "Connected" : "Offline"));

					const SCString& ssWords = pDevice->GetKeywords();

					if (!ssWords.empty()) {
						CString sWords;
						sWords = "[";

						for (SCString::const_iterator it2 = ssWords.begin(); it2 != ssWords.end(); it2++) {
							if (it2 != ssWords.begin()) {
								sWords += "]  [";
							}

							sWords += *it2;
						}

						sWords += "]";

						Table.SetCell("Keywords", sWords);
					}
				}

				PutModule(Table);
			}
		/*
		} else if (sCommand.Token(0).Equals("REMKEYWORD")) {
			CString sKeyword(sCommand.Token(1, true));

			if (sKeyword.empty()) {
				PutModule("Usage: RemKeyWord <keyword/phrase>");
				return;
			} else {
				// @todo probably want to make this global and let each device manage its own keywords
				for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
					it->second->RemKeyword(sKeyword);
				}

				PutModule("Removed keyword [" + sKeyword + "]");
			}
		} else if (sCommand.Token(0).Equals("ADDKEYWORD")) {
			CString sKeyword(sCommand.Token(1, true));

			if (sKeyword.empty()) {
				PutModule("Usage: AddKeyWord <keyword/phrase>");
				return;
			} else {
				// @todo probably want to make this global and let each device manage its own keywords
				for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
					it->second->AddKeyword(sKeyword);
				}

				PutModule("Added keyword [" + sKeyword + "]");
			}
		} else if (sCommand.Equals("LISTNV")) {
			if (BeginNV() == EndNV()) {
				PutModule("No NVs!");
			} else {
				for (MCString::iterator it = BeginNV(); it != EndNV(); it++) {
					PutModule(it->first + ": " + it->second);
				}
			}
		*/
		}
	}

	bool Test(const CString& sKeyWord, const CString& sString) {
		return (!sKeyWord.empty() && (
			sString.Equals(sKeyWord + " ", false, sKeyWord.length() +1)
			|| sString.Right(sKeyWord.length() +1).Equals(" " + sKeyWord)
			|| sString.AsLower().WildCmp("* " + sKeyWord.AsLower() + " *")
			|| (sKeyWord.find_first_of("*?") != CString::npos && sString.AsLower().WildCmp(sKeyWord.AsLower()))
		));
	}

	bool Push(const CString& sNick, const CString& sMessage, const CString& sChannel, bool bHilite, int iBadge) {
		bool bRet = true;
		vector<CClient*>& vpClients = m_pUser->GetClients();

		// Cycle through all of the cached devices
		for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
			CDevice* pDevice = it->second;

			// Determine if this cached device has a client still connected
			bool bFound = false;

			for (vector<CClient*>::size_type a = 0; a < vpClients.size(); a++) {
				if (pDevice->HasClient(vpClients[a])) {
					bFound = true;
					break;
				}
			}

			// If the current cached device was found to still be connected, don't push the msg
			// unless we're trying to set badge to 0
			if (bFound && iBadge != 0) {
				return false;
			}

			// If it's a highlight, then we need to make sure it matches a highlited word
			if (bHilite) {
				// Test our current irc nick
				const CString& sMyNick(m_pUser->GetIRCNick().GetNick());
				bool bMatches = Test(sMyNick, sMessage) || Test(sMyNick + "?*", sMessage);

				// If our nick didn't match, test the list of keywords for this device
				if (!bMatches) {
					const SCString& ssWords = pDevice->GetKeywords();

					for (SCString::const_iterator it2 = ssWords.begin(); it2 != ssWords.end(); it2++) {
						if (Test(*it2, sMessage)) {
							bMatches = true;
							break;
						}
					}
				}

				if (!bMatches) {
					return false;
				}
			}

			if (!pDevice->Push(sNick, sMessage, sChannel, bHilite, iBadge)) {
				bRet = false;
			}
		}

		return bRet;
	}

	virtual void OnClientLogin() {
		// Clear all badges on a client login
		// this could be easily modded to only clear them for the connecting client
		Push("","","",false,0);
	}

	virtual void OnClientDisconnect() {
		for (map<CString, CDevice*>::iterator it = m_mspDevices.begin(); it != m_mspDevices.end(); it++) {
			it->second->RemoveClient(m_pClient);
		}
	}

private:
	map<CString, CDevice*>	m_mspDevices;	// map of token to device info for clients who have sent us PUSH info
};

MODULEDEFS(CColloquyMod, "Push privmsgs and highlights to your iPhone via Colloquy Mobile")