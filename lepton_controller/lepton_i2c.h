/* Must appear before any system header to enable POSIX extensions (usleep etc.)
 * under -std=c99.  Safe to define here because lepton_i2c.h is the root of
 * the entire include tree for this SDK. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

/*
 * lepton_i2c.h  —  Lepton CCI I2C 底层接口
 *
 * 所有寄存器均为 16-bit 大端（Big-Endian）。
 * Lepton CCI I2C 地址: 0x2A
 */

#ifndef LEPTON_I2C_H
#define LEPTON_I2C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   fd;           /* Linux /dev/i2c-N 文件描述符 */
    uint8_t addr;       /* I2C 从机地址，默认 0x2A     */
} lepton_i2c_t;

/**
 * lepton_i2c_open - 打开 I2C 总线并绑定 Lepton 地址
 * @dev:  设备路径，如 "/dev/i2c-2"
 * @ctx:  输出句柄
 * 返回 0 成功，负值失败（errno 已设置）
 */
int lepton_i2c_open(const char *dev, lepton_i2c_t *ctx);

/**
 * lepton_i2c_close - 关闭 I2C 设备
 */
void lepton_i2c_close(lepton_i2c_t *ctx);

/**
 * lepton_i2c_write_reg - 写单个 16-bit 寄存器
 * @reg:  寄存器地址（16-bit）
 * @val:  写入值（16-bit）
 */
int lepton_i2c_write_reg(const lepton_i2c_t *ctx, uint16_t reg, uint16_t val);

/**
 * lepton_i2c_read_reg - 读单个 16-bit 寄存器
 * @reg:  寄存器地址（16-bit）
 * @out:  读出值（输出参数）
 */
int lepton_i2c_read_reg(const lepton_i2c_t *ctx, uint16_t reg, uint16_t *out);

/**
 * lepton_i2c_write_words - 连续写入 n 个 16-bit 字（地址自增）
 * @start_reg:  起始寄存器地址
 * @words:      数据缓冲区
 * @n:          字数
 */
int lepton_i2c_write_words(const lepton_i2c_t *ctx,
			   uint16_t start_reg,
			   const uint16_t *words, int n);

/**
 * lepton_i2c_read_words - 连续读取 n 个 16-bit 字（地址自增）
 * @start_reg:  起始寄存器地址
 * @buf:        输出缓冲区
 * @n:          字数
 */
int lepton_i2c_read_words(const lepton_i2c_t *ctx,
			  uint16_t start_reg,
			  uint16_t *buf, int n);

#ifdef __cplusplus
}
#endif

#endif /* LEPTON_I2C_H */
