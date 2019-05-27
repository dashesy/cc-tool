/*
 * cc_base.cpp
 *
 *  Created on: Nov 30, 2011
 *      Author: st
 */

#include <boost/regex.hpp>
#include "common.h"
#include "version.h"
#include "log.h"
#include "timer.h"
#include "programmer/cc_programmer.h"
#include "cc_base.h"

//==============================================================================
static bool extract_usb_address(const String &str, uint_t &bus, uint_t &device)
{
	boost::cmatch what;
	boost::regex hex_regex("0x([\\da-fA-F]{1,4}):0x([\\da-fA-F]{1,4})");

	if (boost::regex_match(str.c_str(), what, hex_regex))
	{
		hexstring_to_number(what[1].str(), bus);
		hexstring_to_number(what[2].str(), device);
		return true;
	}

	boost::regex regex("([\\d]{1,3}):([\\d]{1,3})");

	if (!boost::regex_match(str.c_str(), what, regex))
		return false;

	string_to_number(what[1].str(), bus);
	string_to_number(what[2].str(), device);
	return true;
}

//==============================================================================
static std::ostream& operator <<(std::ostream &os, const USB_DeviceID &o)
{
	os << std::hex << std::setfill('0') << std::nouppercase;

	os << "  VID: 0x" << std::setw(4) << o.vendor_id;
	os << " PID: 0x" << std::setw(4) << o.product_id;
	os << " Description: " << o.description;

	return os;
}

//==============================================================================
static void init_log(int argc, char *argv[], String &log_file)
{
	if (log_file.empty())
		log_file = String(MODULE_NAME) + ".log";

	log_get().set_log_file(log_file);
	log_info("main, %s %u.%u", MODULE_NAME, VERSION_MAJOR, VERSION_MINOR);

	String command_line;
	while (argc--)
		command_line += String(*argv++) + " ";
	log_info("main, command line: %s", command_line.c_str());
}

//==============================================================================
static void print_usage(const po::options_description &desc)
{
	std::cout << MODULE_DESCRIPTION << "\n";
	std::cout << " Version: "  << VERSION_MAJOR << "." << VERSION_MINOR << "\n";
	std::cout << "\n Command line options:\n";
	std::cout << desc;
}

//==============================================================================
void CC_Base::init_options(po::options_description &desc)
{
	desc.add_options()
		("help,h", "produce help message");

	desc.add_options()
		("log", po::value<String>(&option_log_name_)->implicit_value(""),
				"create log of all operations");

	desc.add_options()
		("device,d", po::value<String>(&option_device_address_),
				"set programmer device usb address 'bus:device'");

	desc.add_options()
		("fast,f", "set fast debug interface speed (by default: slow)");

	desc.add_options()
		("name,n", po::value<String>(&option_unit_name_),
				"specify target name e.g. CC2530 etc.");

	desc.add_options()
		("allow-overlapping-records",
				"allow overlapping records in hex files, the last one wins (by default: they are disallowed)");
}

//==============================================================================
void CC_Base::on_help(const po::options_description &desc)
{
	print_usage(desc);

	std::cout << "\n Supported targets:\n   ";
	StringVector targets = programmer_.supported_unit_names();
	foreach (const String &item, targets)
		std::cout  << item << " ";

	std::cout << "\n\n Supported programmers:\n";
	USB_DeviceIDVector devices = programmer_.supported_devices();
	foreach (const USB_DeviceID &item, devices)
		std::cout  << item << "\n";
}

//==============================================================================
bool CC_Base::read_options(const po::options_description &desc, const po::variables_map &vm)
{
	if (vm.count("help"))
	{
		on_help(desc);
		return false;
	}

	if (vm.count("device"))
	{
		uint_t bus = 0, device = 0;
		if (!extract_usb_address(option_device_address_, bus, device))
			throw po::error("Bad device address format");
	}

	option_fast_interface_speed_ = vm.count("fast") > 0;
	option_allow_overlapping_records_ = vm.count("allow-overlapping-records") > 0;
	return true;
}

//==============================================================================
bool CC_Base::init_unit()
{
	String unit_name;
	bool supported = false;
	programmer_.unit_status(unit_name, supported);

	if (unit_name.empty())
	{
		std::cout << "  No target detected" << "\n";
		return false;
	}
	std::cout << "  Target: " << unit_name << "\n";

	if (!supported)
	{
		std::cout << "  Target not supported" << "\n";
		log_info("main, target not supported");
		return false;
	}

	boost::to_upper(option_unit_name_);
	if (unit_name != option_unit_name_ && !option_unit_name_.empty())
	{
		std::cout << "Specified target " << option_unit_name_ << " not found" << "\n";
		return false;
	}

	if (!programmer_.unit_connect(unit_info_))
	{
		std::cout << "  Unable to communicate with target" << "\n";
		return false;
	}
	return true;
}

//==============================================================================
bool CC_Base::init_programmer()
{
	CC_Programmer::OpenResult open_result = CC_Programmer::OR_OK;

	if (!option_device_address_.empty())
	{
		uint_t bus = 0, device = 0;
		extract_usb_address(option_device_address_, bus, device);
		open_result = programmer_.open(bus, device);
		if (open_result == CC_Programmer::OR_NOT_SUPPORTED)
			std::cout << std::setfill('0') << "  Device at "
						<< std::setw(3) << bus << ":"
						<< std::setw(3) << device
						<< " not supported" << "\n";
	}
	else
		open_result = programmer_.open();

	if (open_result != CC_Programmer::OR_OK)
	{
		std::cout << "  CC Debugger device not found" << "\n";
		return false;
	}

	programmer_.set_debug_interface_speed(option_fast_interface_speed_?
			CC_Programmer::IS_FAST : CC_Programmer::IS_SLOW);

	CC_ProgrammerInfo info;
	programmer_.programmer_info(info);
	std::cout << "  Programmer: " << info.name << "\n";

	return true;
}

//==============================================================================
bool CC_Base::execute(int argc, char *argv[])
{
	po::options_description desc;
	init_options(desc);

	try
	{
		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("log"))
			init_log(argc, argv, option_log_name_);

		if (!read_options(desc, vm))
			return false;

		if (init_programmer() && init_unit())
		{
			log_info("main, start task processing");
			process_tasks();
			log_info("main, finish task processing");
			programmer_.unit_close();
			return true;
		}
	}
	catch (std::runtime_error& e) // usb, file error
	{
		std::cout << "  Error occurred";
		if (strlen(e.what()))
		{
			std::cout << ": " << e.what();
			log_info("%s", e.what());
		}
		std::cout << "\n";
	}
	catch (po::error& e) // command line error
	{
		std::cout << "  Bad command line options";
		if (strlen(e.what()))
			std::cout << " (" << e.what() << ")";
		std::cout << "\n  Try --help for more information\n";
	}
	return false;
}

//==============================================================================
void CC_Base::process_tasks()
{ }

//==============================================================================
CC_Base::CC_Base() :
		option_allow_overlapping_records_(false),
		option_fast_interface_speed_(false)
{ }
