/*
 * cc_tool.cpp
 *
 * Created on: Aug 3, 2011
 *     Author: George Stark <george-u@yandex.com>
 *
 * License: GNU GPL v2
 *
 */

#include <boost/regex.hpp>
#include "common.h"
#include "version.h"
#include "log.h"
#include "timer.h"
#include "programmer/cc_programmer.h"
#include "cc_flasher.h"

enum Task {
	T_RESET 		= 0x0001,
	T_ERASE 		= 0x0002,
	T_WRITE_FLASH 	= 0x0004,
	T_READ_FLASH 	= 0x0008,
	T_VERIFY 		= 0x0010,
	T_LOCK 			= 0x0020,
	T_READ_MAC 		= 0x0040,
	T_WRITE_MAC 	= 0x0080,
	T_PRESERVE_MAC 	= 0x0100,
	T_READ_INFO_PAGE= 0x0200,
	T_TEST 			= 0x0400,
};

//==============================================================================
static String mac_address_to_string(const ByteVector &mac)
{
	ByteVector data(mac.size(), 0);
	std::reverse_copy(mac.begin(), mac.end(), data.begin());

	return binary_to_hex(&data[0], data.size(), ":");
}

//==============================================================================
static bool extract_mac_address(const String &mac, size_t length, ByteVector &data)
{
	String exp;
	for (size_t i = 0; i < length; i++)
		exp += "([\\da-fA-F]{2}):";
	exp.erase(exp.end() - 1);

	boost::cmatch what;
	boost::regex regex(exp);

	if (!boost::regex_match(mac.c_str(), what, regex))
		return false;

	for (size_t i = length; i > 0; i--)
		data.push_back(std::strtoul(what[i].str().c_str(), NULL, 16));
	return true;
}

//==============================================================================
static void print_hex_dump(const ByteVector &data)
{
	size_t total_size = data.size();
	size_t offset = 0;

	while (total_size)
	{
		size_t size = std::min(total_size, (size_t)40);
		std::cout << binary_to_hex(&data[offset], size, "") << "\n";
		offset += size;
		total_size -= size;
	}
}

//==============================================================================
static void load_flash_data(const OptionFileInfo &file_info, DataSectionStore &section_store,
		bool allow_overlapping_records)
{
	if (file_info.type == "hex")
	{
		DataSectionStore sections;
		hex_file_load(file_info.name, sections, false,
				allow_overlapping_records);
		section_store.add_sections(sections, true);

		size_t n = 0;
		log_info("main, loaded hex file %s", file_info.name.c_str());
		foreach (const DataSection &item, section_store.sections())
			log_info(" section %02u, address: %06Xh, size: %06Xh",
					n++, item.address, item.size());
	}

	if (file_info.type == "bin")
	{
		ByteVector data;
		binary_file_load(file_info.name, data);

		DataSection section(file_info.offset, data);
		section_store.add_section(section, true);

		log_info("main, loaded bin file %s, size: %u", file_info.name.c_str(),
				data.size());
	}
}

//==============================================================================
static std::ostream& operator <<(std::ostream &os, const CC_ProgrammerInfo &o)
{
	os << "   Name: " << o.name << "\n";
	os << "   Debugger ID: " << o.debugger_id << "\n";
	os << std::hex << std::setfill('0') << std::uppercase;
	os << "   Version: 0x" << std::setw(4) << o.fw_version << "\n";
	os << "   Revision: 0x" << std::setw(4) << o.fw_revision << "\n";
	os << std::dec;

	return os;
}

//==============================================================================
static std::ostream& operator <<(std::ostream &os, const UnitInfo &o)
{
	os << "   Name: " << o.name << "\n";
	os << std::hex << std::setfill('0') << std::uppercase;
	os << "   Revision: 0x" << std::setw(2) << (uint_t)o.revision << "\n";
	os << "   Internal ID: 0x" << std::setw(2) << (uint_t)o.internal_ID << "\n";
	os << "   ID: 0x" << std::setw(4) << o.ID << "\n";
	os << std::dec;
	os << "   Flash size: ";
	if (!o.flash_size)
		os << "n/a" << "\n";
	else
		os << o.flash_size << " KB" << "\n";
	os << "   Flash page size: " << o.flash_page_size << "\n";
	os << "   RAM size: ";
	if (!o.ram_size)
		os << "n/a" << "\n";
	else
		os << o.ram_size << " KB" << "\n";
	os << std::dec;
	return os;
}

//==============================================================================
static void on_progress(size_t done_read, size_t total_read)
{
	std::cout << "  Progress: " << done_read * 100 / total_read
			<< "%\r" << std::flush;
}

//==============================================================================
static void print_result(bool result)
{
	// clear out progress message
	std::cout << String(18, ' ') << "\r" << std::flush;

	if (result)
		std::cout << "  Completed" << "\n";
	else
		std::cout << "  Failed"	<< "\n";
}

//==============================================================================
static void print_result(bool result, const Timer& timer)
{
	// clear out progress message
	std::cout << String(18, ' ') << "\r" << std::flush;

	if (result)
		std::cout << "  Completed (" << timer.elapsed_time() << ")" << "\n";
	else
		std::cout << "  Failed"	<< "\n";
}

//==============================================================================
void CC_Flasher::init_options(po::options_description &desc)
{
	CC_Base::init_options(desc);

	desc.add_options()
		("read-info-page,i", po::value<String>(&option_info_page_)->implicit_value(""),
				"read info pages");

	desc.add_options()
		("read-mac-address,a", "read mac address(es)");

	desc.add_options()
		("write-mac-address,b", po::value<String>(), "write (secondary) mac address");

	desc.add_options()
		("preserve-mac-address,p", "preserve (secondary) mac address across writing");

	desc.add_options()
		("read,r", po::value<String>(), "read flash memory");

	desc.add_options()
		("erase,e", "erase flash memory");

	desc.add_options()
		("write,w", po::value<StringVector>(),
				"write flash memory.");

	desc.add_options()
		("verify,v", po::value<String>()->implicit_value(""),
				"verify flash after write, method '(r)ead' or '(c)cr' (used by default)");

	desc.add_options()
		("reset", "perform target reset");

	desc.add_options()
		("test,t", "search for programmer and target");

	desc.add_options()
		("lock,l", po::value<String>(&option_lock_data_),
				"specify lock data in hex numbers or by string: debug[;pages:xx]");

	desc.add_options()
		("flash-size,s", po::value<String>(&option_flash_size_),
				"specify target flash size in KB");
}

//==============================================================================
bool CC_Flasher::read_options(const po::options_description &desc, const po::variables_map &vm)
{
	if (!CC_Base::read_options(desc, vm))
		return false;

	if (vm.count("read-mac-address"))
		task_set_ |= T_READ_MAC;

	if (vm.count("preserve-mac-address"))
		task_set_ |= T_PRESERVE_MAC;

	if (vm.count("write-mac-address"))
	{
		task_set_ |= T_WRITE_MAC;

		String value = vm["write-mac-address"].as<String>();

		if (!extract_mac_address(value, 6, mac_addr_) &&
				!extract_mac_address(value, 8, mac_addr_))
			throw po::error("bad mac address format - " + value);

		if (task_set_ & T_PRESERVE_MAC)
			throw po::error("incompatible options write-mac-address and preserve-mac-address");
	}

	if (vm.count("read-info-page"))
	{
		task_set_ |= T_READ_INFO_PAGE;
		info_page_read_target_.set_source(option_info_page_);
	}

	if (vm.count("verify"))
	{
		task_set_ |= T_VERIFY;

		String value = vm["verify"].as<String>();
		if (value == "r" || value == "read")
			verify_method_ = CC_Programmer::VM_BY_READ;
		else
		if ((value == "c" || value == "crc"))
			verify_method_ = CC_Programmer::VM_BY_CRC;
		else
		if (!value.empty())
			throw po::error("invalid verify method - " + value);
	}

	if (vm.count("reset"))
		task_set_ |= T_RESET;
	if (vm.count("erase"))
		task_set_ |= T_ERASE;
	if (vm.count("test"))
		task_set_ |= T_TEST;

	if (vm.count("read"))
	{
		task_set_ |= T_READ_FLASH;
		flash_read_target_.set_source(vm["read"].as<String>());
	}

	if (vm.count("write"))
	{
		if (!(task_set_ & T_ERASE))
		{
			std::cout << "  Writing flash is not supported without erase" << "\n";
			return false;
		}

		task_set_ |= T_WRITE_FLASH;
		StringVector list = vm["write"].as<StringVector>();
		foreach (String &item, list)
		{
			OptionFileInfo file_info;
			option_extract_file_info(item, file_info, true);
			load_flash_data(file_info, flash_write_data_,
					option_allow_overlapping_records_);
		}
	}

	if (vm.count("lock"))
		task_set_ |= T_LOCK;

	if ((task_set_ & T_VERIFY) && !(task_set_ & T_WRITE_FLASH))
		throw po::error("'verify' option is used without write");

	if (!option_flash_size_.empty())
	{
		char *error = NULL;
		if (strtoull(option_flash_size_.c_str(), &error, 10) == 0 ||
				*error != '\0')
			throw po::error("invalid flash size value " + option_flash_size_);
	}
	return true;
}

//==============================================================================
bool CC_Flasher::validate_mac_options()
{
	if (!(unit_info_.flags & UnitInfo::SUPPORT_MAC_ADDRESS) &&
		(task_set_ & (T_WRITE_MAC | T_READ_MAC | T_PRESERVE_MAC)))
	{
		std::cout << "  Target does not support MAC address" << "\n";
		return false;
	}

	if (target_locked_ && (task_set_ & T_PRESERVE_MAC))
	{
		std::cout << "  Target is locked. Unable to preserve mac address" << "\n";
		return false;
	}

	if ((task_set_ & T_WRITE_MAC) && mac_addr_.size() != unit_info_.mac_address_size)
	{
		std::cout << "  Wrong MAC address length specified, must be "
				<< unit_info_.mac_address_size << "\n";
		return false;
	}
	if (task_set_ & (T_WRITE_MAC | T_READ_MAC | T_PRESERVE_MAC) &&
			!unit_info_.max_flash_size)
	{
		std::cout << "  Mac address operations are disabled because "
				"target flash size is unavailable." << "\n";
		std::cout << "  See --flash-size option" << "\n";

		return false;
	}
	return true;
}

//==============================================================================
StringVector split_string(const String& input)
{
	StringVector list;

	String s = boost::to_lower_copy(input);
	boost::split(list, s, boost::is_any_of(";"));
	StringVector::iterator it = list.begin();
	while (it != list.end())
	{
		boost::trim(*it);
		if (it->empty())
			it = list.erase(it);
		else
			it++;
	}
	return list;
}

//==============================================================================
bool CC_Flasher::validate_lock_options()
{
	if (!(task_set_ & T_LOCK))
		return true;

	if (!hex_to_binary(option_lock_data_, lock_data_, ""))
	{
		// check if conversion failed due to odd number of hex digits:
		bool hex_data = true;
		foreach(char c, option_lock_data_)
			if (!std::isxdigit(c))
				hex_data = false;
		if (hex_data)
		{
			std::cout << "  Incorrect lock data" << "\n";
			return false;
		}

		try
		{
			programmer_.unit_convert_lock_data(split_string(option_lock_data_), lock_data_);
			std::cout << "  Lock data: " << binary_to_hex(lock_data_, "") << "\n";
		}
		catch (std::exception &e)
		{
			std::cout << "  Error reading lock data: " << e.what() << "\n";
			return false;
		}
	}
	if (lock_data_.size() != programmer_.unit_lock_data_size())
	{
		std::cout << "  Lock data size must be "
				<< programmer_.unit_lock_data_size()
				<< "B, provided: " << lock_data_.size() << "\n";
		return false;
	}
	return true;
}

//==============================================================================
bool CC_Flasher::validate_flash_size_options()
{
	if (option_flash_size_.empty())
		return true;

	if (unit_info_.flash_size)
		std::cout << "  Specified flash size is ignored (read from target instead) " << "\n";
	else
	{
		uint_t flash_size = 0;
		string_to_number(option_flash_size_, flash_size);

		bool found = std::find(
				unit_info_.flash_sizes.begin(),
				unit_info_.flash_sizes.end(),
				flash_size) != unit_info_.flash_sizes.end();

		if (!found)
		{
			String list;
			foreach (uint_t size, unit_info_.flash_sizes)
				string_append(list, number_to_string(size), ", ");

			std::cout << "  Specified flash size is wrong";
			if (list.size())
				std::cout << "; valid values are " << list;
			std::cout << "\n";
			return false;
		}
		programmer_.unit_set_flash_size(flash_size);
	}
	return true;
}

//==============================================================================
void CC_Flasher::task_write_config()
{
	String status;

	if (task_set_ & T_LOCK)
		status += " lock data";

	if (task_set_ & T_WRITE_MAC)
	{
		if (!status.empty())
			status += ", ";
		status += " mac address";
	}

	std::cout << "  Writing " << status << "..." << "\n";
	print_result(programmer_.unit_config_write(mac_addr_, lock_data_));
}

//==============================================================================
void CC_Flasher::process_tasks()
{
	if (!task_set_)
	{
		std::cout << "  No actions specified" << "\n";
		return;
	}

	if (!validate_lock_options())
		return;

	target_locked_ = programmer_.unit_locked();
	if (target_locked_)
		std::cout << "  Target is locked." << "\n";

	if (task_set_ & T_TEST)
		task_test();

	if (target_locked_ && !(task_set_ & T_ERASE))
	{
		std::cout << "  No operations allowed on locked target without erasing\n";
		return;
	}

	if (target_locked_)
	{
		task_erase();
		task_set_ &= ~T_ERASE;
		if (target_locked_)
		{
			std::cout << "  Unit is still locked after erasing" << "\n";
			return;
		}
	}

	// Order of checking matters
	if (!validate_flash_size_options() || !validate_mac_options())
		return;

	if (task_set_ & T_PRESERVE_MAC)
	{
		programmer_.unit_mac_address_read(unit_info_.mac_address_count - 1,
				mac_addr_);

		std::cout << "  MAC address to preserve: "
				<< mac_address_to_string(mac_addr_) << "\n";

		task_set_ |= T_WRITE_MAC;
	}

	if (task_set_ & T_READ_MAC)
		task_read_mac_address();

	if (task_set_ & T_READ_INFO_PAGE)
		task_read_info_page();

	if (task_set_ & T_RESET)
	{
		programmer_.unit_reset();
		std::cout << "  Target reset" << "\n";
	}

	if (task_set_ & T_READ_FLASH)
		task_read_flash();

	if (task_set_ & T_ERASE)
		task_erase();

	if (task_set_ & T_WRITE_FLASH)
	{
		if (task_set_ & T_LOCK)
		{
			if (programmer_.flash_image_embed_lock_data(flash_write_data_, lock_data_))
				task_set_ &= ~T_LOCK;
		}

		if (task_set_ & T_WRITE_MAC)
		{
			if (programmer_.flash_image_embed_mac_address(flash_write_data_, mac_addr_))
				task_set_ &= ~T_WRITE_MAC;
		}
		task_write_flash();
	}

	if (task_set_ & T_VERIFY)
		task_verify_flash();

	if (task_set_ & (T_WRITE_MAC | T_LOCK))
		task_write_config();
}

//==============================================================================
void CC_Flasher::task_test()
{
	std::cout << "  Device info: " << "\n";

	CC_ProgrammerInfo info;
	programmer_.programmer_info(info);
	std::cout << info;

	if (target_locked_)
		return;

	std::cout << "\n";
	std::cout << "  Target info: " << "\n";
	std::cout << unit_info_;
	std::cout << "   Lock data size: " << programmer_.unit_lock_data_size()
			<< " B" << "\n";
}

//==============================================================================
void CC_Flasher::task_read_mac_address()
{
	ByteVector mac0;
	programmer_.unit_mac_address_read(0, mac0);

	if (unit_info_.mac_address_count == 1)
		std::cout << "  MAC address: " << mac_address_to_string(mac0) << "\n";
	else
	{
		ByteVector mac1;
		programmer_.unit_mac_address_read(1, mac1);

		std::cout << "  MAC addresses, primary: "
				<< mac_address_to_string(mac0) << ", secondary: "
				<< mac_address_to_string(mac1) << "\n";
	}
}

//==============================================================================
void CC_Flasher::task_read_info_page()
{
	if (!(unit_info_.flags & UnitInfo::SUPPORT_INFO_PAGE))
	{
		std::cout << "  Target does not reading Info Page" << "\n";
		return;
	}

	std::cout << "  Reading info page..." << "\n";

	Timer timer;
	ByteVector info_page;
	programmer_.unit_read_info_page(info_page);
	print_result(true, timer);

	if (info_page_read_target_.source_type() == ReadTarget::ST_CONSOLE)
	{
		std::cout << "  Information page (" << info_page.size() << " B):" << "\n";
		print_hex_dump(info_page);
	}
	else
		info_page_read_target_.on_read(info_page);
}

//==============================================================================
void CC_Flasher::task_erase()
{
	std::cout << "  Erasing flash..." << "\n";

	print_result(programmer_.unit_erase());

	programmer_.unit_connect(unit_info_);
	target_locked_ = programmer_.unit_locked();
}

//==============================================================================
void CC_Flasher::task_verify_flash()
{
	std::cout << "  Verifying flash..." << "\n";

	Timer timer;
	bool result = programmer_.unit_flash_verify(flash_write_data_, verify_method_);
	print_result(result, timer);
}

//==============================================================================
void CC_Flasher::task_read_flash()
{
	size_t size = unit_info_.actual_flash_size() / 1024;
	std::cout << "  Reading flash (" << size << " KB)..." << "\n";

	Timer timer;
	ByteVector flash_data;
	programmer_.unit_flash_read(flash_data);
	flash_read_target_.on_read(flash_data);
	print_result(true, timer);
}

//==============================================================================
void CC_Flasher::task_write_flash()
{
	size_t flash_size = unit_info_.flash_size ?
			unit_info_.flash_size : unit_info_.max_flash_size;
	flash_size *= 1024;

	if (flash_write_data_.upper_address() > flash_size)
	{
		std::cout << "  Flash image size exceeding flash physical size, writing canceled..." << "\n";
		task_set_ &= ~(T_VERIFY | T_LOCK);
		return;
	}

	String size = convinient_storage_size(flash_write_data_.actual_size());
	std::cout << "  Writing flash (" << size << ")..." << "\n";

	Timer timer;
	programmer_.unit_flash_write(flash_write_data_);
	print_result(true, timer);
}

//==============================================================================
CC_Flasher::CC_Flasher() :
		task_set_(0),
		verify_method_(CC_Programmer::VM_BY_CRC),
		target_locked_(false)
{
	programmer_.do_on_flash_read_progress(on_progress);
	programmer_.do_on_flash_write_progress(on_progress);
}
