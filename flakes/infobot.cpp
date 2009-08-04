/*
 * Copyright (C) 2009 flakes @ EFNet
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "main.h"
#include "znc.h"
#include "Chan.h"
#include "Modules.h"
#include "User.h"

#include <pcre.h>
#include <pcrecpp.h>

#include <ctype.h>

#define NR_OF_TRIGGERS 5

using namespace pcrecpp;

typedef map<const CString, VCString> TEnabledChanMap;
typedef map<const CString, bool> TColorsEnabledMap;
typedef map<const CString, char> TTriggerCharMap;

class CInfoBotModule : public CModule
{
private:
	TEnabledChanMap m_enabledCmds;
	TColorsEnabledMap m_coloredChans;
	TTriggerCharMap m_triggerChars;
	int m_colorOne, m_colorTwo;
	
	static const char* TRIGGERS[NR_OF_TRIGGERS];
	static const char* TRIGGERS_STR;
	static const char* TRIGGER_DESCRIPTIONS[NR_OF_TRIGGERS];
	
	static bool IsTriggerSupported(const CString& sTriggerName);
	bool IsTriggerEnabled(const CString& sChan, const CString& sTriggerName);
	void DisableTrigger(const CString& sChan, const CString& sTriggerName);
	void CheckLineForTrigger(const CString& sMessage, const CString& sChannel, const CString& sNick);
	
	void SaveSettings();
	void LoadSettings();
public:
	MODCONSTRUCTOR(CInfoBotModule)
	{
		m_colorOne = 7;
		m_colorTwo = 14;
	}
	
	bool ColorsEnabled(const CString& sChan);
	char TriggerChar(const CString& sChan);
	void SendMessage(const CString& sSendTo, const CString& sMsg);
	static CString Do8Ball();
	
	bool OnLoad(const CString& sArgsi, CString& sMessage);
	void OnModCommand(const CString& sCommand);
	EModRet OnChanMsg(CNick& Nick, CChan& Channel, CString& sMessage);
	EModRet OnUserMsg(CString& sTarget, CString& sMessage);	
	~CInfoBotModule();
};


static CString StripHTML(const CString& sFrom)
{
	CString sResult = sFrom;

	CString::size_type pos = sResult.find('<');

	while(pos != CString::npos)
	{
		CString::size_type endPos = sResult.find('>', pos);

		if(endPos != CString::npos)
		{
			sResult.erase(pos, endPos - pos + 1);
			pos = sResult.find('<', pos);
		}
		else
		{
			sResult.erase(pos);
			break;
		}
	}
	
	sResult.Trim();

	return sResult.Escape_n(CString::EHTML, CString::EASCII);
}


class CSimpleHTTPSock : protected CSocket
{
private:
	CString m_request;
	CString m_buffer;
	
protected:
	CInfoBotModule *m_pMod;
	
	void Get(const CString& sHost, const CString& sPath, unsigned short iPort = 80, bool bSSL = false)
	{
		m_request = "GET " + sPath + " HTTP/1.0\r\n";
		m_request += "Host: " + sHost + "\r\n";
		m_request += "User-Agent: Mozilla/5.0 (" + CZNC::GetTag() + ")\r\n";
		m_request += "Connection: close\r\n";
		m_request += "\r\n";
		
		Connect(sHost, iPort, bSSL);
	}
	
	void Connected()
	{
		m_buffer.clear();
		Write(m_request);
		m_request.clear();
	}
	
	virtual void Timeout()
	{
		m_request.clear();
		Close();
	}
	
	void Disconnected()
	{
		OnRequestDone(m_buffer);
		Close();
	}
	
	void ReadData(const char *data, int len)
	{
		if(m_buffer.size() + len > 1024 * 1024)
		{
			// make sure our buffers don't EVER take up too much memory.
			// we just abort stuff in this case.
			m_buffer.clear();
			Close();
		}
		else
		{
			m_buffer.append(data, len);
		}
	}
	
	virtual void OnRequestDone(const CString& sResponse) = 0;
public:
	CSimpleHTTPSock(CInfoBotModule *pModInstance)
		: CSocket((CModule*)pModInstance)
	{
		m_pMod = pModInstance;
		
		DisableReadLine();
	}
	
	virtual ~CSimpleHTTPSock()
	{
	}
	
	static CString URLEscape(const CString& s)
	{
		return s.Escape_n(CString::EASCII, CString::EURL);
	}
};


class CTriggerHTTPSock : public CSimpleHTTPSock
{
protected:
	CString m_trigger, m_args, m_chan, m_nick;
	bool m_acceptEmptyArgs;
	
	void Timeout()
	{
		m_pMod->SendMessage(m_chan, "ERROR: Sorry " + m_nick + ", I failed to contact the server.");
		
		CSimpleHTTPSock::Timeout();
	}
	
public:
	CTriggerHTTPSock(CInfoBotModule *pModInstance) : CSimpleHTTPSock(pModInstance)
	{
		m_acceptEmptyArgs = true;
	}
	
	void SetTriggerInfo(const CString& sTrigger, const CString& sArgs, const CString& sChan, const CString& sNick)
	{
		m_trigger = sTrigger;
		m_args = sArgs;
		m_chan = sChan;
		m_nick = sNick;
	}
	
	bool AcceptEmptyArgs() { return m_acceptEmptyArgs; }
	
	virtual void Request() = 0;
};


class CGoogleSock : public CTriggerHTTPSock
{
protected:
	CString ParseFirstResult(const CString& sResponse, bool bGetURLOnly = false)
	{
		RE linkRE(">Search Results<.+?class=.?r.+?href=[\"'](http://\\S+?)[\"'].*?>(.+?)</a", RE_Options(PCRE_CASELESS));
		string sURL_TMP, sTitle_TMP;
		
		if(linkRE.PartialMatch(sResponse.c_str(), &sURL_TMP, &sTitle_TMP))
		{
			CString sURL = StripHTML(sURL_TMP), sTitle = StripHTML(sTitle_TMP);
			
			return (bGetURLOnly ? sURL : sURL + " " + sTitle);
		}
		
		return "ERROR";
	}
	
public:
	CGoogleSock(CInfoBotModule *pModInstance) : CTriggerHTTPSock(pModInstance)
	{
		m_acceptEmptyArgs = false;
	}
	
	void Request()
	{
		Get("www.google.com", "/search?q=" + URLEscape(m_args));
	}
	
	void OnRequestDone(const CString& sResponse)
	{
		CString sFirstLink = ParseFirstResult(sResponse);
		
		if(true)
		{
			m_pMod->SendMessage(m_chan, "%CL1%[%CL2%Google%CL1%]%CLO% " + sFirstLink.Token(0) + " %CL1%[%CL2%" + sFirstLink.Token(1, true) + "%CL1%]");
		}
	}
};


class CImdbComSock : public CTriggerHTTPSock
{
protected:
	CString m_imdbID;
	CString m_title, m_tagline, m_director;
	VCString m_genres;
	int m_year, m_runtime, m_votes;
	float m_rating;
	
	bool ParseResponse(const CString& sResponse)
	{
		/* reset stuff */
		m_year = m_runtime = m_votes = 0;
		m_rating = 0.0;
		m_title.clear(); m_tagline.clear(); m_director.clear();
		m_genres.clear();
		
		/* extract title */
		string sTitleWithYear, sTitle;
		RE titleRE("<h1>(.+?)(<span\\s+class=\"pro-link\".+?)?</h1>", RE_Options(PCRE_CASELESS));
		
		if(titleRE.PartialMatch(sResponse.c_str(), &sTitleWithYear))
		{
			RE titleYearRE("(\\(((?:19|20)\\d{2}(?:/\\w+|))\\).*?)$");
			string sTmpYear, sTmpTitle;
			
			sTitleWithYear = StripHTML(sTitleWithYear);
			
			if(titleYearRE.PartialMatch(sTitleWithYear.c_str(), &sTmpTitle, &sTmpYear))
			{
				sTitle = sTitleWithYear.substr(0, sTitleWithYear.size() - sTmpTitle.size());
				
				m_year = atoi(sTmpYear.c_str());
			}
			else
			{
				sTitle = sTitleWithYear;
			}
			
			m_title = sTitle;
			m_title.Trim();
		}
		else
		{
			// if even extracting the title failed, we give up.
			// we probably got an error page or IMDB completely
			// changed the page structure.
			return false;
		}
		
		/* extract rating */
		string sTmpRating;
		RE ratingRE("User Rating.{1,400}(\\d{1,2}(?:\\.\\d{1,2}))\\s*/\\s*10", RE_Options(PCRE_CASELESS | PCRE_DOTALL));
		
		if(ratingRE.PartialMatch(sResponse.c_str(), &sTmpRating))
		{
			m_rating = (float)CString(sTmpRating).ToDouble();
			
			/* extract no of votes */
			string sTmpVotes;
			RE votesRE("<a[^>]*?href=\"ratings\"[^>]*?>([0-9.,]+)\\s*votes", RE_Options(PCRE_CASELESS));
			
			if(votesRE.PartialMatch(sResponse.c_str(), &sTmpVotes))
			{
				CString sVotesFixed;
				
				for(size_t p = 0; p < sTmpVotes.size(); p++)
				{
					if(isdigit(sTmpVotes[p]))
					{
						sVotesFixed += sTmpVotes[p];
					}
				}
				
				m_votes = sVotesFixed.ToInt();
			}
		}
		else
		{
			m_votes = 0;
		}
		
		/* extract director */
		string sTmpDirector;
		RE directorRE("<h5>Directors?:?(.+?)<br", RE_Options(PCRE_CASELESS | PCRE_DOTALL));
		
		if(directorRE.PartialMatch(sResponse.c_str(), &sTmpDirector))
		{
			m_director = StripHTML(sTmpDirector);
		}
		
		/* extract tagline */
		string sTmpTagline;
		RE taglineRE("<h5>Tag\\s*line:?(.+?)(<a|</div)", RE_Options(PCRE_CASELESS | PCRE_DOTALL));
		
		if(taglineRE.PartialMatch(sResponse.c_str(), &sTmpTagline))
		{
			m_tagline = StripHTML(sTmpTagline);
		}
		
		/* extract genres */
		string sTmpGenreHTML;
		RE genreOuterRE("<h5>Genre:?(.+?)</div", RE_Options(PCRE_CASELESS | PCRE_DOTALL));
		
		if(genreOuterRE.PartialMatch(sResponse.c_str(), &sTmpGenreHTML))
		{
			StringPiece input(sTmpGenreHTML);
			string sTmpGenre;
			RE genreRE("<a href=\"/Sections/Genres/([^\"]+?)/\">", RE_Options(PCRE_CASELESS));
			
			while(genreRE.FindAndConsume(&input, &sTmpGenre))
			{
				m_genres.push_back(StripHTML(sTmpGenre));
			}
		}
		
		/* extract runtime */
		string sTmpRuntime;
		RE runtimeRE("<h5>Runtime:?(.+?)</div", RE_Options(PCRE_CASELESS | PCRE_DOTALL));
		
		if(runtimeRE.PartialMatch(sResponse.c_str(), &sTmpRuntime))
		{
			sTmpRuntime = StripHTML(sTmpRuntime);
			string sRuntime;
			RE runtimeCheckRE("(\\d+)\\s*min", RE_Options(PCRE_CASELESS));
			
			if(runtimeCheckRE.PartialMatch(sTmpRuntime, &sRuntime))
			{
				m_runtime = atoi(sRuntime.c_str());
			}
		}
		
		return true;
	}
	
	void FormatAndSendInfo()
	{
		const CString sPrefix = "%CL1%[%CL2%iMDB%CL1%]%CLO% ";
		CString sLine;
		
		sLine = m_title;
		if(m_year > 0) sLine += " (" + CString(m_year) + ")";
		if(!m_tagline.empty()) sLine += " - " + m_tagline;
		
		m_pMod->SendMessage(m_chan, sPrefix + sLine);
		m_pMod->SendMessage(m_chan, sPrefix + " http://www.imdb.com/title/" + m_imdbID);
		
		sLine = "";
		if(!m_director.empty()) sLine = "Director: " + m_director + " - ";
		sLine += "Rating: " + (m_rating > 0 ? CString(m_rating, 1) : CString("??")) + "/10 with " + (m_votes > 0 ? CString(m_votes) : CString("awaiting five")) + " votes";
		
		m_pMod->SendMessage(m_chan, sPrefix + sLine);
		
		if(m_runtime > 0 || !m_genres.empty())
		{
			sLine = (m_runtime > 0 ? "Runtime: " + CString(m_runtime) + " minutes" + (m_genres.empty() ? CString("") : CString(" - ")) : CString(""));
			if(!m_genres.empty()) sLine += "Genre: ";
			
			for(VCString::const_iterator it = m_genres.begin(); it != m_genres.end(); it++)
			{
				sLine += (*it) + ((it + 1) != m_genres.end() ? " / " : "");
			}
			
			m_pMod->SendMessage(m_chan, sPrefix + sLine);
		}
	}
	
public:
	CImdbComSock(CInfoBotModule *pModInstance, const CString& sImdbID) : CTriggerHTTPSock(pModInstance), m_imdbID(sImdbID) {}
	
	void Request()
	{
		Get("www.imdb.com", "/title/" + m_imdbID + "/");
	}
	
	void OnRequestDone(const CString& sResponse)
	{
		if(ParseResponse(sResponse))
		{
			FormatAndSendInfo();
		}
		else
		{
			m_pMod->SendMessage(m_chan, "%CL1%[%CL2%ERROR%CL2%]%CLO% Getting movie info from imdb.com failed, sorry.");
		}
	}
};


class CImdbGoogleSock : public CGoogleSock
{
public:
	CImdbGoogleSock(CInfoBotModule *pModInstance) : CGoogleSock(pModInstance) {}
	
	void Request()
	{
		Get("www.google.com", "/search?q=" + URLEscape(m_args + " imdb inurl:title"));
	}
	
	void OnRequestDone(const CString& sResponse)
	{
		const CString sURL = ParseFirstResult(sResponse, true);
		string imdbID;
		
		RE URLCheckRE("^https?://(?:[\\w+.]+|)imdb\\.\\w+/title/(tt\\d{4,})", RE_Options(PCRE_CASELESS));
		
		if(URLCheckRE.PartialMatch(sURL.c_str(), &imdbID))
		{
			CImdbComSock *imdbSock = new CImdbComSock(m_pMod, imdbID);
			imdbSock->SetTriggerInfo(m_trigger, m_args, m_chan, m_nick);
			imdbSock->Request();
		}
		else
		{
			m_pMod->SendMessage(m_chan, "%CL1%[%CL2%ERROR%CL2%]%CLO% Movie not found, sorry.");
		}
	}
};


/***** CInfoBotModule implementation below *****/


bool CInfoBotModule::IsTriggerSupported(const CString& sTriggerName)
{
	for(int i = 0; i < NR_OF_TRIGGERS; i++)
	{
		if(sTriggerName.Equals(TRIGGERS[i]))
			return true;
	}
	return false;
}

bool CInfoBotModule::IsTriggerEnabled(const CString& sChan, const CString& sTriggerName)
{
	const VCString& vEnabled = m_enabledCmds[sChan];
	
	for(VCString::const_iterator it = vEnabled.begin(); it != vEnabled.end(); it++)
	{
		if(sTriggerName.Equals(*it))
			return true;
	}
	
	return false;
}

void CInfoBotModule::DisableTrigger(const CString& sChan, const CString& sTriggerName)
{
	VCString& vEnabled = m_enabledCmds[sChan];
	
	for(VCString::iterator it = vEnabled.begin(); it != vEnabled.end(); it++)
	{
		if(sTriggerName.Equals(*it))
		{
			vEnabled.erase(it);
			break;
		}
	}
	
	if(vEnabled.empty())
	{
		m_enabledCmds.erase(sChan);
	}
}

void CInfoBotModule::CheckLineForTrigger(const CString& sMessage, const CString& sChannel, const CString& sNick)
{
	if(sMessage.empty() || sMessage[0] != TriggerChar(sChannel))
		return;
	
	CString sTrigger = sMessage.Token(0).AsLower();
	sTrigger.erase(0, 1);
	const CString sArgs = sMessage.Token(1, true);
	
	if(!IsTriggerSupported(sTrigger) || !IsTriggerEnabled(sChannel.AsLower(), sTrigger))
		return;
	
	CTriggerHTTPSock *pTriggerSock = NULL;
	
	if(sTrigger == "google")
	{
		pTriggerSock = new CGoogleSock(this);
	}
	else if(sTrigger == "imdb")
	{
		pTriggerSock = new CImdbGoogleSock(this);
	}
	else if(sTrigger == "time")
	{
		char buf[100] = {0};
		time_t rawtime;
		tm *timeinfo;
		
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		
		if(timeinfo)
		{
			strftime(buf, 100, "%A, %B %d %Y, %H:%M:%S %Z", timeinfo);
			
			SendMessage(sChannel, "Hey " + sNick + ", it's " + buf + "!");
		}
	}
	else if(sTrigger == "uptime")
	{
		SendMessage(sChannel, "This ZNC has been running for " + CZNC::Get().GetUptime());
	}
	else if(sTrigger == "8ball")
	{
		if(!sArgs.empty())
		{
			SendMessage(sChannel, Do8Ball());
		}
	}
	
	if(pTriggerSock != NULL)
	{
		if(!sArgs.empty() || pTriggerSock->AcceptEmptyArgs())
		{
			pTriggerSock->SetTriggerInfo(sTrigger, sArgs, sChannel, sNick);
		}
		
		pTriggerSock->Request();
	}
}

bool CInfoBotModule::ColorsEnabled(const CString& sChan)
{
	const CString sChanLower = sChan.AsLower();
	
	// colors enabled by default:
	return (m_coloredChans.find(sChanLower) == m_coloredChans.end() || m_coloredChans[sChanLower]);
}

char CInfoBotModule::TriggerChar(const CString& sChan)
{
	const CString sChanLower = sChan.AsLower();
	
	// default is !
	return (m_triggerChars.find(sChanLower) != m_triggerChars.end() ? m_triggerChars[sChanLower] : '!');
}

void CInfoBotModule::SendMessage(const CString& sSendTo, const CString& sMsg)
{
	CString sText = sMsg;
	
	if(ColorsEnabled(sSendTo))
	{
		sText.Replace("%CL1%", "\x03" + CString(m_colorOne));
		sText.Replace("%CL2%", "\x03" + CString(m_colorTwo));
		sText.Replace("%CLO%", "\x03");
		
		// forcefully reset colors before applying our own by
		// prepending the msg with \x03\x03.
		
		sText = "\x03" + sText;
		sText.Replace("\x03\x03", "\x03");
		
		sText = "\x03" + sText;
	}
	else
	{
		sText.Replace("%CL1%", "");
		sText.Replace("%CL2%", "");
		sText.Replace("%CLO%", "");
	}
	
	m_pUser->PutIRC("PRIVMSG " + sSendTo + " :" + sText);
	m_pUser->PutUser(":" + m_pUser->GetIRCNick().GetNickMask() + " PRIVMSG " + sSendTo + " :" + sText);
}

void CInfoBotModule::OnModCommand(const CString& sCommand)
{
	const CString sCmd = sCommand.Token(0).AsUpper();
	
	if(sCmd == "ENABLE" || sCmd == "DISABLE")
	{
		bool bEnable = (sCmd == "ENABLE");
		const CString sChan = sCommand.Token(1).AsLower();
		const CString sTrigger = sCommand.Token(2).AsLower();
		
		if(sChan.empty() || !IsTriggerSupported(sTrigger))
		{
			PutModule("Syntax: " + sCmd + " #chan (" + CString(TRIGGERS_STR) + ")");
		}
		else if(!IsTriggerEnabled(sChan, sTrigger))
		{
			if(bEnable)
			{
				m_enabledCmds[sChan].push_back(sTrigger);
				PutModule("Enabled !" + sTrigger + " on " + sChan + "!");
			}
			else
			{
				PutModule("!" + sTrigger + " is not enabled on " + sChan + "!");
			}
		}
		else
		{
			if(bEnable)
			{
				PutModule("!" + sTrigger + " is already enabled on " + sChan + "!");
			}
			else
			{
				DisableTrigger(sChan, sTrigger);
				PutModule("Disabled !" + sTrigger + " on " + sChan + "!");
			}
		}
		
		SaveSettings();
	}
	else if(sCmd == "LIST" || sCmd == "SHOW" || sCmd == "CHANS" || sCmd == "CHANNELS")
	{
		CTable ChanTable;
		
		if(m_enabledCmds.empty())
		{
			PutModule("No channels set up yet! Use the ENABLE command.");
			return;
		}
		
		ChanTable.AddColumn("Channel");
		ChanTable.AddColumn("Triggers");
		ChanTable.AddColumn("Colors");
		
		for(TEnabledChanMap::const_iterator itc = m_enabledCmds.begin(); itc != m_enabledCmds.end(); itc++)
		{
			CString sTriggers;
			
			ChanTable.AddRow();
			ChanTable.SetCell("Channel", itc->first);
			
			for(VCString::const_iterator itt = itc->second.begin(); itt != itc->second.end(); itt++)
			{
				sTriggers += TriggerChar(itc->first) + *itt + " ";
			}
			
			ChanTable.SetCell("Triggers", sTriggers);
			ChanTable.SetCell("Colors", CString(ColorsEnabled(itc->first)));
		}
		
		PutModule(ChanTable);
		
		PutModule("\x03\x03""Colors: \x03" + CString(m_colorOne) + "[\x03" +  CString(m_colorTwo) + "LikeThis\x03" + CString(m_colorOne) + "]\x03 Example.");
	}
	else if(sCmd == "COLOR" || sCmd == "COLORS" || sCmd == "COLOUR" || sCmd == "COLOURS")
	{
		const CString sAction = sCommand.Token(1).AsLower();
		const CString sParam = sCommand.Token(2).AsLower();
		
		if((sAction == "one" || sAction == "two") && !sParam.empty())
		{
			int iColor = sParam.ToInt();
			
			if(iColor >= 0 && iColor <= 15)
			{
				if(sAction == "one")
					m_colorOne = iColor;
				else
					m_colorTwo = iColor;
				
				PutModule("Changed color " + sAction + " to \x03" + (iColor < 10 ? "0" + CString(iColor) : CString(iColor)) + CString(iColor) + "\x03.");
			}
			else
			{
				PutModule("Invalid color. Needs to be 0 to 15.");
			}
		}
		else if(sParam == "on" || sParam == "off" || sParam == "yes" || sParam == "no" || sParam == "true" || sParam == "false")
		{
			if(sParam == "on" || sParam == "yes" || sParam == "true")
			{
				m_coloredChans.erase(sAction);
				PutModule("Activated color on " + sAction + ".");
			}
			else
			{
				m_coloredChans[sAction] = false;
				PutModule("Deactivated color on " + sAction + ".");
			}
		}
		else
		{
			PutModule("Syntax: COLOR (ONE|TWO) (0-15) / COLOR #chan (ON|OFF)");
		}
	}
	else if(sCmd == "TRIGGERCHAR")
	{
		const CString sChan = sCommand.Token(1).AsLower();
		const CString sParam = sCommand.Token(2);
		
		if(!sChan.empty() && !sParam.empty())
		{
			m_triggerChars[sChan] = sParam[0];
			PutModule("Set trigger char on " + sChan + " to '" + CString(sParam[0]) + "'.");
		}
		else
		{
			PutModule("Syntax: TRIGGERCHAR #chan C (where C is the char, like ! or .)");
		}
	}
	else if(sCmd == "HELP")
	{
		CTable TriggerTable;
	}
	else
	{
		PutModule("Unknown command!");
	}
}

bool CInfoBotModule::OnLoad(const CString& sArgs, CString& sMessage)
{
	LoadSettings();
	return true;
}

void CInfoBotModule::SaveSettings()
{
	ClearNV();
	
	for(TEnabledChanMap::const_iterator itc = m_enabledCmds.begin(); itc != m_enabledCmds.end(); itc++)
	{
		CString sName = "chan:" + itc->first;
		CString sData;
		
		for(VCString::const_iterator itt = itc->second.begin(); itt != itc->second.end(); itt++)
		{
			sData += "enable=" + *itt + "\n";
		}
		
		if(TriggerChar(itc->first) != '!') sData += "trigger=" + CString(TriggerChar(itc->first)) + "\n";
		sData += "colors=" + CString(ColorsEnabled(itc->first) ? 1 : 0);
		
		SetNV(sName, sData, false);
	}
	
	SetNV("color:1", CString(m_colorOne), false);
	SetNV("color:2", CString(m_colorTwo), true); // last change writes to disk!
}

void CInfoBotModule::LoadSettings()
{
	for(MCString::iterator it = BeginNV(); it != EndNV(); it++)
	{
		if(it->first.Left(5) == "chan:")
		{
			const CString sChanName = it->first.substr(5);
			pcrecpp::StringPiece input(it->second);
			
			string left, right;
			pcrecpp::RE re("(\\w+) ?= ?(.+?)\n");
			while(re.Consume(&input, &left, &right))
			{
				if(left == "enable")
				{
					m_enabledCmds[sChanName].push_back(right);
				}
				else if(left == "colors")
				{
					m_coloredChans[sChanName] = (right != "0");
				}
			}
		}
		else if(it->first == "color:1")
		{
			m_colorOne = atoi(it->second.c_str());
		}
		else if(it->first == "color:2")
		{
			m_colorTwo = atoi(it->second.c_str());
		}
	}
}

CInfoBotModule::EModRet CInfoBotModule::OnChanMsg(CNick& Nick, CChan& Channel, CString& sMessage)
{
	CheckLineForTrigger(sMessage, Channel.GetName(), Nick.GetNick());
	return CONTINUE;
}

CInfoBotModule::EModRet CInfoBotModule::OnUserMsg(CString& sTarget, CString& sMessage)
{
	if(!sTarget.empty() && !isalnum(sTarget[0]))
	{
		CheckLineForTrigger(sMessage, sTarget, m_pUser->GetIRCNick().GetNick());
	}
	return CONTINUE;
}

CInfoBotModule::~CInfoBotModule()
{
}

CString CInfoBotModule::Do8Ball()
{
	static const char *answers[21] = {
		"Are you mad?!",
		"As I see it: yes",
		"Ask again later",
		"Better not tell you now",
		"Cannot predict now",
		"Concentrate and ask again",
		"Don't count on it",
		"It is certain",
		"It is decidedly so",
		"Most likely",
		"My reply is no",
		"My sources say no",
		"No",
		"Outlook good",
		"Outlook not so good",
		"Signs point to yes",
		"Very doubtful",
		"Without a doubt",
		"Yes - definitely",
		"Yes",
		"You may rely on it",
	};
	
	return answers[rand() % 21];
}

const char* CInfoBotModule::TRIGGERS[NR_OF_TRIGGERS] = {
	"google",
	"imdb",
	"time",
	"uptime",
	"8ball"
};
const char* CInfoBotModule::TRIGGER_DESCRIPTIONS[NR_OF_TRIGGERS] = {
	"Does a Google search.",
	"Does a search on IMDB.",
	"Shows the current time.",
	"Shows how long ZNC has been running.",
	"Ask the magic 8ball anything!"
};
const char* CInfoBotModule::TRIGGERS_STR = "google|imdb|time|uptime|8ball";


MODULEDEFS(CInfoBotModule, "Provides commands like !google, !imdb and !8ball to selected channels")
