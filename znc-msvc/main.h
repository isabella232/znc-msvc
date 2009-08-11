/*
 * Copyright (C) 2004-2009  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef _MAIN_H
#define _MAIN_H

// The following defines are for #if comparison (preprocessor only likes ints)
#define VERSION_MAJOR	0
#define VERSION_MINOR	75
// This one is for display purpose
#define VERSION		(VERSION_MAJOR + VERSION_MINOR / 1000.0)

#ifdef _WIN32
#include "revision.h" // generated by post update hook
#define MODVERSION REVISION
#else
#define MODVERSION VERSION
#endif

// You can add -DVERSION_EXTRA="stuff" to your CXXFLAGS!
#ifndef VERSION_EXTRA
# ifdef _WIN32
#  define VERSION_EXTRA "-r" REVISION_STR "-Win32"
# else
#  define VERSION_EXTRA ""
# endif
#endif

#ifndef _MODDIR_
#define _MODDIR_ "/usr/lib/znc"
#endif

#ifndef _DATADIR_
#define _DATADIR_ "/usr/share/znc"
#endif

#ifdef _MODULES
#define MODULECALL(macFUNC, macUSER, macCLIENT, macEXITER)	\
	if (macUSER) {											\
		CGlobalModules& GMods = CZNC::Get().GetModules();	\
		CModules& UMods = macUSER->GetModules();			\
		CUser* pOldGUser = GMods.GetUser();				\
		CClient* pOldGClient = GMods.GetClient();			\
		CClient* pOldUClient = UMods.GetClient();			\
		GMods.SetUser(macUSER);						\
		GMods.SetClient(macCLIENT);	\
		UMods.SetClient(macCLIENT);							\
		if (GMods.macFUNC || UMods.macFUNC) {				\
			GMods.SetUser(pOldGUser);				\
			GMods.SetClient(pOldGClient);		\
			UMods.SetClient(pOldUClient);							\
			macEXITER;										\
		}													\
		GMods.SetUser(pOldGUser);					\
		GMods.SetClient(pOldGClient);			\
		UMods.SetClient(pOldUClient);								\
	}
# ifndef _WIN32
#  define MODULE_FILE_EXT ".so"
#  define MODULE_FILE_EXT_LEN 3
 #else
#  define MODULE_FILE_EXT ".dll"
#  define MODULE_FILE_EXT_LEN 4
# endif
#else
#define MODULECALL(macFUNC, macUSER, macCLIENT, macEXITER)
#endif

#ifdef _WIN32
#define WINVER		0x0500
#define _WIN32_WINNT   0x0500
#define _WIN32_WINDOWS 0x0500
#define _WIN32_IE      0x0600
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef WIN_MSVC
#define VC_EXTRALEAN
#endif

#include "exports.h"

#ifdef WIN_MSVC
#include "znc_msvc.h"
#endif

#include "ZNCString.h"
#include "Utils.h"
#include "Buffer.h"
#include "Chan.h"
#include "Client.h"
#include "Csocket.h"
#include "DCCBounce.h"
#include "DCCSock.h"
#include "FileUtils.h"
#include "HTTPSock.h"
#include "IRCSock.h"
#include "MD5.h"
#include "Modules.h"
#include "Nick.h"
#include "Server.h"
#include "Template.h"
#include "Timers.h"
#include "User.h"
#include "znc.h"

#endif // !_MAIN_H
