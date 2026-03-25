#define _POSIX_C_SOURCE 200809L
#include "lepton_i2c.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define LEPTON_DEFAULT_ADDR 0x2A

int lepton_i2c_open(const char *dev, lepton_i2c_t *ctx)
{
	if (!dev || !ctx) {
		errno = EINVAL;
		return -1;
	}

	ctx->fd = open(dev, O_RDWR);
	if (ctx->fd < 0)
		return -1;

	ctx->addr = LEPTON_DEFAULT_ADDR;

	if (ioctl(ctx->fd, I2C_SLAVE, ctx->addr) < 0) {
		close(ctx->fd);
		ctx->fd = -1;
		return -1;
	}

	return 0;
}

void lepton_i2c_close(lepton_i2c_t *ctx)
{
	if (ctx && ctx->fd >= 0) {
		close(ctx->fd);
		ctx->fd = -1;
	}
}

int lepton_i2c_write_reg(const lepton_i2c_t *ctx, uint16_t reg, uint16_t val)
{
	uint8_t buf[4] = {
		(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
		(uint8_t)(val >> 8), (uint8_t)(val & 0xFF)
	};

	if (write(ctx->fd, buf, 4) != 4)
		return -1;

	return 0;
}

int lepton_i2c_read_reg(const lepton_i2c_t *ctx, uint16_t reg, uint16_t *out)
{
	uint8_t addr_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
	uint8_t data_buf[2];
	struct i2c_msg msgs[2] = {
		{ .addr = ctx->addr, .flags = 0,        .len = 2, .buf = addr_buf },
		{ .addr = ctx->addr, .flags = I2C_M_RD, .len = 2, .buf = data_buf },
	};
	struct i2c_rdwr_ioctl_data rdwr = { .msgs = msgs, .nmsgs = 2 };

	if (ioctl(ctx->fd, I2C_RDWR, &rdwr) < 0)
		return -1;

	*out = ((uint16_t)data_buf[0] << 8) | data_buf[1];
	return 0;
}

int lepton_i2c_write_words(const lepton_i2c_t *ctx, uint16_t start_reg,
		const uint16_t *words, int n)
{
	for (int i = 0; i < n; i++) {
		if (lepton_i2c_write_reg(ctx, (uint16_t)(start_reg + i * 2), words[i]) < 0)
			return -1;
	}

	return 0;
}

int lepton_i2c_read_words(const lepton_i2c_t *ctx, uint16_t start_reg,
		uint16_t *buf, int n)
{
	for (int i = 0; i < n; i++) {
		if (lepton_i2c_read_reg(ctx, (uint16_t)(start_reg + i * 2), &buf[i]) < 0)
			return -1;
	}

	return 0;
}
