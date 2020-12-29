/*
 * log.h
 *
 * Created on: Nov 9, 2011
 *     Author: George Stark <george-u@yandex.com>
 *
 * License: GNU GPL v2
 *
 */

#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "common.h"

class Log
{
public:
	enum LogLevel { LL_INFO };

	void add(LogLevel level, const String &message, va_list ap);
	void set_log_file(const String &file_name);

	Log();
	~Log();
private:
	FILE *file_;
};

void log_info(String message, ...);
Log &log_get();

#endif // !_LOG_H_
