#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <unistd.h>
#include <getopt.h>
#include <error.h>
#include <sys/stat.h>

#define NAME	"cgiformhelper"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": split cgi form-data in files\n"
	"Usage: " NAME " [TEMPDIR]\n"
	"\n"
	"multipart/form-data is the expected content-type on stdin\n"
	"TEMPDIR does not need to exist.\n"
	"When TEMPDIR is absent, /tmp/cgi-<parentpid> is used.\n"
	"\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "+?V:";

char buf[2*1024];

/* compare haystack: if it begins with needle, return the equal size,
   otherwise 0 */
static inline int strstart(const char *haystack, const char *needle)
{
	int len = strlen(needle);

	return strncmp(haystack ?: "", needle, len) ? 0 : len;
}

/* when str is a 'key=val' pair, put null terminator on key & return val */
static const char *getval(char *str)
{
	char *p;

	p = strchr(str, '=');
	if (!p)
		return NULL;
	*p++ = 0;
	if (strchr("\"'", *p) && (p[strlen(p)-1] == *p)) {
		/* strip surrounding " */
		++p;
		p[strlen(p)-1] = 0;
	}
	return p;
}

static const char *memstr(const char *haystack, const char *needle, int len)
{
	int nlen = strlen(needle);

	for (; len >= nlen; ++haystack, --len) {
		if (!strncmp(haystack, needle, nlen))
			return haystack;
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	int opt, state, ret;
	const char *conttype, *needle, *val;
	char *tempdir, *boundary, *eboundary, *endp, *tok;
	int fill, boundarylen;
	FILE *fpout;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, VERSION);
		return 0;

	default:
		fprintf(stderr, "unknown option '%c'", opt);
	case '?':
		fputs(help_msg, stderr);
		exit(1);
		break;
	}

	tempdir = argv[optind];
	if (!tempdir)
		asprintf(&tempdir, "/tmp/cgi-%i", getppid());

	if (mkdir(tempdir, 0777) < 0) {
		if (errno != EEXIST)
			error(1, errno, "mkdir %s", tempdir);
	}
	if (chdir(tempdir) < 0)
		error(1, errno, "chdir %s", tempdir);

	/* test content-type */
	conttype = getenv("CONTENT_TYPE");
	ret = strstart(conttype, "multipart/form-data; boundary=");
	if (!ret)
		error(1, 0, "wrong content type: %s", conttype);
	/* prepend -- before boundary */
	asprintf(&boundary, "\r\n--%s\r\n", conttype + ret);
	asprintf(&eboundary, "\r\n--%s--\r\n", conttype + ret);
	boundarylen = strlen(boundary);

	/* start parsing */
	fill = 0;
	state = 0; /* 0 is initial, 1 is header, 2 is contents */
	while (1) {
		ret = fread(buf + fill, 1, sizeof(buf) - fill, stdin);
		if (ret <= 0) {
			if (feof(stdin) && !fill)
				break;
			if (ferror(stdin))
				error(1, errno, "read stdin");
		}
		if (ret > 0)
			fill += ret;
		if (state == 0) {
			/* find the first boundary, without prefixed \r\n */
			needle = strstr(buf, boundary + 2);
			if (!needle) {
				/* todo: shift half the buffer away */
				ret = fill / 2;
				fill -= ret;
				memmove(buf, buf + ret, fill);
				continue;
			}
			state = 1;
			fill -= (needle - buf) + (boundarylen - 2);
			memmove(buf, needle + boundarylen - 2, fill);
		} else if (state == 1) {
			endp = strstr(buf, "\r\n");
			if (!endp) {
				if (fill == sizeof(buf))
					error(1, 0, "buffer full with no line");
				continue;
			}
			*endp = 0;
			endp += 2;
			if (!strlen(buf)) {
				state = 2;
			} else if ((ret = strstart(buf, "Content-Disposition: form-data; ")) > 0) {
				for (tok = strtok(buf + ret, "; \t"); tok;
						tok = strtok(NULL, "; \t")) {
					val = getval(tok);
					if (!strcmp(tok, "name")) {
						fpout = fopen(val, "w");
						if (!fpout)
							error(1, errno, "fopen %s", val);
						break;
					}
				}
			}
			fill -= endp - buf;
			memmove(buf, endp, fill);
		} else if (state == 2) {
			needle = memstr(buf, boundary, fill);
			/* test for terminator */
			if (!needle && fill < sizeof(buf))
				needle = memstr(buf, eboundary, fill);
			if (needle) {
				/* close file with leftover from buffer */
				ret = needle - buf;
				if (fpout) {
					fwrite(buf, 1, ret, fpout);
					fclose(fpout);
					fpout = NULL;
				}
				fill -= ret + boundarylen;
				memmove(buf, buf + ret + boundarylen, fill);
				state = 1;
			} else {
				/* only write half the buffer, to prevent missing part
				   of the boundary */
				ret = sizeof(buf) / 2;
				if (ret > fill)
					ret = fill;
				if (fpout) {
					ret = fwrite(buf, 1, ret, fpout);
					if (ret <= 0)
						error(1, errno, "fwrite");
				}
				fill -= ret;
				memmove(buf, buf + ret, fill);
			}
		}
	}
	return 0;
}
