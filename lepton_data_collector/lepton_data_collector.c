/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see https://linuxtv.org/docs.php for more information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>             /* getopt_long() */
#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <linux/videodev2.h>
#include "lepton_vospi_funcs.h"
#include "shmq.h"
#include "log.h"
#include "sdk_image.h"

#define SHMQ_DEV "/dev/shmq"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

enum io_method {
        IO_METHOD_READ,
        IO_METHOD_MMAP,
        IO_METHOD_USERPTR,
};

struct buffer {
        void   *start;
        size_t  length;
};

static lepton_version   lep_version = LEPTON_VERSION_2X;
static char            *dev_name;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static int              to_file;
static char            *out_file_prefix = NULL;
static int              force_format;
static int              frame_count = 70;
static int		to_shmq = 0;
static telemetry_location telemetry_loc = TELEMETRY_OFF;

static lepton_vospi_info lep_info;
static int              frame_number = 0;
static unsigned short	*pixel_data = NULL;
static int		fd_q;
static uint32_t		qid;
static uint8_t		*pool;
static uint32_t		sz_buf;
static uint32_t		img_w;
static uint32_t		img_h;

static void sig_proc(int signo)
{
	to_shmq = 0;
	to_file = 0;
}

static void errno_exit(const char *s)
{
        pr_err("%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
        int r;

        do {
                r = ioctl(fh, request, arg);
        } while (-1 == r && EINTR == errno);

        return r;
}

static uint64_t timestamp_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

typedef uint8_t pixel_size_arry[PIXEL_SIZE_MAP_LEN];

static pixel_size_arry fmt_to_pixel_map = {
	3, /* SDK_PIX_FMT_RGB */
	2, /* SDK_PIX_FMT_Y16 */
	2, /* SDK_PIX_FMT_X16 */
	1  /* SDK_PIX_FMT_Y8  */
};

static uint8_t fmt_to_pixel_size(pixel_size_arry map, sdk_pixel_fmt_t fmt)
{
	return map[fmt];
}

static uint8_t max_pixel_size(pixel_size_arry map, int map_len)
{
	uint8_t max;

	if (map == NULL || map_len <= 0) {
		return 0;
	}

	max = map[0];

	for (int i = 1; i < map_len; i++) {
		if (map[i] > max)
			max = map[i];
	}

	return max;
}

static void process_image(const void *p, int size)
{
	FILE *out_file = NULL;
	char out_path[256];
	int done = 0;
	int lc_errs = 0;
	int ret;
	struct shmq_buf_desc d = { .queue_id = qid };

	if (is_subframe_index_valid(&lep_info, (unsigned short *)p) == 0)
		return;

	lc_errs = extract_pixel_data(&lep_info, (unsigned short *)p, pixel_data, &done);
	if (done != 1)
		return;

	/* if true, save image data to file */
	if (to_file && frame_number < frame_count)
	{
		/* Either a Lepton 2.X frame was received, or
		 * the last subframe of a Lepton 3.X frame was
		 * received.
		 */
		snprintf(out_path, sizeof(out_path), "%s%06d.gray", out_file_prefix,
				frame_number);
		out_file = fopen(out_path, "wb");
		if (out_file) {
			fwrite(pixel_data, sizeof(unsigned short),
					lep_info.image_params.pixel_width*lep_info.image_params.pixel_height,
					out_file);
			fclose(out_file);
			/* image stored */
			fflush(stdout);
			pr_err("save %s\n", out_path);
		}
		else {
			/* failed to store image frame */
			fflush(stderr);
			pr_err("failed save %s\n", out_path);
		}
		frame_number++;
	} else {
		to_file = 0;
	}

	if (to_shmq) {
		sdk_pixel_fmt_t	fmt	= SDK_PIX_FMT_Y16;
		uint8_t bpp		= fmt_to_pixel_size(fmt_to_pixel_map, fmt);
		size_t  reserved_len	= (size_t)img_w * bpp * RESERVED_LINES;
		size_t  pixels_len	= (size_t)img_w * img_h * bpp;
		size_t  pixel_off	= HDR_LEN + reserved_len;
		size_t  total		= HDR_LEN + reserved_len + pixels_len;
		uint8_t	*buf;

		ret = ioctl(fd_q, SHMQ_IOC_GET_FREE, &d);
		if(ret < 0) {
			pr_info("SHMQ_IOC_GET_FREE failed, errno:%d\n", errno);
			return;
		}

		buf = pool + d.offset;
		buf[0] = (uint8_t)(img_w >> 8);
		buf[1] = (uint8_t)(img_w);
		buf[2] = (uint8_t)(img_h >> 8);
		buf[3] = (uint8_t)(img_h);
		buf[4] = bpp;
		buf[5] = fmt;
		*(uint64_t *)(buf + TIMESTAMP_OFF) = timestamp_us();

		memcpy(buf + pixel_off, pixel_data, total);
		d.data_size = total;

		ret = ioctl(fd_q, SHMQ_IOC_ENQUEUE, &d);
		if(ret < 0) {
			pr_info("SHMQ_IOC_ENQUEUE failed, errno:%d\n", errno);
			return;
		}
	}
}

static int read_frame(void)
{
        struct v4l2_buffer buf;
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                if (-1 == read(fd, buffers[0].start, buffers[0].length)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit("read");
                        }
                }

                process_image(buffers[0].start, buffers[0].length);
                break;

        case IO_METHOD_MMAP:
                CLEAR(buf);

                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;

                if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit("VIDIOC_DQBUF");
                        }
                }
                if (buf.flags & V4L2_BUF_FLAG_ERROR) {
                        /* RLC - in case of data corruption, skip this buffer
                        * and move on to the next.
                        */
                        fflush(stderr);
                        pr_err("!");
                        fflush(stdout);
                        return 0;
                }

                assert(buf.index < n_buffers);

                process_image(buffers[buf.index].start, buf.bytesused);

                if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                        errno_exit("VIDIOC_QBUF");
                break;

        case IO_METHOD_USERPTR:
                CLEAR(buf);

                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_USERPTR;

                if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit("VIDIOC_DQBUF");
                        }
                }

                for (i = 0; i < n_buffers; ++i)
                        if (buf.m.userptr == (unsigned long)buffers[i].start
                            && buf.length == buffers[i].length)
                                break;

                assert(i < n_buffers);

                process_image((void *)buf.m.userptr, buf.bytesused);

                if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                        errno_exit("VIDIOC_QBUF");
                break;
        }

        return 1;
}

static void mainloop(void)
{
	for (;to_file || to_shmq ;) {
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(fd + 1, &fds, NULL, NULL, &tv);

		if (-1 == r) {
			if (EINTR == errno)
				continue;
			errno_exit("select");
		}

		if (0 == r)
			pr_err("select timeout\n");

		read_frame();
	}
}

static void stop_capturing(void)
{
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
                        errno_exit("VIDIOC_STREAMOFF");
                break;
        }
}

static void start_capturing(void)
{
        unsigned int i;
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_MMAP;
                        buf.index = i;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
                        errno_exit("VIDIOC_STREAMON");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_USERPTR;
                        buf.index = i;
                        buf.m.userptr = (unsigned long)buffers[i].start;
                        buf.length = buffers[i].length;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
                        errno_exit("VIDIOC_STREAMON");
                break;
        }
}

static void uninit_device(void)
{
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                free(buffers[0].start);
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i)
                        if (-1 == munmap(buffers[i].start, buffers[i].length))
                                errno_exit("munmap");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i)
                        free(buffers[i].start);
                break;
        }

        free(buffers);
}

static void init_read(unsigned int buffer_size)
{
        buffers = calloc(1, sizeof(*buffers));

        if (!buffers) {
                pr_err("Out of memory\n");
                exit(EXIT_FAILURE);
        }

        buffers[0].length = buffer_size;
        buffers[0].start = malloc(buffer_size);

        if (!buffers[0].start) {
                pr_err("Out of memory\n");
                exit(EXIT_FAILURE);
        }
}

static void init_mmap(void)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        pr_err("%s does not support "
                                 "memory mapping\n", dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        if (req.count < 2) {
                pr_err("Insufficient buffer memory on %s\n",
                         dev_name);
                exit(EXIT_FAILURE);
        }

        buffers = calloc(req.count, sizeof(*buffers));

        if (!buffers) {
                pr_err("Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                struct v4l2_buffer buf;

                CLEAR(buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.index       = n_buffers;

                if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit("VIDIOC_QUERYBUF");

                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start =
                        mmap(NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              fd, buf.m.offset);

                if (MAP_FAILED == buffers[n_buffers].start)
                        errno_exit("mmap");
        }
}

static void init_userp(unsigned int buffer_size)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        pr_err("%s does not support "
                                 "user pointer i/o\n", dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        buffers = calloc(4, sizeof(*buffers));

        if (!buffers) {
                pr_err("Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
                buffers[n_buffers].length = buffer_size;
                buffers[n_buffers].start = malloc(buffer_size);

                if (!buffers[n_buffers].start) {
                        pr_err("Out of memory\n");
                        exit(EXIT_FAILURE);
                }
        }
}

static void init_device(void)
{
        struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
        unsigned int min;

        if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
                if (EINVAL == errno) {
                        pr_err("%s is no V4L2 device\n",
                                 dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_QUERYCAP");
                }
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                pr_err("%s is no video capture device\n",
                         dev_name);
                exit(EXIT_FAILURE);
        }

        switch (io) {
        case IO_METHOD_READ:
                if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
                        pr_err("%s does not support read i/o\n",
                                 dev_name);
                        exit(EXIT_FAILURE);
                }
                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
                        pr_err("%s does not support streaming i/o\n",
                                 dev_name);
                        exit(EXIT_FAILURE);
                }
                break;
        }


        /* Select video input, video standard and tune here. */


        CLEAR(cropcap);

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

                if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
                        switch (errno) {
                        case EINVAL:
                                /* Cropping not supported. */
                                break;
                        default:
                                /* Errors ignored. */
                                break;
                        }
                }
        } else {
                /* Errors ignored. */
        }


        CLEAR(fmt);

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (force_format) {
                fmt.fmt.pix.width       = 640;
                fmt.fmt.pix.height      = 480;
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
                fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

                if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
                        errno_exit("VIDIOC_S_FMT");

                /* Note VIDIOC_S_FMT may change width and height. */
        } else {
                /* Preserve original settings as set by v4l2-ctl for example */
                if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
                        errno_exit("VIDIOC_G_FMT");
        }

        /* Buggy driver paranoia. */
        min = fmt.fmt.pix.width * 2;
        if (fmt.fmt.pix.bytesperline < min)
                fmt.fmt.pix.bytesperline = min;
        min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
        if (fmt.fmt.pix.sizeimage < min)
                fmt.fmt.pix.sizeimage = min;

        switch (io) {
        case IO_METHOD_READ:
                init_read(fmt.fmt.pix.sizeimage);
                break;

        case IO_METHOD_MMAP:
                init_mmap();
                break;

        case IO_METHOD_USERPTR:
                init_userp(fmt.fmt.pix.sizeimage);
                break;
        }
}

static void close_device(void)
{
        if (-1 == close(fd))
                errno_exit("close");

        fd = -1;
}

static void open_device(void)
{
        struct stat st;

        if (-1 == stat(dev_name, &st)) {
                pr_err("Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (!S_ISCHR(st.st_mode)) {
                pr_err("%s is no device\n", dev_name);
                exit(EXIT_FAILURE);
        }

        fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == fd) {
                pr_err("Cannot open '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }
}

static void usage(FILE *fp, int argc, char **argv)
{
        fprintf(fp,
                 "Usage: %s [options]\n\n"
                 "Version 1.3\n"
                 "Options:\n"
                 "-2 | --lepton2       Lepton 2.X selected\n"
                 "-3 | --lepton3       Lepton 3.X selected\n"
                 "-d | --device name   Video device name [%s]\n"
                 "-h | --help          Print this message\n"
                 "-m | --mmap          Use memory mapped buffers [default]\n"
                 "-r | --read          Use read() calls\n"
                 "-u | --userp         Use application allocated buffers\n"
                 "-o | --output prefix Outputs frames to <filename prefix><frame_no> files\n"
                 "-f | --format        Force format to 640x480 YUYV\n"
                 "-c | --count         Number of frames to grab [%i]\n"
                 "-t | --telemetry     Telemetry location: one of 'off', 'start', or 'end'\n"
                 "-s | --share	       Direct image stream to share memory queue\n"
                 "",
                 argv[0], dev_name, frame_count);
}

static const char short_options[] = "23d:hmruo:fc:t:s";

static const struct option
long_options[] = {
        { "lepton2", no_argument,      NULL, '2' },
        { "lepton3", no_argument,      NULL, '3' },
        { "device", required_argument, NULL, 'd' },
        { "help",   no_argument,       NULL, 'h' },
        { "mmap",   no_argument,       NULL, 'm' },
        { "read",   no_argument,       NULL, 'r' },
        { "userp",  no_argument,       NULL, 'u' },
        { "output", required_argument, NULL, 'o' },
        { "format", no_argument,       NULL, 'f' },
        { "count",  required_argument, NULL, 'c' },
        { "telemetry", required_argument, NULL, 't' },
        { "share", required_argument,  NULL, 's' },
        { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
        int lepton_version_arg_found = 0;
	size_t pool_sz;

        dev_name = "/dev/video0";
	signal(SIGINT, sig_proc);
	signal(SIGTERM, sig_proc);

        for (;;) {
                FILE *test_f;
                int idx;
                int c;

                c = getopt_long(argc, argv,
                                short_options, long_options, &idx);

                if (-1 == c)
                        break;

                switch (c) {
                case 0: /* getopt_long() flag */
                        break;

                case '2':
                        lepton_version_arg_found++;
                        lep_version = LEPTON_VERSION_2X;
			img_w = LEPTON_SUBFRAME_LINE_PIXEL_WIDTH;
			img_h = LEPTON_SUBFRAME_DATA_LINE_HEIGHT;
                        break;

                case '3':
                        lepton_version_arg_found++;
                        lep_version = LEPTON_VERSION_3X;
			img_w = LEPTON_SUBFRAME_LINE_PIXEL_WIDTH * 2;
			img_h = LEPTON_SUBFRAME_DATA_LINE_HEIGHT * 2;
                        break;

                case 'd':
                        dev_name = optarg;
                        break;

                case 'h':
                        usage(stdout, argc, argv);
                        exit(EXIT_SUCCESS);

                case 'm':
                        io = IO_METHOD_MMAP;
                        break;

                case 'r':
                        io = IO_METHOD_READ;
                        break;

                case 'u':
                        io = IO_METHOD_USERPTR;
                        break;

                case 'o':
                        to_file++;
                        out_file_prefix = optarg;
                        /* make sure the prefix itself is writable */
                        test_f = fopen(out_file_prefix, "wb");
                        if (!test_f) {
                                pr_err("Cannot open file name prefix '%s' for writing.\n", optarg);
                                exit(1);
                        }
                        fclose(test_f);
                        /* delete the test file */
                        unlink(out_file_prefix);
                        break;

                case 'f':
                        force_format++;
                        break;

                case 'c':
                        errno = 0;
                        frame_count = strtol(optarg, NULL, 0);
                        if (errno)
                                errno_exit(optarg);
                        break;

                case 't':
                        if (strncmp("start", optarg, 5) == 0) {
                                telemetry_loc = TELEMETRY_AT_START;
                        }
                        else if (strncmp("end", optarg, 3) == 0) {
                                telemetry_loc = TELEMETRY_AT_END;
                        }
                        else if (strncmp("off", optarg, 3) == 0) {
                                telemetry_loc = TELEMETRY_OFF;
                        }
                        else {
                                pr_err("Unknown telemetry location '%s'\n", optarg);
                                exit(1);
                        }
                        break;

                case 's':
			to_shmq = 1;
                        break;

                default:
                        usage(stderr, argc, argv);
                        exit(EXIT_FAILURE);
                }
        }

        if (lepton_version_arg_found > 1) {
                pr_warn("Warning: Multiple lepton version command-line args found. The last setting will be used.\n");
        }
        pr_info("Collecting frames from Lepton %d.X \n", (int)lep_version);
        if (telemetry_loc == TELEMETRY_OFF) {
                pr_info("telemetry off\n");
        }
        else if (telemetry_loc == TELEMETRY_AT_END) {
                pr_info("telemetry at end of subframe data\n");
        }
        else if (telemetry_loc == TELEMETRY_AT_START) {
                pr_info("telemetry at start of subframe data\n");
        }
        init_lepton_info(&lep_info, lep_version, telemetry_loc);
        pixel_data = (unsigned short *)calloc(lep_info.image_params.pixel_width*lep_info.image_params.pixel_height,
                sizeof(unsigned short));
        if (!pixel_data) {
                errno_exit("Cannot allocate pixel buffer");
        }

	fd_q = shmq_open_dev();
	sz_buf = RESERVED_OFF + img_w * (img_h + RESERVED_LINES) *
		max_pixel_size(fmt_to_pixel_map, PIXEL_SIZE_MAP_LEN);
	qid = shmq_create_queue(fd_q, "lpt_img_shmq", 8, sz_buf);
	shmq_set_timeout(fd_q, qid, 500);
	pool = shmq_map_queue(fd_q, qid, &pool_sz);
	pr_info("pool size:%ld\n", pool_sz);
        open_device();
        init_device();
        start_capturing();
        mainloop();
        stop_capturing();
        uninit_device();
        close_device();
        free(pixel_data);
	shmq_munmap_queue(pool, pool_sz);
	shmq_destroy_queue(fd_q, qid);
	shmq_close_dev(fd_q);
        pr_info("exit lepton_data_collector\n");

        return 0;
}
