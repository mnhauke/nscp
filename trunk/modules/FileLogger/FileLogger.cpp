/**************************************************************************
*   Copyright (C) 2004-2007 by Michael Medin <michael@medin.name>         *
*                                                                         *
*   This code is part of NSClient++ - http://trac.nakednuns.org/nscp      *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
***************************************************************************/

#include "stdafx.h"
#include "FileLogger.h"

#include <sys/timeb.h>
#include <time.h>

FileLogger gFileLogger;

BOOL APIENTRY DllMain( HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	NSCModuleWrapper::wrapDllMain(hModule, ul_reason_for_call);
	return TRUE;
}

FileLogger::FileLogger() {
}
FileLogger::~FileLogger() {
}

std::wstring FileLogger::getFileName()
{
	if (file_.empty()) {
		file_ = NSCModuleHelper::getSettingsString(LOG_SECTION_TITLE, LOG_FILENAME, LOG_FILENAME_DEFAULT);
		if (file_.find(_T("\\")) == std::wstring::npos)
			file_ = NSCModuleHelper::getBasePath() + _T("\\") + file_;
	}
	return file_;
}

bool FileLogger::loadModule() {
	_tzset();
	getFileName();
	format_ = NSCModuleHelper::getSettingsString(LOG_SECTION_TITLE, LOG_DATEMASK, LOG_DATEMASK_DEFAULT);
	return true;
}
bool FileLogger::unloadModule() {
	return true;
}
bool FileLogger::hasCommandHandler() {
	return false;
}
bool FileLogger::hasMessageHandler() {
	return true;
}
void FileLogger::handleMessage(int msgType, TCHAR* file, int line, TCHAR* message) {
	TCHAR buffer[65];
	std::wofstream stream(file_.c_str(), std::ios::app);
	__time64_t ltime;
	_time64( &ltime );
	struct tm *today = _localtime64( &ltime );
	if (today) {
		size_t len = wcsftime(buffer, 63, format_.c_str(), today);
		if ((len < 1)||(len > 64))
			wcsncpy_s(buffer, 64, _T("???"), 63);
		else
			buffer[len] = 0;
	} else {
		wcsncpy_s(buffer, 64, _T("???"), 63);
	}
	stream << buffer << _T(": ") << NSCHelper::translateMessageType(msgType) << _T(":") << file << _T(":") << line << _T(": ") << message << std::endl;
}

NSC_WRAPPERS_MAIN_DEF(gFileLogger);
NSC_WRAPPERS_HANDLE_MSG_DEF(gFileLogger);
NSC_WRAPPERS_IGNORE_CMD_DEF();
NSC_WRAPPERS_HANDLE_CONFIGURATION(gFileLogger);




MODULE_SETTINGS_START(FileLogger, _T("File logger configuration"),_T("..."))
PAGE(_T("Filelogger"))

ITEM_CHECK_BOOL(_T("Log debug messages"), _T("Enable this to log debug messages (when running with /test debuglog is always enabled)"))
ITEM_MAP_TO(_T("basic_ini_text_mapper"))
OPTION(_T("section"), _T("log"))
OPTION(_T("key"), _T("debug"))
OPTION(_T("default"), _T("false"))
OPTION(_T("true_value"), _T("1"))
OPTION(_T("false_value"), _T("0"))
ITEM_END()

ITEM_EDIT_TEXT(_T("Log file"), _T("This is the size of the buffer that stores CPU history."))
OPTION(_T("unit"), _T("(relative to NSClient++ binary"))
ITEM_MAP_TO(_T("basic_ini_text_mapper"))
OPTION(_T("section"), _T("log"))
OPTION(_T("key"), _T("file"))
OPTION(_T("default"), _T("NSC.log"))
ITEM_END()

ITEM_EDIT_TEXT(_T("Date mask"), _T("The date/timeformat in the log file."))
ITEM_MAP_TO(_T("basic_ini_text_mapper"))
OPTION(_T("section"), _T("log"))
OPTION(_T("key"), _T("date_mask"))
OPTION(_T("default"), _T("%Y-%m-%d %H:%M:%S"))
ITEM_END()

PAGE_END()
MODULE_SETTINGS_END()