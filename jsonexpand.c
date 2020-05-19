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

#define NAME	"jsonexpand"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": expand json in key-value pairs, 1/line\n"
	"Usage: " NAME "\n"
	"\n"
	"text/json is the expected content-type on stdin\n"
	"Options:\n"
	" -s, --shell	Prepare for direct eval in shell\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },

	{ "shell", no_argument, NULL, 's', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "+?Vs";

static int emit_regular(const char *topic, const char *dat, int start, int stop);
static int (*emit)(const char *topic, const char *dat, int start, int stop) = emit_regular;

/* generic error logging */
#define mylog(loglevel, fmt, ...) \
	({\
		syslog(loglevel, fmt, ##__VA_ARGS__); \
		if (loglevel <= LOG_ERR)\
			exit(1);\
	})

#define ESTR(num)	strerror(num)

static int shellvarname_escape(int c)
{
	return (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| c == '_';
}

static int emit_shell(const char *topic, const char *dat, int start, int stop)
{
	int j;

	for (j = 0; topic[j]; ++j) {
		printf("%c", shellvarname_escape(topic[j]) ? topic[j] : '_');
	}
	printf("='");
	/* TODO: escape content */
	fwrite(dat + start, stop - start, 1, stdout);
	printf("'\n");
	return 0;
}

static int emit_regular(const char *topic, const char *dat, int start, int stop)
{
	printf("%s\t", topic);
	fwrite(dat + start, stop - start, 1, stdout);
	printf("\n");
	return 0;
}

static int tokdump(char *str, jsmntok_t *t, const char *topic)
{
	int ntok, j;
	char *newtopic;
	size_t len;

	switch (t->type) {
	case JSMN_PRIMITIVE:
	case JSMN_STRING:
		emit(topic, str, t->start, t->end);
		return 1;
	case JSMN_OBJECT:
		newtopic = NULL;
		len = 0;
		ntok = 1;
		for (j = 0; j < t->size; ++j) {
			if (t[ntok].type != JSMN_STRING)
				mylog(LOG_ERR, "property with non-string name");
			if (strlen(topic) + t[ntok].end - t[ntok].start + 1 > len) {
				len = (strlen(topic) + t[ntok].end - t[ntok].start + 1 + 128) & ~127;
				newtopic = realloc(newtopic, len);
				if (!newtopic)
					mylog(LOG_ERR, "realloc %li: %s", (long)len, ESTR(errno));
			}
			sprintf(newtopic, "%s/%.*s", topic, t[ntok].end - t[ntok].start, str + t[ntok].start);
			++ntok;
			ntok += tokdump(str, t+ntok, newtopic);
		}
		if (newtopic)
			free(newtopic);
		return ntok;
	case JSMN_ARRAY:
		newtopic = malloc(strlen(topic) + 16);
		ntok = 1;
		for (j = 0; j < t->size; ++j) {
			sprintf(newtopic, "%s/%i", topic, j);
			ntok += tokdump(str, t+ntok, newtopic);
		}
		free(newtopic);
		return ntok;
	default:
		return 0;
	}
}

int main(int argc, char *argv[])
{
	int opt, ret;
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

	case 's':
		emit = emit_shell;
		break;
	}

	openlog(NAME, LOG_PERROR, LOG_DAEMON);

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

	tokdump(dat, tok, "");
	return 0;
}
