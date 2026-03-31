#include <stdio.h>
#include <stdint.h>
#include "sdk.h"
#include "LEPTON_SDK.h"
#include "LEPTON_Types.h"
#include "LEPTON_SYS.h"
#include "LEPTON_OEM.h"
#include "log.h"
#include "perf_tick.h"

#define SERVER_IP_DEFAULT   "192.168.21.2"
#define SERVER_PORT_DEFAULT 8080

static const char *g_ip;
static uint16_t    g_port;
void print_beijing_time(long long timestamp_us) {
	time_t seconds = timestamp_us / 1000000;
	int microseconds = timestamp_us % 1000000;

	seconds += 8 * 3600;

	struct tm *t = gmtime(&seconds);

	pr_debug("Beijing time: %04d-%02d-%02d %02d:%02d:%02d.%06d, %llu\n",
			t->tm_year + 1900,
			t->tm_mon + 1,
			t->tm_mday,
			t->tm_hour,
			t->tm_min,
			t->tm_sec,
			microseconds, timestamp_us);
}


int main(int argc, char *argv[])
{
	LEP_RESULT result;
	LEP_CAMERA_PORT_DESC_T portDesc;
	LEP_SDK_VERSION_T version;
	LEP_OEM_GPIO_MODE_E gpio_mode;

	sdk_handle_t *h;
	sdk_err_t     err;

	g_ip     = (argc >= 2) ? argv[1] : SERVER_IP_DEFAULT;
	g_port   = (argc >= 3) ? (uint16_t)atoi(argv[2])
		: (uint16_t)SERVER_PORT_DEFAULT;


	h = sdk_create(NULL);
	if (h == NULL) {
		pr_err("sdk_create failed\n");
		return -1;
	}

	err = sdk_connect(h, g_ip, g_port);
	if (err != SDK_OK) {
		pr_err("%s\n", sdk_strerror(err));
		goto connect_failed;
	}

	/* Upload a local buffer to a path on the device */
	err = sdk_send_file(h, "/tmp/test.txt", "wangwenjie", 10, 20);
	if (err != SDK_OK)
		pr_err("send file failed %s\n", sdk_strerror(err));

	/* Download a file frome a path on the device */
	uint8_t *recv_file_buf = (uint8_t *)malloc(4 * 1024 * 1024);
	size_t recv_file_len;
	err = sdk_recv_file(h, "/cmake_install.cmake", &recv_file_buf, &recv_file_len, 500);
	free(recv_file_buf);
	pr_info("read file length: %ld\n", recv_file_len);

	portDesc.cci_handle = h;
	portDesc.portType = LEP_CCI_TWI;
	result = LEP_SelectDevice(&portDesc, MAC_COM);
	if (result != LEP_OK) {
		pr_err("LEP_SelectDevice failed");
		goto select_dev_failed;
	}

	result = LEP_OpenPort(0, LEP_CCI_TWI, 0, &portDesc);
	if (result != LEP_OK) {
		pr_err("LEP_OpenPort failed");
		goto open_port_failed;
	}

	/* get sdk version */
	LEP_GetSDKVersion(&portDesc, &version);
	pr_info("LEPTON Sdk version:%d.%d.%d\n", version.major, version.minor, version.build);

	/* shutter control */
uint64_t elapsed;

PERF_MEASURE_US(elapsed,
	result = LEP_SetSysShutterPosition(&portDesc, LEP_SYS_SHUTTER_POSITION_CLOSED);
	if (result != LEP_OK) {
		pr_err("LEP_SetSysShutterPosition failed");
		goto cci_ops_failed;
	}
);

	pr_debug("elapsed time: %ld us\n", (uint64_t)elapsed);


PERF_MEASURE_US(elapsed,
	result = LEP_SetSysShutterPosition(&portDesc, LEP_SYS_SHUTTER_POSITION_OPEN);
	if (result != LEP_OK) {
		pr_err("LEP_SetSysShutterPosition failed");
		goto cci_ops_failed;
	}
);

	pr_debug("elapsed time: %ld us\n", (uint64_t)elapsed);


	/* enable vsync signal, and then the image streaming will start */
	gpio_mode = LEP_OEM_END_GPIO_MODE;
	result = LEP_GetOemGpioMode(&portDesc, &gpio_mode);
	if (result != LEP_OK) {
		pr_err("LEP_GetOemGpioMode failed");
		goto cci_ops_failed;
	}
	pr_info("LEP_GetOemGpioMode gpio_mode = %d result = %d.\n", gpio_mode, result);

	result = LEP_SetOemGpioMode(&portDesc, LEP_OEM_GPIO_MODE_VSYNC);
	if (result != LEP_OK) {
		pr_err("LEP_SetOemGpioMode failed");
		goto cci_ops_failed;
	}
	pr_info("LEP_SetOemGpioMode result = %d.\n", result);

	gpio_mode = LEP_OEM_END_GPIO_MODE;
	result = LEP_GetOemGpioMode(&portDesc, &gpio_mode);
	if (result != LEP_OK) {
		pr_err("LEP_GetOemGpioMode failed");
		goto cci_ops_failed;
	}
	pr_info("LEP_GetOemGpioMode gpio_mode = %d result = %d.\n", gpio_mode, result);

	for (int i = 0; i < 10; ++i) {
		sdk_image_buf_t *buf = sdk_recv_image(h, 120);
		if (buf == NULL) {
			pr_info("timeout or network error\n");
			continue;
		}

		print_beijing_time(buf->timestamp);
		print_beijing_time(buf->img_timestamp);

		sdk_release_image(h, buf);
	}

	/* disable vsync signal, and the the image streaming will stop */
	result = LEP_SetOemGpioMode(&portDesc, LEP_OEM_GPIO_MODE_GPIO);
	if (result != LEP_OK) {
		pr_err("LEP_SetOemGpioMode failed\n");
		goto cci_ops_failed;
	}
	pr_info("LEP_SetOemGpioMode result = %d.\n", result);

cci_ops_failed:
	LEP_ClosePort(&portDesc);

select_dev_failed:
open_port_failed:
	sdk_disconnect(h);
connect_failed:
	sdk_destroy(h);

	pr_info("exit sdk_demo\n");
	return 0;
}
