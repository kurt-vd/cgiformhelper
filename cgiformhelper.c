#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <dirent.h>
#include <syslog.h>
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
	"Program Options: (mainly for debuggin)\n"
	" -bBOUNDARY	Use BOUNDARY as mime boundary seperator instead\n"
	"		The content type is not checked\n"
	"		This allows for off-site debugging\n"
	" -sFILE	write parameter names in sequence to FILE\n"
	"\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },

	{ "boundary", required_argument, NULL, 'b', },
	{ "sequence", required_argument, NULL, 's', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "+?Vb:s:";

char buf[2*1024];

__attribute__((format(printf,2,3)))
static void esyslog(int priority, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vsyslog(priority, fmt, va);
	va_end(va);
	if (priority >= LOG_ERR)
		exit(1);
}

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

/* property access functions */
__attribute__((format(printf,2,3)))
static const char *propread(size_t sizehint, const char *fmt, ...)
{
	static char *buf;
	static size_t bufsize;
	int ret, fd;
	va_list va;
	char *filename;

	va_start(va, fmt);
	vasprintf(&filename, fmt, va);
	va_end(va);

	if (!sizehint) {
		struct stat st;

		if (stat(filename, &st))
			esyslog(LOG_ERR, "stat %s: %s\n", filename, strerror(errno));
		sizehint = st.st_size;
	}
	if (sizehint > bufsize) {
		bufsize = (sizehint+1+127) & ~127;
		buf = realloc(buf, bufsize);
		if (!buf)
			esyslog(LOG_ERR, "realloc: %s\n", strerror(errno));
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		esyslog(LOG_ERR, "open %s: %s\n", filename, strerror(errno));
	ret = read(fd, buf, sizehint);
	if (ret < 0)
		esyslog(LOG_ERR, "read %s: %s\n", filename, strerror(errno));
	buf[ret] = 0;
	close(fd);
	free(filename);
	return buf;
}

__attribute__((format(printf,2,3)))
static int propwrite(const char *str, const char *fmt, ...)
{
	int ret, fd;
	va_list va;
	char *filename;

	va_start(va, fmt);
	vasprintf(&filename, fmt, va);
	va_end(va);

	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		esyslog(LOG_ERR, "open %s: %s\n", filename, strerror(errno));
	ret = write(fd, str, strlen(str));
	if (ret < 0)
		esyslog(LOG_ERR, "read %s: %s\n", filename, strerror(errno));
	close(fd);
	free(filename);
	return 0;
}

/* return the destination filename for a cgi parameter value
 * This will fixup things when multiple values for the same
 * parameter name exists
 */
static const char *cgimultiname(const char *name)
{
	struct stat st;
	int nparams;
	static char *filename;
	char nbuf[32];

	if (stat(name, &st) < 0) {
		if (errno == ENOENT)
			/* name is valid to use */
			return name;
		esyslog(LOG_ERR, "stat %s: %s\n", name, strerror(errno));
		return NULL;
	}
	if (S_ISDIR(st.st_mode)) {
		/* directory already exists */
		nparams = strtoul(propread(st.st_size, "%s/.n", name), NULL, 0);
	} else {
		nparams = 1;
		if (mkdir(".tmp", 0777) < 0)
			esyslog(LOG_ERR, "mkdir .tmp: %s\n", strerror(errno));
		if (rename(name, ".tmp/0") < 0)
			esyslog(LOG_ERR, "mv %s .tmp/0: %s\n", name, strerror(errno));
		if (rename(".tmp", name) < 0)
			esyslog(LOG_ERR, "mv .tmp %s: %s\n", name, strerror(errno));
		/* move metadata of parameters */
		DIR *dir;
		struct dirent *ent;
		char *prefix, *newname;
		int prefixlen;

		/* prepare property prefix */
		asprintf(&prefix, ".%s:", name);
		prefixlen = strlen(prefix);

		dir = opendir(".");
		if (!dir)
			esyslog(LOG_ERR, "opendir .");
		for (ent = readdir(dir); ent; ent = readdir(dir)) {
			if (!strncmp(ent->d_name, prefix, prefixlen)) {
				asprintf(&newname, "%s/.0:%s", name, ent->d_name+prefixlen);
				if (rename(ent->d_name, newname) < 0)
					esyslog(LOG_ERR, "mv %s %s: %s\n", ent->d_name, newname, strerror(errno));
				free(newname);
			}
		}
		closedir(dir);
		free(prefix);
	}
	if (filename)
		free(filename);
	asprintf(&filename, "%s/%u", name, nparams);
	++nparams;
	sprintf(nbuf, "%u\n", nparams);
	propwrite(nbuf, "%s/.n", name);
	return filename;
}

int main(int argc, char *argv[])
{
	int opt, state, ret;
	const char *conttype, *needle, *val;
	char *tempdir, *boundary, *eboundary, *endp, *tok;
	int fill, boundarylen;
	FILE *fpout;
	const char *boundaryopt = NULL;
	const char *sequencefile = NULL;
	FILE *seqfp = NULL;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, VERSION);
		return 0;
	case 'b':
		boundaryopt = optarg;
		break;
	case 's':
		sequencefile = optarg;
		break;

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
		asprintf(&tempdir, "/tmp/cgi-%i", getppid());

	if (mkdir(tempdir, 0777) < 0) {
		if (errno != EEXIST)
			esyslog(LOG_ERR, "mkdir %s: %s\n", tempdir, strerror(errno));
	}
	if (chdir(tempdir) < 0)
		esyslog(LOG_ERR,"chdir %s: %s\n", tempdir, strerror(errno));

	if (sequencefile) {
		seqfp = fopen(sequencefile, "w");
		if (!seqfp)
			esyslog(LOG_ERR, "fopen %s w: %s\n", sequencefile, strerror(errno));
	}

	if (!boundaryopt) {
		/* test content-type */
		conttype = getenv("CONTENT_TYPE");
		if (!conttype)
			exit(1);
		ret = strstart(conttype, "multipart/form-data; boundary=");
		if (!ret)
			esyslog(LOG_ERR, "wrong content type: %s\n", conttype);
		boundaryopt = conttype + ret;
	}
	/* prepend -- before boundary */
	asprintf(&boundary, "\r\n--%s\r\n", boundaryopt);
	asprintf(&eboundary, "\r\n--%s--\r\n", boundaryopt);
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
				esyslog(LOG_ERR, "read stdin: %s\n", strerror(errno));
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
					esyslog(LOG_ERR, "buffer full with no line\n");
				continue;
			}
			*endp = 0;
			endp += 2;
			if (!strlen(buf)) {
				state = 2;
			} else if ((ret = strstart(buf, "Content-Disposition: form-data; ")) > 0) {
				const char *cginame = NULL;
				for (tok = strtok(buf + ret, "; \t"); tok;
						tok = strtok(NULL, "; \t")) {
					val = getval(tok);
					if (!strcmp(tok, "name")) {
						cginame = val;
						if (seqfp)
							fprintf(seqfp, "%s\n", cginame);
						/* protect for multiple entries */
						cginame = cgimultiname(cginame);
						fpout = fopen(cginame, "w");
						if (!fpout)
							esyslog(LOG_ERR, "fopen %s: %s\n", cginame, strerror(errno));
					} else if (cginame) {
						/* save property */
						FILE *fp;
						char *propname, *dir;

						dir = strrchr(cginame, '/');
						if (!dir)
							asprintf(&propname, ".%s:%s", cginame, tok);
						else {
							/* strip */
							*dir = 0;
							asprintf(&propname, "%s/.%s:%s", cginame, dir+1, tok);
							*dir = '/';
						}
						fp = fopen(propname, "w");
						if (!fp)
							esyslog(LOG_ERR, "fopen %s: %s\n", propname, strerror(errno));
						fprintf(fp, "%s\n", val);
						fclose(fp);
						free(propname);
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
						esyslog(LOG_ERR, "fwrite: %s\n", strerror(errno));
				}
				fill -= ret;
				memmove(buf, buf + ret, fill);
			}
		}
	}
	fclose(seqfp);
	return 0;
}
