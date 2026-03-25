#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include "lepton_i2c.h"
#include "shmq.h"

enum {
	I2C_READ,
	I2C_WRITE,
};

static int main_run;

static void sig_proc(int signo)
{
	main_run = 0;
}

int main(int argc, char *argv[])
{
	const char *dev;
	lepton_i2c_t ctx;
	int ret;
	int shmq_fd;
	struct shmq_buf_desc rx_desc;
	struct shmq_buf_desc tx_desc;
	uint8_t	*rx_pool;
	uint8_t	*tx_pool;
	int rx_qid;
	int tx_qid;
	size_t rx_pool_sz;
	size_t tx_pool_sz;

	signal(SIGINT, sig_proc);
	signal(SIGTERM, sig_proc);

	if (argc == 1) {
		printf("usage:%s /dev/i2c-X\n", argv[0]);
		return -1;
	}

	dev = argv[1];

	ret = lepton_i2c_open(dev, &ctx);
	if (ret < 0) {
		fprintf(stderr, "lepton_i2c_open failed, ret:%d\n", ret);
		perror("open:");
		return -1;
	}

	shmq_fd = shmq_open_dev();
	rx_qid = shmq_create_queue(shmq_fd, "lpt_cmd_in", 4, 1024);
	tx_qid = shmq_create_queue(shmq_fd, "lpt_cmd_out", 4, 1024);
	rx_desc.queue_id = rx_qid;
	tx_desc.queue_id = tx_qid;
	shmq_set_timeout(shmq_fd, rx_qid, 500);
	shmq_set_timeout(shmq_fd, tx_qid, 500);
	rx_pool = shmq_map_queue(shmq_fd, rx_qid, &rx_pool_sz);
	tx_pool = shmq_map_queue(shmq_fd, tx_qid, &tx_pool_sz);
	fprintf(stdout, "rx_pool size:%ld\n", rx_pool_sz);
	fprintf(stdout, "tx_pool size:%ld\n", tx_pool_sz);

	main_run = 1;
	while(main_run) {
		uint8_t *buf;
		int direction;
		uint16_t reg_addr;
		uint16_t *rx_words;
		const uint16_t *tx_words;
		int n_words;
		int ret_code;
		uint8_t *ack_buf;
		int ack_len;

		/* request */
		ret = ioctl(shmq_fd, SHMQ_IOC_DEQUEUE, &rx_desc);
		if (ret < 0) {
			fprintf(stderr, "SHMQ_IOC_DEQUEUE failed, ret:%d\n", ret);
			continue;
		}

		buf = rx_pool + rx_desc.offset;
		direction = buf[0];
		reg_addr = buf[1] | ((buf[2] << 8) & 0xff00);
		n_words = buf[3] | ((buf[4] << 8) & 0xff00);

#define PAYLOAD_OFFSET 5
#define SZ_WACK_RET_CODE 4
#define SZ_WORD sizeof(short)

		if (direction == I2C_WRITE) {
			ack_len = PAYLOAD_OFFSET + SZ_WACK_RET_CODE;
			ack_buf = (uint8_t *)malloc(ack_len);
			tx_words = (const uint16_t *)&buf[PAYLOAD_OFFSET];
			ret |= lepton_i2c_write_words(&ctx, reg_addr, tx_words, n_words);
		} else {
			ack_len = PAYLOAD_OFFSET + n_words * SZ_WORD;
			ack_buf = (uint8_t *)malloc(ack_len);
			rx_words = (uint16_t *)(ack_buf + PAYLOAD_OFFSET);
			ret |= lepton_i2c_read_words(&ctx, reg_addr, rx_words, n_words);
		}

		if (ret != 0) {
			fprintf(stderr, "i2c R/W failed, ret:%d\n", ret);
			ret_code = -1;
		}

		ioctl(shmq_fd, SHMQ_IOC_RELEASE, &rx_desc);

response:
		/* response ACK */
		ret = ioctl(shmq_fd, SHMQ_IOC_GET_FREE, &tx_desc);
		if (ret < 0) {
			fprintf(stderr, "SHMQ_IOC_GET_FREE failed, ret:%d\n", ret);
			goto response;
		}

		ack_buf[1] = reg_addr & 0x00ff;
		ack_buf[2] = (reg_addr >> 8) & 0x00ff;
		ack_buf[3] = n_words & 0x00ff;
		ack_buf[4] = (n_words >> 8) & 0x00ff;
		if (direction == I2C_WRITE) {
			ack_buf[0] = I2C_WRITE;
			memcpy(&ack_buf[PAYLOAD_OFFSET], &ret_code, sizeof(ret_code));
		} else {
			ack_buf[0] = I2C_READ;
		}

		buf = tx_pool + tx_desc.offset;
		memcpy(buf, ack_buf, ack_len);
		tx_desc.data_size = ack_len;

		ret = ioctl(shmq_fd, SHMQ_IOC_ENQUEUE, &tx_desc);
		if (ret < 0)
			fprintf(stderr, "SHMQ_IOC_ENQUEUE failed, ret:%d\n", ret);

		free(ack_buf);
	}

	shmq_munmap_queue(rx_pool, rx_pool_sz);
	shmq_munmap_queue(tx_pool, tx_pool_sz);
	shmq_destroy_queue(shmq_fd, rx_qid);
	shmq_destroy_queue(shmq_fd, tx_qid);
	shmq_close_dev(shmq_fd);
	shmq_close_dev(shmq_fd);

	lepton_i2c_close(&ctx);
	return 0;
}
