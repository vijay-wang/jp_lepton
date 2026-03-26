#include <stdio.h>
#include <unistd.h>
#include "sdk.h"
#include "LEPTON_SDK.h"
#include "LEPTON_Types.h"
#include "LEPTON_SYS.h"
#include "log.h"

#define SERVER_IP_DEFAULT   "192.168.21.2"
#define SERVER_PORT_DEFAULT 8080

static const char *g_ip;
static uint16_t    g_port;

int main(int argc, char *argv[])
{
	LEP_RESULT result;
	LEP_CAMERA_PORT_DESC_T portDesc;
	LEP_SDK_VERSION_T version;

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
		sdk_destroy(h);
		pr_err("%s\n", sdk_strerror(err));
		goto connect_failed;
	}

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

	result = LEP_SetSysShutterPosition(&portDesc, LEP_SYS_SHUTTER_POSITION_CLOSED);
	if (result != LEP_OK)
		pr_err("LEP_SetSysShutterPosition failed");

	sleep(1);

	result = LEP_SetSysShutterPosition(&portDesc, LEP_SYS_SHUTTER_POSITION_OPEN);
	if (result != LEP_OK)
		pr_err("LEP_SetSysShutterPosition failed");

	LEP_GetSDKVersion(&portDesc, &version);
	pr_info("LEPTON Sdk version:%d.%d.%d\n", version.major, version.minor, version.build);


	LEP_ClosePort(&portDesc);
select_dev_failed:
open_port_failed:
	sdk_disconnect(h);
connect_failed:
	sdk_destroy(h);

	return 0;
}
