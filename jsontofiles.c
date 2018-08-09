#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <syslog.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <jsmn.h>

#define NAME	"jsontofiles"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": split json data in files\n"
	"Usage: " NAME " [TEMPDIR]\n"
	"\n"
	"text/json is the expected content-type on stdin\n"
	"TEMPDIR does not need to exist.\n"
	"When TEMPDIR is absent, /tmp/cgi-<parentpid> is used.\n"
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

static const char optstring[] = "+?V";

/* generic error logging */
#define mylog(loglevel, fmt, ...) \
	({\
		syslog(loglevel, fmt, ##__VA_ARGS__); \
		if (loglevel <= LOG_ERR)\
			exit(1);\
	})

#define ESTR(num)	strerror(num)

static int tokdump(char *str, jsmntok_t *t, const char *fs)
{
	int fd, ntok, j, ret;
	char *newfs;
	size_t len;

	switch (t->type) {
	case JSMN_PRIMITIVE:
	case JSMN_STRING:
		fd = open(fs, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
			mylog(LOG_ERR, "create %s: %s", fs, ESTR(errno));
		ret = write(fd, str+t->start, t->end - t->start);
		if (ret < 0)
			mylog(LOG_ERR, "write %s: %s", fs, ESTR(errno));
		close(fd);
		return 1;
	case JSMN_OBJECT:
		newfs = NULL;
		len = 0;
		ntok = 1;
		if (mkdir(fs, 0777) < 0)
			mylog(LOG_ERR, "mkdir %s: %s", fs, ESTR(errno));
		for (j = 0; j < t->size; ++j) {
			if (t[ntok].type != JSMN_STRING)
				mylog(LOG_ERR, "property with non-string name");
			if (strlen(fs) + t[ntok].end - t[ntok].start + 1 > len) {
				len = (strlen(fs) + t[ntok].end - t[ntok].start + 1 + 128) & ~127;
				newfs = realloc(newfs, len);
				if (!newfs)
					mylog(LOG_ERR, "realloc %li: %s", (long)len, ESTR(errno));
			}
			sprintf(newfs, "%s/%.*s", fs, t[ntok].end - t[ntok].start, str + t[ntok].start);
			++ntok;
			ntok += tokdump(str, t+ntok, newfs);
		}
		if (newfs)
			free(newfs);
		return ntok;
	case JSMN_ARRAY:
		newfs = malloc(strlen(fs) + 16);
		ntok = 1;
		if (mkdir(fs, 0777) < 0)
			mylog(LOG_ERR, "mkdir %s: %s", fs, ESTR(errno));
		for (j = 0; j < t->size; ++j) {
			sprintf(newfs, "%s/%i", fs, j);
			ntok += tokdump(str, t+ntok, newfs);
		}
		free(newfs);
		return ntok;
	default:
		return 0;
	}
}

int main(int argc, char *argv[])
{
	int opt, ret;
	char *tempdir;
	struct stat st;

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

	openlog(NAME, LOG_PERROR, LOG_DAEMON);

	tempdir = argv[optind];
	if (!tempdir)
		asprintf(&tempdir, "/tmp/%s-%i", NAME, getppid());

	/* setup input */
	if (fstat(STDIN_FILENO, &st))
		mylog(LOG_ERR, "fstat(0): %s", ESTR(errno));

	char *dat;
	dat = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, STDIN_FILENO, 0);
	if (dat == MAP_FAILED)
		mylog(LOG_ERR, "mmap(0): %s", ESTR(errno));

	/* start parsing */
	jsmn_parser prs;
	jsmntok_t *tok;
	size_t tokcnt;

	jsmn_init(&prs);
	ret = jsmn_parse(&prs, dat, st.st_size, NULL, 0);
	if (ret < 0)
		mylog(LOG_ERR, "jsmn_parse returned %i", ret);

	jsmn_init(&prs);
	tokcnt = ret;
	tok = malloc(sizeof(*tok)*tokcnt);
	ret = jsmn_parse(&prs, dat, st.st_size, tok, tokcnt);
	if (ret < 0)
		mylog(LOG_ERR, "jsmn_parse returned %i", ret);

	tokdump(dat, tok, tempdir);
	return 0;
}
