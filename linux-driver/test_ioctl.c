/*
 * Simple test program for RawAccel IOCTL interface
 * Compile: gcc -o test_ioctl test_ioctl.c
 * Run: ./test_ioctl
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

/* IOCTL magic number */
#define RAWACCEL_IOC_MAGIC 0x88

/* Version structure */
struct rawaccel_version {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint8_t reserved;
};

/* IOCTL command */
#define RAWACCEL_IOC_GET_VERSION \
	_IOR(RAWACCEL_IOC_MAGIC, 0, struct rawaccel_version)

int main(void)
{
	int fd;
	struct rawaccel_version version;
	int ret;

	printf("RawAccel IOCTL Test Program\n");
	printf("============================\n\n");

	/* Open /dev/rawaccel */
	fd = open("/dev/rawaccel", O_RDWR);
	if (fd < 0) {
		perror("Failed to open /dev/rawaccel");
		printf("\nMake sure the kernel module is loaded:\n");
		printf("  sudo insmod rawaccel.ko\n");
		return 1;
	}
	printf("✓ Successfully opened /dev/rawaccel\n");

	/* Test GET_VERSION IOCTL */
	ret = ioctl(fd, RAWACCEL_IOC_GET_VERSION, &version);
	if (ret < 0) {
		perror("IOCTL GET_VERSION failed");
		close(fd);
		return 1;
	}
	printf("✓ GET_VERSION IOCTL succeeded\n");
	printf("  Driver version: %u.%u.%u\n",
	       version.major, version.minor, version.patch);

	close(fd);
	printf("\nAll tests passed!\n");
	return 0;
}
