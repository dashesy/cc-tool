/*
 * log.cpp
 *
 * Created on: Nov 9, 2011
 *     Author: George Stark <george-u@yandex.com>
 *
 * License: GNU GPL v2
 *
 */

#include "log.h"
#include <stdarg.h>

//==============================================================================
Log::Log() :
	file_(NULL)
{ }

//==============================================================================
Log::~Log()
{
	if (file_)
		fclose(file_);
}

//==============================================================================
void Log::set_log_file(const String &file_name)
{
	if (file_)
		fclose(file_);
	file_ = fopen(file_name.c_str(), "w");
}

//==============================================================================
void Log::add(LogLevel level, const String &message, va_list ap)
{
	time_t sys_time;
	tm local_time;
	time(&sys_time);
	localtime_r(&sys_time, &local_time);

	char time_stamp[64] = { '\0' };
	strftime(time_stamp, 64, "%d.%m %H:%M:%S:", &local_time);
	sprintf(time_stamp + strlen(time_stamp), "%05d", get_tick_count() % 100000);

	if (file_)
	{
		fprintf(file_, "[%s] ", time_stamp);
		vfprintf(file_, message.c_str(), ap);
		fprintf(file_, "\n");
		fflush(file_);
	}
}

//==============================================================================
void log_info(String message, ...)
{
	va_list ap;
	va_start(ap, message);
	log_get().add(Log::LL_INFO, message, ap);
	va_end(ap);
}

//==============================================================================
Log &log_get()
{
	static Log log;
	return log;
}
