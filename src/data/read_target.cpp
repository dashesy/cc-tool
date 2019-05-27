/*
 * read_traget.cpp
 *
 * Created on: Oct 31, 2011
 *     Author: George Stark <george-u@yandex.com>
 *
 * License: GNU GPL v2
 *
 */

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "data/data_section_store.h"
#include "data/hex_file.h"
#include "data/binary_file.h"
#include "read_target.h"
#include "common/common.h"

//==============================================================================
static String file_extension(const String path)
{
#if BOOST_FILESYSTEM_VERSION >= 3
	return boost::filesystem::path(path).extension().string();
#else
	return boost::filesystem::extension(path);
#endif
}

//==============================================================================
ReadTarget::ReadTarget() :
			source_type_(ST_CONSOLE)
{ }

//==============================================================================
ReadTarget::SourceType ReadTarget::source_type() const
{	return source_type_; }

//==============================================================================
void ReadTarget::on_read(const ByteVector &data) const
{
	if (file_format_ == "hex")
	{
		DataSectionStore store;
		store.add_section(DataSection(0, data), true);
		hex_file_save(file_name_, store);
	}
	if (file_format_ == "bin")
		binary_file_save(file_name_, data);
}


//==============================================================================
void ReadTarget::set_source(const String &input)
{
	if (input.empty())
		source_type_ = ST_CONSOLE;
	else
	{
		OptionFileInfo file_info;
		option_extract_file_info(input, file_info, false);

		source_type_ = ST_FILE;
		file_format_ = file_info.type;
		file_name_ = file_info.name;
	}
}

//==============================================================================
void option_extract_file_info(const String &input, OptionFileInfo &file_info,
		bool support_offset)
{
	StringVector strs;
	boost::split(strs, input, boost::is_any_of(":"));

	if (strs.size() > 3)
		throw std::runtime_error("bad file name format (" + input + ")");

	file_info.offset = 0;
	file_info.name = strs[0];
	if (strs.size() > 1)
		file_info.type = strs[1];
	if (file_info.type.empty())
	{
		file_info.type = file_extension(file_info.name);
		if (file_info.type[0] == '.')
			file_info.type.erase(0, 1);
	}

	if (strs.size() > 2)
	{
		if (!string_to_number(strs[2], file_info.offset))
			throw std::runtime_error("bad offset value (" + input + ")");

		if (!support_offset)
			throw std::runtime_error("offset is not allowed here (" + input + ")");
	}

	String type;

	if (file_info.type.empty() || file_info.type == "bin" || file_info.type == "binary")
		type = "bin";
	if (file_info.type == "hex" || file_info.type == "ihex")
		type = "hex";
	if (type.empty())
		throw std::runtime_error("unknown file type (" + input + ")");
	file_info.type = type;
}
