#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "shmq.h"
#include "log.h"

#define SHMQ_DEV "/dev/shmq"

int shmq_open_dev(void)
{
	int fd = open(SHMQ_DEV, O_RDWR);

	if (fd < 0) {
		perror("open " SHMQ_DEV);
		exit(1);
	}

	return fd;
}

int shmq_close_dev(int fd)
{
	close(fd);
}

uint32_t shmq_create_queue(int fd, const char *name, uint32_t bufs, uint32_t buf_sz)
{
	struct shmq_create c = { .buf_count = bufs, .buf_size = buf_sz };

	strncpy(c.name, name, 31);
	if (ioctl(fd, SHMQ_IOC_CREATE, &c) < 0) {
		pr_err("CREATE '%s' failed: %s\n", name, strerror(errno));
		exit(1);
	}

	return c.queue_id;
}

void shmq_set_timeout(int fd, uint32_t qid, uint32_t ms)
{
	struct shmq_timeout t = { .queue_id = qid, .timeout_ms = ms };

	ioctl(fd, SHMQ_IOC_SET_TIMEOUT, &t);
}

uint8_t *shmq_map_queue(int fd, uint32_t qid, size_t *out_sz)
{
	struct shmq_mmap_info mi = { .queue_id = qid };

	if (ioctl(fd, SHMQ_IOC_MMAP_INFO, &mi) < 0) {
		perror("MMAP_INFO");
		exit(1);
	}

	if (ioctl(fd, SHMQ_IOC_MMAP_SELECT, &qid) < 0) {
		perror("MMAP_SELECT");
		exit(1);
	}

	void *p = mmap(NULL, mi.total_size, PROT_READ|PROT_WRITE,
			MAP_SHARED, fd, 0);

	if (p == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	*out_sz = mi.total_size;
	return p;
}

int shmq_munmap_queue(void *addr, size_t length)
{
	return munmap(addr, length);
}

int shmq_flush(int fd, struct shmq_queue_id *qid)
{
	return ioctl(fd, SHMQ_IOC_FLUSH, qid);
}

void shmq_destroy_queue(int fd, uint32_t qid)
{
	struct shmq_queue_id qi = { .queue_id = qid };

	ioctl(fd, SHMQ_IOC_DESTROY, &qi);
}
