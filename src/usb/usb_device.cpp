/*
 * usb_device.cpp
 *
 * Created on: Aug 2, 2011
 *     Author: George Stark <george-u@yandex.com>
 *
 * License: GNU GPL v2
 *
 */

#include "usb_device.h"
#include "log.h"

static void on_error(const String &context, int error = LIBUSB_ERROR_OTHER);

typedef std::vector<libusb_device *> libusb_device_vector;

//==============================================================================
class USB_Enumerator
{
public:
	USB_Enumerator(USB_ContextPtr context, libusb_device_vector &devices);
	~USB_Enumerator();

private:
	libusb_device **list_;
};

//==============================================================================
USB_Enumerator::USB_Enumerator(USB_ContextPtr context, libusb_device_vector &devices)
{
	ssize_t count = libusb_get_device_list(context.get(), &list_);
	if (count < 0)
		on_error("libusb_get_device_list", count);

	for (ssize_t i = 0; i < count; i++)
		devices.push_back(list_[i]);
}

//==============================================================================
USB_Enumerator::~USB_Enumerator()
{	libusb_free_device_list(list_, true); }

//==============================================================================
static const char* libusb_error_string(enum libusb_error errcode)
{
    switch (errcode)
    {
        case LIBUSB_SUCCESS:
            return "Success";
        case LIBUSB_ERROR_IO:
            return "Input/output error";
        case LIBUSB_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case LIBUSB_ERROR_ACCESS:
            return "Access denied (insufficient permissions)";
        case LIBUSB_ERROR_NO_DEVICE:
            return "No such device (it may have been disconnected)";
        case LIBUSB_ERROR_NOT_FOUND:
            return "Entity not found";
        case LIBUSB_ERROR_BUSY:
            return "Resource busy";
        case LIBUSB_ERROR_TIMEOUT:
            return "Operation timed out";
        case LIBUSB_ERROR_OVERFLOW:
            return "Overflow";
        case LIBUSB_ERROR_PIPE:
            return "Pipe error";
        case LIBUSB_ERROR_INTERRUPTED:
            return "System call interrupted (perhaps due to signal)";
        case LIBUSB_ERROR_NO_MEM:
            return "Insufficient memory";
        case LIBUSB_ERROR_NOT_SUPPORTED:
            return "Operation not supported or unimplemented on this platform";
        case LIBUSB_ERROR_OTHER:
            return "Other error";

        default:
           return "Unknown error";
    }
}

//==============================================================================
static void on_error(const String &context, int error)
{
	throw std::runtime_error(context + " failed, " +
			libusb_error_string((libusb_error)error));
}

//==============================================================================
static void on_timeout_error(const String &context, ssize_t total,
		ssize_t transferred)
{
	std::stringstream ss;
	ss << context << " timeout, transferred " << transferred << " from " << total;

	throw std::runtime_error(ss.str());
}

//==============================================================================
USB_Device::USB_Device() :
	handle_(NULL),
	device_(NULL),
	timeout_(0)
{ }

//==============================================================================
USB_Device::~USB_Device()
{	close(); }

//==============================================================================
void USB_Device::init_context()
{
	if (context_)
		return;

	libusb_context *context = NULL;
	int result = libusb_init(&context);
	if (result != LIBUSB_SUCCESS)
		on_error("Failed to init USB context", result);

	context_ = USB_ContextPtr(context, libusb_exit);
}

//==============================================================================
bool USB_Device::opened() const
{
	return handle_ != NULL;
}

//==============================================================================
void USB_Device::close()
{
	if (handle_)
	{
		libusb_close(handle_);
		handle_ = NULL;
	}
}

//==============================================================================
bool USB_Device::open_by_vid_pid(uint16_t vendor_id, uint16_t product_id)
{
	init_context();
	close();

	libusb_device_vector devices;
	USB_Enumerator enumerator(context_, devices);
	libusb_device_descriptor descriptor;

	foreach (libusb_device *item, devices)
	{
		if (!libusb_get_device_descriptor(item, &descriptor) &&
				descriptor.idVendor == vendor_id &&
				descriptor.idProduct == product_id)
		{
			int result = libusb_open(item, &handle_);
			if (result < 0)
				on_error("libusb_open", result);
			device_ = libusb_get_device(handle_);

			log_info("usb, open device, VID: %04Xh, PID: %04Xh", vendor_id, product_id);

			return true;
		}
	}
	return false;
}

//==============================================================================
bool USB_Device::open_by_address(uint8_t bus_number, uint8_t device_address)
{
	init_context();
	close();

	libusb_device_vector devices;
	USB_Enumerator enumerator(context_, devices);

	foreach (libusb_device *item,  devices)
	{
		if (libusb_get_bus_number(item) == bus_number &&
				libusb_get_device_address(item) == device_address)
		{
			int result = libusb_open(item, &handle_);
			if (result < 0)
				on_error("libusb_open", result);
			device_ = libusb_get_device(handle_);
			return true;
		}
	}
	return false;
}

//==============================================================================
void USB_Device::check_open()
{
	if (!opened())
		on_error("try using not-opened device");
}

//==============================================================================
void USB_Device::device_decriptor(libusb_device_descriptor &descriptor)
{
	check_open();

	int result = libusb_get_device_descriptor(device_, &descriptor);
	if (result != LIBUSB_SUCCESS)
		on_error("Failed to get device descriptor", (libusb_error)result);
}

//==============================================================================
void USB_Device::string_descriptor_utf8(uint8_t index, uint16_t language, String &data)
{
	check_open();

	uint8_t raw_data[256];
	int result = libusb_get_string_descriptor(handle_, index, language, raw_data,
			sizeof(raw_data));
	if (result < 0)
		on_error("Failed to get string descriptor " + number_to_string(index),
				(libusb_error)result);

	data.assign(raw_data, raw_data + result);

	raw_data[result] = '\0';
	log_info("usb, get string descriptor %u, language: %u, data: %s", index, language, raw_data);
}

//==============================================================================
void USB_Device::string_descriptor_ascii(uint8_t index, String &data)
{
	check_open();

	uint8_t raw_data[256];
	int result = libusb_get_string_descriptor_ascii(handle_, index, raw_data,
			sizeof(raw_data));
	if (result < 0)
		on_error("Failed to get string descriptor " + number_to_string(index),
				(libusb_error)result);

	data.assign(raw_data, raw_data + result);

	raw_data[result] = '\0';
	log_info("usb, get string descriptor %u, data: %s", index, raw_data);
}

//==============================================================================
void USB_Device::set_transfer_timeout(uint_t timeout)
{
	log_info("usb, set timeout %u ms", timeout);

	timeout_ = timeout;
}

//==============================================================================
void USB_Device::set_debug_mode(DebugLevel level)
{
#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000106)
	libusb_set_option(context_.get(), LIBUSB_OPTION_LOG_LEVEL, level);
#else
	libusb_set_debug(context_.get(), level);
#endif
}

//==============================================================================
void USB_Device::reset_device()
{
	libusb_reset_device(handle_);
}

//==============================================================================
void USB_Device::claim_interface(uint_t interface_number)
{
	log_info("usb, claim interface %u", interface_number);

	int result = libusb_claim_interface(handle_, interface_number);
	if (result < 0)
		on_error("claim_interface", result);
}

//==============================================================================
void USB_Device::release_interface(uint_t interface_number)
{
	log_info("usb, release interface %u", interface_number);

	int result = libusb_release_interface(handle_, interface_number);
	if (result < 0)
		on_error("release_interface", result);
}

//==============================================================================
void USB_Device::set_configuration(uint_t configuration)
{
	log_info("usb, set configuration %u", configuration);

	int result = libusb_set_configuration(handle_, configuration);
	if (result < 0)
		on_error("set_configuration", result);
}

//==============================================================================
void USB_Device::bulk_read(uint8_t endpoint, size_t count, uint8_t data[])
{
	int transferred = 0;
	endpoint |= LIBUSB_ENDPOINT_IN;

	ssize_t result = libusb_bulk_transfer(handle_, endpoint, data, count,
			&transferred, timeout_);

	log_info("usb, bulk read, count %u: data: %s",
			count, binary_to_hex(data, transferred, " ").c_str());

	if (result < 0)
		on_error("libusb_bulk_transfer (in)", result);

	if ((int)count != transferred)
		on_timeout_error("libusb_bulk_transfer (in)", count, transferred);
}

//==============================================================================
void USB_Device::bulk_write(uint8_t endpoint, size_t count, const uint8_t data[])
{
	log_info("usb, bulk write, count: %u, data: %s", count,
			binary_to_hex(data, count, " ").c_str());

	int transferred = 0;
	endpoint |= LIBUSB_ENDPOINT_OUT;

	ssize_t result = libusb_bulk_transfer(handle_, endpoint, const_cast<uint8_t*>(data), count,
			&transferred, timeout_);
	if (result < 0)
		on_error("libusb_bulk_transfer (out)", result);

	if ((int)count != transferred)
		on_timeout_error("libusb_bulk_transfer (out)", count, transferred);
}

//==============================================================================
void USB_Device::control_write(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue,
		uint16_t wIndex, const uint8_t data[], size_t count)
{
	bmRequestType |= LIBUSB_ENDPOINT_OUT;

	log_info("usb, control write, request_type: %02Xh, request: %02Xh, value: %04Xh, index: %04Xh, count: %u",
			bmRequestType, bRequest, wValue, wIndex, count);
	if (count)
		log_info("usb, control write, data: %s", binary_to_hex(data, count, " ").c_str());

	ssize_t result = libusb_control_transfer(handle_, bmRequestType, bRequest,
			wValue, wIndex, const_cast<uint8_t*>(data), count, timeout_);
	if (result < 0)
		on_error("libusb_control_transfer (out)", result);

	if (count && (ssize_t)count != result)
		on_timeout_error("libusb_control_transfer (out)", count, result);
}

//==============================================================================
void USB_Device::control_read(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue,
		uint16_t wIndex, uint8_t data[], size_t count)
{
	bmRequestType |= LIBUSB_ENDPOINT_IN;

	log_info("usb, control read, request_type: %02Xh, request: %02Xh, value: %04Xh, index: %04Xh, count: %u",
			bmRequestType, bRequest, wValue, wIndex, count);

	ssize_t result = libusb_control_transfer(handle_, bmRequestType, bRequest,
			wValue, wIndex, data, count, timeout_);
	if (result < 0)
		on_error("libusb_control_transfer (in)", result);

	if (count && (ssize_t)count != result)
		on_timeout_error("libusb_control_transfer (in)", count, result);

	log_info("usb, control read, data: %s", binary_to_hex(data, count, " ").c_str());
}
