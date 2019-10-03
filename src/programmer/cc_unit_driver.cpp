/*
 * cc_unit_driver.cpp
 *
 * Created on: Aug 2, 2011
 *     Author: George Stark <george-u@yandex.com>
 *
 * License: GNU GPL v2
 *
 */

#include <boost/crc.hpp>
#include "cc_unit_driver.h"
#include "cc_programmer.h"
#include "log.h"
#include "cc_debug_interface.h"

#include "data/binary_file.h" // remove
#include "data/hex_file.h"  // remove

typedef boost::crc_optimal<16, 0x8005, 0xFFFF, 0, false, false> CrcCalculator;

const size_t MAX_EMPTY_BLOCK_SIZE = FLASH_BANK_SIZE;
static uint8_t empty_block_[MAX_EMPTY_BLOCK_SIZE];

//==============================================================================
CC_UnitDriver::CC_UnitDriver(USB_Device &programmer, ProgressWatcher &pw) :

	usb_device_(programmer),
	pw_(pw),
	endpoint_in_(0),
	endpoint_out_(0)
{
	memset(empty_block_, FLASH_EMPTY_BYTE, FLASH_BANK_SIZE);
}

//==============================================================================
void CC_UnitDriver::set_programmer_ID(const USB_DeviceID& programmer_ID)
{
	endpoint_in_ = programmer_ID.endpoint_in;
	endpoint_out_ = programmer_ID.endpoint_out;
}

//==============================================================================
bool CC_UnitDriver::set_flash_size(uint_t flash_size)
{
	bool found = std::find(
			unit_info_.flash_sizes.begin(),
			unit_info_.flash_sizes.end(),
			flash_size) != unit_info_.flash_sizes.end();

	if (!unit_info_.flash_size && found)
	{
		unit_info_.flash_size = flash_size;
		return true;
	}
	return false;
}

//==============================================================================
void CC_UnitDriver::reset(bool debug_mode)
{
	log_info("programmer, reset target, debug mode: %u", debug_mode);

	const uint8_t USB_REQUEST_RESET	= 0xC9;
	uint16_t index = debug_mode ? 1 : 0;

	usb_device_.control_write(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
			USB_REQUEST_RESET, 0, index, NULL, 0);
}

//==============================================================================
void CC_UnitDriver::read_debug_status(uint8_t &status)
{
	log_info("programmer, read debug status");

	uint8_t command[] = { 0x1F, DEBUG_COMMAND_READ_STATUS };

	status = 0;

	usb_device_.bulk_write(endpoint_out_, sizeof(command), command);
	usb_device_.bulk_read(endpoint_in_, 1, &status);

	log_info("programmer, debug status, %02Xh", status);
}

//==============================================================================
void CC_UnitDriver::read_debug_config(uint8_t &config)
{
	log_info("programmer, read debug config");

	uint8_t command[] = { 0x1F, DEBUG_COMMAND_RD_CONFIG };

	usb_device_.bulk_write(endpoint_out_, sizeof(command), command);
	usb_device_.bulk_read(endpoint_in_, 1, &config);

	log_info("programmer, debug config, %02Xh", config);
}

//==============================================================================
void CC_UnitDriver::write_debug_config(uint8_t config)
{
	log_info("programmer, write debug config, %02Xh", config);

	uint8_t command[] = { 0x4C, DEBUG_COMMAND_WR_CONFIG, config };

	usb_device_.bulk_write(endpoint_out_, sizeof(command), command);
}

//==============================================================================
bool CC_UnitDriver::erase_page(uint_t page_offset)
{
	page_offset /= reg_info_.flash_word_size;

	write_xdata_memory(reg_info_.faddrl, 0);
	write_xdata_memory(reg_info_.faddrh, HIBYTE(page_offset));

	// erase
	write_xdata_memory(reg_info_.fctl, FCTL_ERASE);

	// wait for write to finish
	uint8_t reg;
	while ((reg = read_xdata_memory(reg_info_.fctl)) & FCTL_BUSY);

	return !(reg & FCTL_ABORT);
}

//==============================================================================
void CC_UnitDriver::erase()
{
        reset(true); /* Fixes problems with erase with CC2543 - at least
		      * for me - David Austin */
    
	log_info("programmer, erase");

	uint8_t command[] = { 0x1C, DEBUG_COMMAND_CHIP_ERASE };

	usb_device_.bulk_write(endpoint_out_, sizeof(command), command);
}

//==============================================================================
void CC_UnitDriver::write_sfr(uint8_t address, uint8_t value)
{
	log_info("write sfr at %02Xh, value: %02Xh", address, value);

	uint8_t header[] 		= { 0x40, 0x55, 0x00, };
	uint8_t mov_a_direct[] 	= { 0xBE, 0x57, 0x75, address, value };
	uint8_t footer[] 		= { 0x90, 0x56, 0x74 };

	ByteVector command;
	vector_append(command, header, sizeof(header));
	vector_append(command, mov_a_direct, sizeof(mov_a_direct));
	vector_append(command, footer, sizeof(footer));

	usb_device_.bulk_write(endpoint_out_, command.size(), &command[0]);
}

//==============================================================================
void CC_UnitDriver::read_sfr(uint8_t address, uint8_t &value)
{
	log_info("programmer, read sfr at %02Xh", address);

	uint8_t header[] 		= { 0x40, 0x55, 0x00, };
	uint8_t mov_a_direct[] 	= { 0x7F, 0x56, 0xE5, address };
	uint8_t footer[] 		= { 0x90, 0x56, 0x74 };

	ByteVector command;
	vector_append(command, header, sizeof(header));
	vector_append(command, mov_a_direct, sizeof(mov_a_direct));
	vector_append(command, footer, sizeof(footer));

	usb_device_.bulk_write(endpoint_out_, command.size(), &command[0]);
	usb_device_.bulk_read(endpoint_in_, 1, &value);

	log_info("programmer, sfr value: %02Xh", value);
}

//==============================================================================
uint8_t CC_UnitDriver::read_xdata_memory(uint16_t address)
{
	ByteVector data;
	read_xdata_memory(address, 1, data);
	return data[0];
}

//==============================================================================
void CC_UnitDriver::read_xdata_memory(uint16_t address, size_t count, ByteVector &data)
{
	log_info("programmer, read xdata memory at %04Xh, count: %u", address, count);

	uint8_t header[] = {
			0x40, 0x55, 0x00, 0x72, 0x56, 0xE5, 0x92, 0xBE, 0x57, 0x75,
			0x92, 0x00, 0x74, 0x56, 0xE5, 0x83, 0x76, 0x56, 0xE5, 0x82 };

	uint8_t footer[] 	= { 0xD4, 0x57, 0x90, 0xC2, 0x57, 0x75, 0x92, 0x90, 0x56, 0x74 };

	uint8_t load_dtpr[] = { 0xBE, 0x57, 0x90, 0x00, 0x00 };
	uint8_t mov_a_dtpr[] = { 0x4E, 0x55, 0xE0 };
	uint8_t inc_dtpr[] 	= { 0x5E, 0x55, 0xA3 };

	ByteVector command;
	vector_append(command, header, sizeof(header));

	load_dtpr[sizeof(load_dtpr) - 1] = address;
	load_dtpr[sizeof(load_dtpr) - 2] = address >> 8;
	vector_append(command, load_dtpr, sizeof(load_dtpr));

	for (size_t i = 0; i < count; i++)
	{
		if (i == (count - 1) || !((i + 1) % 64))
			mov_a_dtpr[0] |= 1;
		else
			mov_a_dtpr[0] &= ~1;

		vector_append(command, mov_a_dtpr, sizeof(mov_a_dtpr));
		vector_append(command, inc_dtpr, sizeof(inc_dtpr));
	}
	vector_append(command, footer, sizeof(footer));

	data.resize(count);

	usb_device_.bulk_write(endpoint_out_, command.size(), &command[0]);
	usb_device_.bulk_read(endpoint_in_, count, &data[0]);

	log_info("programmer, read xdata memory, data: %s", binary_to_hex(&data[0], count, " ").c_str());
}

//==============================================================================
bool CC_UnitDriver::flash_image_embed_mac_address(DataSectionStore &sections,
		const ByteVector &mac_address)
{
	return false;
}

//==============================================================================
bool CC_UnitDriver::flash_image_embed_lock_data(DataSectionStore &sections,
		const ByteVector &lock_data)
{
	return false;
}

//==============================================================================
void CC_UnitDriver::read_info_page(ByteVector &info_page)
{ }

//==============================================================================
void CC_UnitDriver::mac_address_read(size_t index, ByteVector &mac_address)
{ }

//==============================================================================
void CC_UnitDriver::write_xdata_memory(uint16_t address, uint8_t data)
{	write_xdata_memory(address, &data, 1); }

//==============================================================================
void CC_UnitDriver::write_xdata_memory(uint16_t address, const ByteVector &data)
{	write_xdata_memory(address, &data[0], data.size()); }

//==============================================================================
void CC_UnitDriver::write_xdata_memory(uint16_t address, const uint8_t data[], size_t size)
{
	log_info("programmer, write xdata memory at %04Xh, count: %u", address, size);

	uint8_t header[] = {
			0x40, 0x55, 0x00, 0x72, 0x56, 0xE5, 0x92, 0xBE, 0x57, 0x75, 0x92, 0x00,
			0x74, 0x56, 0xE5, 0x83, 0x76, 0x56, 0xE5, 0x82 };

	uint8_t footer[] = { 0xD4, 0x57, 0x90, 0xC2, 0x57, 0x75, 0x92, 0x90, 0x56, 0x74 };

	uint8_t load_dtpr[] 	= { 0xBE, 0x57, 0x90, HIBYTE(address), LOBYTE(address) };
	uint8_t mov_a_data[] 	= { 0x8E, 0x56, 0x74, 0x00 };
	uint8_t mov_dtpr_a[]	= { 0x5E, 0x55, 0xF0 };
	uint8_t inc_dtpr[] 		= { 0x5E, 0x55, 0xA3 };

	ByteVector command;
	vector_append(command, header, sizeof(header));
	vector_append(command, load_dtpr, sizeof(load_dtpr));

	for (size_t i = 0; i < size; i++)
	{
		mov_a_data[3] = data[i];
		vector_append(command, mov_a_data, sizeof(mov_a_data));
		vector_append(command, mov_dtpr_a, sizeof(mov_dtpr_a));
		vector_append(command, inc_dtpr, sizeof(inc_dtpr));
	}
	vector_append(command, footer, sizeof(footer));

	usb_device_.bulk_write(endpoint_out_, command.size(), &command[0]);
}

//==============================================================================
void CC_UnitDriver::select_info_page_flash(bool select_info_pages)
{
	uint8_t config = 0;
	read_debug_config(config);

	if (select_info_pages)
		config |= DEBUG_CONFIG_SEL_FLASH_INFO_PAGE;
	else
		config &= ~DEBUG_CONFIG_SEL_FLASH_INFO_PAGE;

	write_debug_config(config);
}

//==============================================================================
bool CC_UnitDriver::flash_verify_by_read(const DataSectionStore &section_store)
{
	ByteVector data;
	bool result = true;

	pw_.read_start(section_store.actual_size());

	flash_read_start();
	foreach (const DataSection &item, section_store.sections())
	{
		flash_read_block(item.address, item.size(), data);
		if (memcmp(&data[0], &item.data[0], item.size()))
		{
			//binary_file_save("vr.bin", data);
			result = false;
			break;
		}
		data.clear();
	}
	flash_read_end();

	pw_.read_finish();

	return result;
}

//==============================================================================
bool CC_UnitDriver::flash_verify_by_crc(const DataSectionStore &section_store)
{
	// Channel 0: Flash mapped to Xdata -> CRC shift register
	uint8_t dma_desc[8] = {
		  HIBYTE(reg_info_.xbank_offset),   // src[15:8]
		  LOBYTE(reg_info_.xbank_offset),   // src[7:0]
		  HIBYTE(reg_info_.rndh),    		// dest[15:8]
		  LOBYTE(reg_info_.rndh),      		// dest[7:0]
		  HIBYTE(reg_info_.verify_block_size),// block size[15:8]
		  LOBYTE(reg_info_.verify_block_size),// block size[7:0]
		  0x20,                     		// no trigger event, block mode
		  0x42,                   			// increment source
	};

	write_xdata_memory(reg_info_.dma_arm, 0x00);

	// set the pointer to the DMA descriptors
	write_xdata_memory(reg_info_.dma0_cfgl, LOBYTE(reg_info_.dma0_cfg_offset));
	write_xdata_memory(reg_info_.dma0_cfgh, HIBYTE(reg_info_.dma0_cfg_offset));

	size_t flash_bank = 0xFF; // correct flash bank will be set later

	pw_.read_start(section_store.actual_size());

	foreach (const DataSection &section, section_store.sections())
	{
		size_t section_offset = section.address;
		size_t total_size = section.size();
		size_t bank_offset = section_offset % FLASH_BANK_SIZE;
		while (total_size)
		{
			size_t count = std::min(total_size, reg_info_.verify_block_size);
			size_t flash_bank_0 = section_offset / FLASH_BANK_SIZE;
			size_t flash_bank_1 = (section_offset + count) / FLASH_BANK_SIZE;

			if (flash_bank_0 != flash_bank_1)
				count = FLASH_BANK_SIZE - (section_offset % FLASH_BANK_SIZE);

			if (flash_bank != flash_bank_0)
			{
				flash_bank = flash_bank_0;
				if (reg_info_.memctr)
					write_xdata_memory(reg_info_.memctr, flash_bank);
				bank_offset = section_offset % FLASH_BANK_SIZE;
			}

			dma_desc[0] = HIBYTE(bank_offset + reg_info_.xbank_offset);
			dma_desc[1] = LOBYTE(bank_offset + reg_info_.xbank_offset);
			dma_desc[4] = HIBYTE(count);
			dma_desc[5] = LOBYTE(count);
			write_xdata_memory(reg_info_.dma0_cfg_offset, dma_desc, sizeof(dma_desc));

			CrcCalculator crc_calc;
			crc_calc.process_bytes(&section.data[section.size() - total_size], count);
			if (calc_block_crc() != crc_calc.checksum())
				return false;

			total_size -= count;
			section_offset += count;
			bank_offset += count;

			pw_.read_progress(count);
		}
	}
	pw_.read_finish();
	return true;
}

//==============================================================================
uint16_t CC_UnitDriver::calc_block_crc()
{
	write_xdata_memory(reg_info_.rndl, 0xFF);
	write_xdata_memory(reg_info_.rndl, 0xFF);
	write_xdata_memory(reg_info_.dma_arm, 0x01);
	write_xdata_memory(reg_info_.dma_req, 0x01);

	while (!(read_xdata_memory(reg_info_.dma_irq) & 0x01));

	ByteVector xsfr;
	read_xdata_memory(reg_info_.rndl, 2, xsfr);
	return xsfr[0] | (xsfr[1] << 8);
}

//==============================================================================
static void create_read_proc(size_t count, ByteVector &proc)
{
	uint8_t clr_a[]			= { 0x5E, 0x55, 0xE4 };
	uint8_t mov_c_a_dptr_a[]= { 0x4E, 0x55, 0x93 };
	uint8_t inc_dptr[]		= { 0x5E, 0x55, 0xA3 };

	proc.clear();
	for (size_t i = 0; i < count; i++)
	{
		vector_append(proc, clr_a, sizeof(clr_a));
		if (!((i + 1) % 64) || i == (count - 1))
			mov_c_a_dptr_a[0] |= 0x01;
		vector_append(proc, mov_c_a_dptr_a, sizeof(mov_c_a_dptr_a));
		mov_c_a_dptr_a[0] &= ~0x01;
		vector_append(proc, inc_dptr, sizeof(inc_dptr));
	}
}

//==============================================================================
void CC_UnitDriver::flash_read_near(uint16_t address, size_t size, ByteVector &data)
{
	const uint8_t load_dtpr[] = { 0xBE, 0x57, 0x90, HIBYTE(address), LOBYTE(address) };
	usb_device_.bulk_write(endpoint_out_, sizeof(load_dtpr), load_dtpr);

	size_t offset = data.size();
	data.resize(offset + size, FLASH_EMPTY_BYTE);

	ByteVector command;
	for (size_t i = 0; i < size / FLASH_READ_CHUNK_SIZE; i++)
	{
		if (command.empty())
			create_read_proc(FLASH_READ_CHUNK_SIZE, command);

		usb_device_.bulk_write(endpoint_out_, command.size(), &command[0]);
		usb_device_.bulk_read(endpoint_in_, FLASH_READ_CHUNK_SIZE, &data[offset]);
		offset += FLASH_READ_CHUNK_SIZE;

		pw_.read_progress(FLASH_READ_CHUNK_SIZE);
	}

	if ((size % FLASH_READ_CHUNK_SIZE))
	{
		create_read_proc(size % FLASH_READ_CHUNK_SIZE, command);

		usb_device_.bulk_write(endpoint_out_, command.size(), &command[0]);
		usb_device_.bulk_read(endpoint_in_, size - offset, &data[offset]);

		pw_.read_progress(size - offset);
	}
}

//==============================================================================
void CC_UnitDriver::flash_read(size_t offset, size_t size, ByteVector &flash_data)
{
	uint8_t flash_bank = 0xFF;

	while (size)
	{
		size_t bank_offset = offset % FLASH_BANK_SIZE;
		size_t count = std::min(size, (size_t)0x8000);
		uint8_t flash_bank_0 = offset / FLASH_BANK_SIZE;
		uint8_t flash_bank_1 = (offset + count) / FLASH_BANK_SIZE;

		if (flash_bank_0 != flash_bank_1)
			size_t count = ((offset + count) % FLASH_BANK_SIZE);

		if (flash_bank != flash_bank_0)
		{
			flash_bank = flash_bank_0;
			write_xdata_memory(reg_info_.fmap, flash_bank);
			bank_offset = offset % FLASH_BANK_SIZE;
		}

		flash_read_near(bank_offset + reg_info_.xbank_offset, count, flash_data);

		size -= count;
		offset += count;
	}
}

//==============================================================================
void CC_UnitDriver::flash_read_start()
{
	const uint8_t USB_PREPARE = 0xC6;

	uint8_t byte = 0;
	usb_device_.control_read(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
			USB_PREPARE, 0, 0, &byte, 1);
	//reset(true); // if write and lock verifing will fail after reset

	uint8_t header[] = {
		0x40, 0x55, 0x00, 0x72, 0x56, 0xE5, 0xD0, 0x74, 0x56, 0xE5, 0x92, 0xBE,
		0x57, 0x75, 0x92, 0x00, 0x76, 0x56, 0xE5, 0x83, 0x78, 0x56, 0xE5, 0x82,
		0x7A, 0x56, 0xE5, 0x9F
	};

	usb_device_.bulk_write(endpoint_out_, sizeof(header), header);
}

//==============================================================================
void CC_UnitDriver::flash_read_end()
{
	uint8_t command[] = {
			0xCA, 0x57, 0x75, 0x9F, 0xD6, 0x57, 0x90, 0xC4, 0x57,
			0x75, 0x92, 0xC2, 0x57, 0x75, 0xD0, 0x90, 0x56, 0x74
	};
	usb_device_.bulk_write(endpoint_out_, sizeof(command), command);
}

//==============================================================================
void CC_UnitDriver::write_lock_to_info_page(uint8_t lock_byte)
{
	select_info_page_flash(true);

	ByteVector data;
	data.push_back(0xFF);
	data.push_back(lock_byte);

	DataSectionStore store;
	store.add_section(DataSection(0, data), true);
	write_flash_slow(store);

	select_info_page_flash(false);
}

//==============================================================================
uint_t CC_UnitDriver::lock_data_size() const
{
	return reg_info_.lock_size;
}

//==============================================================================
UnitCoreInfo CC_UnitDriver::get_reg_info()
{	return reg_info_; }

//==============================================================================
void CC_UnitDriver::set_reg_info(const UnitCoreInfo &reg_info)
{	reg_info_ = reg_info; }

//==============================================================================
bool CC_UnitDriver::empty_block(const uint8_t* data, size_t size)
{
	if (size <= MAX_EMPTY_BLOCK_SIZE)
		return memcmp(data, empty_block_, size) == 0;

	ByteVector empty_block;
	empty_block.resize(size, FLASH_EMPTY_BYTE);
	return memcmp(data, &empty_block[0], size) == 0;
}

//==============================================================================
void CC_UnitDriver::convert_lock_data_std_set(
		const StringVector& qualifiers,
		const ByteVector& lock_sizes,
		ByteVector& lock_data)
{
	ByteVector data(1, 0x1F);

	foreach (const String &item, qualifiers)
	{
		if (item == "debug")
			data[0] &= ~1;
		else
		if (item == "boot")
			data[0] &= ~0x10;
		else
		if (item == "flash" || item == "pages")
			data[0] &= ~0x0E;
		else
		if (item.find("flash:") == 0)
		{
			uint_t size = 0;
			String arg = item.substr(ARRAY_SIZE("flash:") - 1);
			if (!string_to_number(arg, size))
				throw std::runtime_error("incorrect flash size value");

			ByteVector::const_iterator it = std::find(
					lock_sizes.begin(),
					lock_sizes.end(),
					size);

			if (it == lock_sizes.end())
				throw std::runtime_error("target unsupport flash size " + arg);

			uint8_t index = (uint8_t)(it - lock_sizes.begin());

			data[0] &= ~(7 << 1); // clear out prev lock size data
			data[0] |= (~index & 0x07) << 1;
		}
		else
			throw std::runtime_error("unknown lock qualifyer: " + item);
	}
	lock_data = data;
}

//==============================================================================
void CC_UnitDriver::write_flash_slow(const DataSectionStore &section_store)
{
	const size_t WRITE_BLOCK_SIZE = reg_info_.write_block_size;

	// Channel 0: Xdata buffer -> Flash controller
	uint8_t dma_desc[8] = {
		  HIBYTE(reg_info_.dma_data_offset),// src[15:8]
		  LOBYTE(reg_info_.dma_data_offset),// src[7:0]
		  HIBYTE(reg_info_.fwdata),    		// dest[15:8]
		  LOBYTE(reg_info_.fwdata),      	// dest[7:0]
		  HIBYTE(WRITE_BLOCK_SIZE),			// block size[15:8]
		  LOBYTE(WRITE_BLOCK_SIZE),			// block size[7:0]
		  18,								// trigger FLASH
		  0x42 								// increment source
	};

	// Load dma descriptors
	write_xdata_memory(reg_info_.dma0_cfg_offset, dma_desc, sizeof(dma_desc));

	// Set the pointer to the DMA descriptors
	write_xdata_memory(reg_info_.dma0_cfgl, LOBYTE(reg_info_.dma0_cfg_offset));
	write_xdata_memory(reg_info_.dma0_cfgh, HIBYTE(reg_info_.dma0_cfg_offset));

	size_t faddr = (size_t)-1;

	ByteVector data;
	section_store.create_image(FLASH_EMPTY_BYTE, data);
	data.resize(align_up(section_store.upper_address(), WRITE_BLOCK_SIZE),
			FLASH_EMPTY_BYTE);

	pw_.write_start(data.size());

	for (size_t i = 0; i < (data.size() / WRITE_BLOCK_SIZE); i++)
	{
		pw_.write_progress(WRITE_BLOCK_SIZE);

		size_t offset = WRITE_BLOCK_SIZE * i;
		if (empty_block(&data[offset], WRITE_BLOCK_SIZE))
			continue;

		size_t next_faddr = offset / reg_info_.flash_word_size;
		if (next_faddr != faddr)
		{
			write_xdata_memory(reg_info_.faddrl, LOBYTE(next_faddr));
			write_xdata_memory(reg_info_.faddrh, HIBYTE(next_faddr));
			faddr = next_faddr + WRITE_BLOCK_SIZE / reg_info_.flash_word_size;
		}

		write_xdata_memory(reg_info_.dma_data_offset, &data[offset], WRITE_BLOCK_SIZE);

		write_xdata_memory(reg_info_.dma_arm, 0x01);
		write_xdata_memory(reg_info_.fctl, reg_info_.fctl_write);
		while ((read_xdata_memory(reg_info_.fctl) & FCTL_BUSY));
	}
	pw_.write_finish();
}
