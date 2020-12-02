#include <stdio.h>
#include <string.h>

static const char unreserved[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~";

int main(int argc, char *argv[])
{
	char *str;
	int j;

	for (j = 1; j < argc; ++j) {
		for (str = argv[j]; *str; ++str) {
			if (strchr(unreserved, *str))
				fputc(*str, stdout);
			else
				printf("%%%02x", *str & 0xff);
		}
		printf("\n");
	}
	return 0;
}
