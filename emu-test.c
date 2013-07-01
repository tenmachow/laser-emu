#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>



int main(int argc, char *argv[])
{
	int fd;
	char *buffer;
	size_t bufsiz;
	ssize_t ret;

	if (argc < 2) {
		printf("Usage: %s <slave pts>\n", argv[0]);
		exit(1);
	}
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", argv[1], strerror(errno));
		exit(1);
	}

	buffer = malloc(BUFSIZ);
	if (buffer == NULL) {
		fprintf(stderr, "malloc failed: %s\n", strerror(errno));
		exit(2);
	}
	bufsiz = BUFSIZ;

	write(fd, "DX\r\n", 4);
	while (1) {
		ret = read(fd, buffer, bufsiz);
		if (ret > 0)
			write(STDOUT_FILENO, buffer, ret);
	}
#if 0
	while (1) {
		ret = getline(&buffer, &bufsiz, stdin);
		if (ret < 0) {
			break;
		}
		write(fd, buffer, ret);
		ret = read(fd, buffer, bufsiz);
		buffer[ret] = 0;
		printf("read %d bytes: %s\n", ret, buffer);
	}
#endif
	free(buffer);

	return 0;
}
