#include <stdio.h>
#include <stdint.h>
#include "sdk.h"
#include "LEPTON_SDK.h"
#include "LEPTON_Types.h"
#include "LEPTON_SYS.h"
#include "LEPTON_OEM.h"
#include "log.h"
#include "perf_tick.h"
#if defined (__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#endif


#define SERVER_IP_DEFAULT   "192.168.21.180"
#define SERVER_PORT_DEFAULT 8080

static const char *g_ip;
static uint16_t    g_port;
void print_beijing_time(long long timestamp_us)
{
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

/**
 * y16_minmax() - find minimum and maximum Y16 values in a frame
 * @buf:    pointer to Y16 buffer
 * @pixels: number of pixels
 * @min:    output minimum value
 * @max:    output maximum value
 */
static void y16_minmax(const uint16_t *buf, unsigned int pixels,
		       uint16_t *min, uint16_t *max)
{
	unsigned int i;
	uint16_t lo = buf[0];
	uint16_t hi = buf[0];

	for (i = 1; i < pixels; i++) {
		if (buf[i] < lo)
			lo = buf[i];
		if (buf[i] > hi)
			hi = buf[i];
	}

	*min = lo;
	*max = hi;
}

/**
 * y16_to_rgb() - convert Y16 thermal buffer to ARGB8888 with iron colormap
 * @y16:     input Y16 buffer
 * @rgb:     output ARGB8888 buffer
 * @pixels:  number of pixels to convert
 * @y16_min: minimum Y16 value in this frame
 * @y16_max: maximum Y16 value in this frame
 *
 * Applies an iron colormap:
 *   0x00-0x3F: black  -> blue
 *   0x40-0x7F: blue   -> red
 *   0x80-0xBF: red    -> yellow
 *   0xC0-0xFF: yellow -> white
 */
static void y16_to_rgb(const uint16_t *y16, uint32_t *rgb,
		       unsigned int pixels,
		       uint16_t y16_min, uint16_t y16_max)
{
	unsigned int i;
	uint32_t range = y16_max - y16_min;

	if (range == 0)
		range = 1;

	for (i = 0; i < pixels; i++) {
		uint32_t val = y16[i];
		uint8_t r, g, b;
		uint32_t norm;

		if (val < y16_min)
			val = y16_min;
		else if (val > y16_max)
			val = y16_max;

		norm = (val - y16_min) * 255 / range;

		if (norm < 64) {
			r = 0;
			g = 0;
			b = (uint8_t)(norm * 4);
		} else if (norm < 128) {
			r = (uint8_t)((norm - 64) * 4);
			g = 0;
			b = 255;
		} else if (norm < 192) {
			r = 255;
			g = (uint8_t)((norm - 128) * 4);
			b = (uint8_t)(255 - (norm - 128) * 4);
		} else {
			r = 255;
			g = 255;
			b = (uint8_t)((norm - 192) * 4);
		}

		rgb[i] = (0xFFu << 24) | ((uint32_t)r << 16) |
			 ((uint32_t)g << 8) | b;
	}
}

#if defined (__linux__)
/**
 * blit_to_fb() - scale and blit ARGB8888 buffer to framebuffer
 * @fb_ptr:  mmap'd framebuffer pointer
 * @vinfo:   framebuffer variable screen info
 * @finfo:   framebuffer fixed screen info
 * @src:     source ARGB8888 buffer
 * @src_w:   source width in pixels
 * @src_h:   source height in pixels
 *
 * Nearest-neighbour scaling to fill the framebuffer display area.
 * Supports 32bpp framebuffers only.
 */
static void blit_to_fb(uint8_t *fb_ptr,
		       const struct fb_var_screeninfo *vinfo,
		       const struct fb_fix_screeninfo *finfo,
		       const uint32_t *src,
		       unsigned int src_w, unsigned int src_h)
{
	unsigned int x, y;
	unsigned int dst_w = vinfo->xres;
	unsigned int dst_h = vinfo->yres;

	for (y = 0; y < dst_h; y++) {
		unsigned int src_y = y * src_h / dst_h;

		for (x = 0; x < dst_w; x++) {
			unsigned int src_x = x * src_w / dst_w;
			uint32_t pixel = src[src_y * src_w + src_x];
			unsigned int offset = y * finfo->line_length +
					      x * (vinfo->bits_per_pixel / 8);

			*(uint32_t *)(fb_ptr + offset) = pixel;
		}
	}
}

/**
 * lepton_fb_display() - convert one Y16 frame and display it on the framebuffer
 * @fb_ptr:  mmap'd framebuffer pointer
 * @vinfo:   framebuffer variable screen info
 * @finfo:   framebuffer fixed screen info
 * @y16:     input Y16 frame buffer
 * @width:   frame width in pixels
 * @height:  frame height in pixels
 * @rgb:     temporary ARGB8888 working buffer (width * height * 4 bytes)
 */
static void lepton_fb_display(uint8_t *fb_ptr,
			      const struct fb_var_screeninfo *vinfo,
			      const struct fb_fix_screeninfo *finfo,
			      const uint16_t *y16,
			      unsigned int width, unsigned int height,
			      uint32_t *rgb)
{
	uint16_t y16_min, y16_max;

	y16_minmax(y16, width * height, &y16_min, &y16_max);
	y16_to_rgb(y16, rgb, width * height, y16_min, y16_max);
	blit_to_fb(fb_ptr, vinfo, finfo, rgb, width, height);
}
#endif

int main(int argc, char *argv[])
{
	LEP_RESULT result;
	LEP_CAMERA_PORT_DESC_T portDesc;
	LEP_SDK_VERSION_T version;
	LEP_OEM_GPIO_MODE_E gpio_mode;
	uint64_t elapsed;
	uint32_t *rgb_buf;
#if defined (__linux__)
	int fb_fd;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	uint8_t *fb_ptr;
	size_t fb_size;
#endif

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
#define TEST_BUF_SIZE (4 * 1024 * 1024)
	uint8_t *wr_file_buf = (uint8_t *)malloc(TEST_BUF_SIZE);

	err = sdk_send_file(h, "/tmp/test.txt", wr_file_buf, TEST_BUF_SIZE, 300);
	if (err != SDK_OK)
		pr_err("Upload file failed, %s\n", sdk_strerror(err));
	else
		pr_info("Upload file successfully\n");
	free(wr_file_buf);

	/* Download a file frome a path on the device */
	uint8_t *recv_file_buf = (uint8_t *)malloc(TEST_BUF_SIZE);
	size_t recv_file_len;

	err = sdk_recv_file(h, "/etc/inittab", &recv_file_buf, &recv_file_len, 200);
	if (err != SDK_OK)
		pr_err("Download file failed, %s\n", sdk_strerror(err));
	else
		pr_info("Download file successfully, length:%ld\n", recv_file_len);
	free(recv_file_buf);

	portDesc.cci_handle = h;
	portDesc.portType = LEP_CCI_TWI;
	result = LEP_SelectDevice(&portDesc, MAC_COM);
	if (result != LEP_OK) {
		pr_err("LEP_SelectDevice failed\n");
		goto select_dev_failed;
	}

	result = LEP_OpenPort(0, LEP_CCI_TWI, 0, &portDesc);
	if (result != LEP_OK) {
		pr_err("LEP_OpenPort failed\n");
		goto open_port_failed;
	}

	/* get sdk version */
	LEP_GetSDKVersion(&portDesc, &version);
	pr_info("LEPTON Sdk version:%d.%d.%d\n", version.major, version.minor, version.build);

	/* shutter control */

PERF_MEASURE_US(elapsed,
	result = LEP_SetSysShutterPosition(&portDesc, LEP_SYS_SHUTTER_POSITION_CLOSED);
	if (result != LEP_OK) {
		pr_err("LEP_SetSysShutterPosition failed\n");
		goto cci_ops_failed;
	}
);

	pr_debug("LEP_SetSysShutterPosition elapsed time: %ld us\n", (uint64_t)elapsed);


PERF_MEASURE_US(elapsed,
	result = LEP_SetSysShutterPosition(&portDesc, LEP_SYS_SHUTTER_POSITION_OPEN);
	if (result != LEP_OK) {
		pr_err("LEP_SetSysShutterPosition failed\n");
		goto cci_ops_failed;
	}
);

	pr_debug("LEP_SetSysShutterPosition elapsed time: %ld us\n", (uint64_t)elapsed);

	/* turn on telemetry data, this operation must be ealier than strating stream */
	result = LEP_SetSysTelemetryLocation(&portDesc, LEP_TELEMETRY_LOCATION_HEADER);
	if (result != LEP_OK) {
		pr_err("LEP_SetSysTelemetryLocation failed\n");
		goto cci_ops_failed;
	}

	LEP_SYS_TELEMETRY_LOCATION_E telemetry_loc;
	result = LEP_GetSysTelemetryLocation(&portDesc, &telemetry_loc);
	if (result != LEP_OK) {
		pr_err("LEP_GetSysTelemetryLocation failed\n");
		goto cci_ops_failed;
	}

	pr_debug("LEP_GetSysTelemetryLocation telemetry_loc = %d\n", telemetry_loc);

	result = LEP_SetSysTelemetryEnableState(&portDesc, LEP_TELEMETRY_ENABLED);
	if (result != LEP_OK) {
		pr_err("LEP_SetSysTelemetryEnableState failed\n");
		goto cci_ops_failed;
	}

	LEP_SYS_TELEMETRY_ENABLE_STATE_E telemetry_status;
	result = LEP_GetSysTelemetryEnableState(&portDesc, &telemetry_status);
	if (result != LEP_OK) {
		pr_err("LEP_GetSysTelemetryEnableState failed\n");
		goto cci_ops_failed;
	}

	pr_debug("LEP_GetSysTelemetryEnableState telemetry_status = %d\n", telemetry_status);

	/* enable vsync signal, and then the image streaming will start */
	gpio_mode = LEP_OEM_END_GPIO_MODE;
	result = LEP_GetOemGpioMode(&portDesc, &gpio_mode);
	if (result != LEP_OK) {
		pr_err("LEP_GetOemGpioMode failed\n");
		goto cci_ops_failed;
	}
	pr_info("LEP_GetOemGpioMode gpio_mode = %d result = %d.\n", gpio_mode, result);

	result = LEP_SetOemGpioMode(&portDesc, LEP_OEM_GPIO_MODE_VSYNC);
	if (result != LEP_OK) {
		pr_err("LEP_SetOemGpioMode failed\n");
		goto cci_ops_failed;
	}
	pr_info("LEP_SetOemGpioMode result = %d.\n", result);

	gpio_mode = LEP_OEM_END_GPIO_MODE;
	result = LEP_GetOemGpioMode(&portDesc, &gpio_mode);
	if (result != LEP_OK) {
		pr_err("LEP_GetOemGpioMode failed\n");
		goto cci_ops_failed;
	}
	pr_info("LEP_GetOemGpioMode gpio_mode = %d result = %d.\n", gpio_mode, result);

#if defined (__linux__)
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd < 0) {
		perror("open /dev/fb0");
		goto err_free;
	}

		if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		perror("ioctl FBIOGET_VSCREENINFO");
		goto err_close;
	}

	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		perror("ioctl FBIOGET_FSCREENINFO");
		goto err_close;
	}

	if (vinfo.bits_per_pixel != 32) {
		fprintf(stderr, "unsupported bpp: %u (require 32)\n",
			vinfo.bits_per_pixel);
		goto err_close;
	}

	fb_size = finfo.line_length * vinfo.yres;
	fb_ptr = mmap(NULL, fb_size, PROT_READ | PROT_WRITE,
		      MAP_SHARED, fb_fd, 0);
	if (fb_ptr == MAP_FAILED) {
		perror("mmap framebuffer");
		goto err_close;
	}

	printf("framebuffer: %ux%u %ubpp line_length=%u\n",
	       vinfo.xres, vinfo.yres,
	       vinfo.bits_per_pixel, finfo.line_length);
#endif

	for (int i = 0; i < 10000000; ++i) {
		sdk_image_buf_t *buf = sdk_recv_image(h, 120);

		if (buf == NULL) {
			pr_info("timeout or network error\n");
			continue;
		}

		print_beijing_time(buf->timestamp);
		print_beijing_time(buf->img_timestamp);
		uint8_t *telemetry_data = buf->reserved_data + 160 * 2 * 2;
		pr_info("major:%d, minor:%d\n", telemetry_data[1], telemetry_data[0]);

		uint32_t time_counter = ((uint32_t)telemetry_data[4] << 24) |
			((uint32_t)telemetry_data[5] << 16) |
			((uint32_t)telemetry_data[2] << 8)  |
			(uint32_t)telemetry_data[3];
		pr_info("time counter:%u\n", time_counter);

		telemetry_data += 40;
		uint32_t frame_counter = ((uint32_t)telemetry_data[2] << 24) |
			((uint32_t)telemetry_data[3] << 16) |
			((uint32_t)telemetry_data[0] << 8)  |
			(uint32_t)telemetry_data[1];
		pr_info("frame counter:%u\n", frame_counter);
#if defined (__linux__)
		if (rgb_buf == NULL)
			rgb_buf = malloc(buf->width * buf->height * sizeof(uint32_t));
		lepton_fb_display(fb_ptr, &vinfo, &finfo, (uint16_t *)buf->pixel_data, buf->width, buf->height, rgb_buf);
#endif
		sdk_release_image(h, buf);
	}

#if defined (__linux__)
	munmap(fb_ptr, fb_size);
err_close:
	close(fb_fd);
err_free:
	if (rgb_buf)
		free(rgb_buf);
	rgb_buf = NULL;
#endif

	// /* disable vsync signal, and the image streaming will stop */
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
