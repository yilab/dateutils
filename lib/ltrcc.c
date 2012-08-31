/*** ltrcc.c -- leapseconds materialiser
 *
 * Copyright (C) 2012 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of dateutils.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "leaps.h"
#include "date-core.h"
#include "dt-core.h"
#include "nifty.h"

static inline dt_ssexy_t
__to_unix_epoch(struct dt_dt_s dt)
{
/* daisy is competing with the prevalent unix epoch, this is the offset */
#define DAISY_UNIX_BASE		(19359)
	if (dt.typ == DT_SEXY) {
		/* no way to find out, is there */
		return dt.sexy;
	} else if (dt_sandwich_p(dt) || dt_sandwich_only_d_p(dt)) {
		struct dt_d_s d = dt_dconv(DT_DAISY, dt.d);
		dt_daisy_t dd = d.daisy;
		dt_ssexy_t res = (dd - DAISY_UNIX_BASE) * SECS_PER_DAY;
		if (dt_sandwich_p(dt)) {
			res += (dt.t.hms.h * 60 + dt.t.hms.m) * 60 + dt.t.hms.s;
		}
		return res;
	}
	return 0;
}


#define PROLOGUE	(-1UL)
#define EPILOGUE	(0UL)

static int
pr_line_corr(const char *line, size_t llen, va_list UNUSED(vap))
{
	static int32_t corr = 0;
	char *ep;

	if (llen == PROLOGUE) {
		/* prologue */
		corr = 0;
		fprintf(stdout, "\
const int32_t %s[] = {\n\
	0,\n", line);
		return 0;
	} else if (llen == EPILOGUE) {
		fprintf(stdout, "\
	%i\n\
};\n", corr);
		return 0;
	} else if (line == NULL) {
		/* grrrr */
		return -1;
	} else if (line[0] == '#') {
		/* comment line */
		return 0;
	} else if (line[0] == '\n') {
		/* empty line */
		return 0;
	}
	/* otherwise process */
	if ((dt_strpd(line, "Leap\t%Y\t%b\t%d\t", &ep), ep) == NULL) {
		return -1;
	} else if (llen - (ep - line) < 9) {
		return -1;
	} else if (ep[8] != '\t') {
		return -1;
	}

	switch (ep[9]) {
	case '+':
		++corr;
		break;
	case '-':
		--corr;
		break;
	default:
		/* still buggered */
		return -1;
	}
	/* output the correction then */
	fprintf(stdout, "\t%i,\n", corr);
	return 0;
}

static int
pr_line_d(const char *line, size_t llen, va_list vap)
{
	static int32_t corr = 0;
	struct dt_d_s d;
	dt_dtyp_t typ;
	int colp;
	char *ep;

	/* extract type from inner list */
	typ = va_arg(vap, dt_dtyp_t);
	colp = va_arg(vap, int);

	if (llen == PROLOGUE) {
		/* prologue */
		corr = 0;
		if (!colp) {
			fprintf(stdout, "\
const struct zleap_s %s[] = {\n\
	{0x00U/* 0 */, 0},\n", line);
		} else {
			fprintf(stdout, "\
const uint32_t %s[] = {\n\
	0x00U/* 0 */,\n", line);
		}
		return 0;
	} else if (llen == EPILOGUE) {
		/* epilogue */
		if (!colp) {
			fprintf(stdout, "\
	{UINT32_MAX, %i}\n\
};\n", corr);
		} else {
			fputs("\
	UINT32_MAX\n\
};\n", stdout);
		}
		return 0;
	} else if (line == NULL) {
		/* something's fucked */
		return -1;
	} else if (line[0] == '#') {
		/* comment line */
		return 0;
	} else if (line[0] == '\n') {
		/* empty line */
		return 0;
	}
	/* otherwise process */
	if ((d = dt_strpd(line, "Leap\t%Y\t%b\t%d\t", &ep), ep) == NULL) {
		return -1;
	} else if (llen - (ep - line) < 9) {
		return -1;
	} else if (ep[8] != '\t') {
		return -1;
	}

	/* convert to target type */
	d = dt_dconv(typ, d);

	if (!colp) {
		switch (ep[9]) {
		case '+':
			++corr;
			break;
		case '-':
			--corr;
			break;
		default:
			/* still buggered */
			return -1;
		}
		/* just output the line then */
		fprintf(stdout, "\t{0x%xU/* %i */, %i},\n",
			d.u, (int32_t)d.u, corr);
	} else {
		fprintf(stdout, "\t0x%xU/* %i */,\n", d.u, (int32_t)d.u);
	}
	return 0;
}

static int
pr_line_dt(const char *line, size_t llen, va_list vap)
{
	static int32_t corr = 0;
	struct dt_dt_s d;
	dt_dtyp_t __attribute__((unused)) typ;
	int colp;
	char *ep;
	dt_ssexy_t val;

	/* extract type from inner list */
	typ = va_arg(vap, dt_dtyp_t);
	colp = va_arg(vap, int);

	if (llen == PROLOGUE) {
		/* prologue */
		corr = 0;
		if (!colp) {
			fprintf(stdout, "\
const struct zleap_s %s[] = {\n\
	{INT32_MIN, 0},\n", line);
		} else {
			fprintf(stdout, "\
const int32_t %s[] = {\n\
	INT32_MIN,\n", line);
		}
		return 0;
	} else if (llen == EPILOGUE) {
		/* epilogue */
		if (!colp) {
			fprintf(stdout, "\
	{INT32_MAX, %i}\n\
};\n", corr);
		} else {
			fputs("\
	INT32_MAX\n\
};\n", stdout);
		}
		return 0;
	} else if (line == NULL) {
		/* buggre */
		return -1;
	} else if (line[0] == '#') {
		/* comment line */
		return 0;
	} else if (line[0] == '\n') {
		/* empty line */
		return 0;
	}
	/* otherwise process */
	if ((d = dt_strpdt(
		     line, "Leap\t%Y\t%b\t%d\t%H:%M:%S", &ep), ep) == NULL) {
		return -1;
	} else if (llen - (ep - line) < 1) {
		return -1;
	} else if (ep[0] != '\t') {
		return -1;
	}

	/* fix up and convert to target type */
	d.t.hms.s--;
	val = __to_unix_epoch(d);

	if (!colp) {
		switch (ep[1]) {
		case '+':
			++corr;
			break;
		case '-':
			--corr;
			break;
		default:
			/* still buggered */
			return -1;
		}
		/* just output the line then */
		fprintf(stdout, "\t{0x%xU/* %li */, %i},\n",
			(uint32_t)val, val, corr);
	} else {
		/* column-oriented mode */
		fprintf(stdout, "\t0x%xU/* %li */,\n", (uint32_t)val, val);
	}
	return 0;
}

static int
pr_line_t(const char *line, size_t llen, va_list vap)
{
	struct dt_dt_s d;
	dt_dtyp_t __attribute__((unused)) typ;
	int colp;
	char *ep;
	uint32_t val;

	/* extract type from inner list */
	typ = va_arg(vap, dt_dtyp_t);
	colp = va_arg(vap, int);

	if (llen == PROLOGUE) {
		/* prologue */
		fprintf(stdout, "\
const uint32_t %s[] = {\n\
	UINT32_MAX,\n", line);
		return 0;
	} else if (llen == EPILOGUE) {
		/* epilogue */
		fputs("\
	UINT32_MAX\n\
};\n", stdout);
		return 0;
	} else if (typ != DT_HMS || !colp) {
		return 0;
	} else if (line == NULL) {
		/* do fuckall */
		return -1;
	} else if (line[0] == '#') {
		/* comment line */
		return 0;
	} else if (line[0] == '\n') {
		/* empty line */
		return 0;
	}
	/* otherwise process */
	if ((d = dt_strpdt(
		     line, "Leap\t%Y\t%b\t%d\t%H:%M:%S", &ep), ep) == NULL) {
		return -1;
	} else if (llen - (ep - line) < 1) {
		return -1;
	} else if (ep[0] != '\t') {
		return -1;
	}

	/* fix up and convert to target type */
	val = d.t.hms.u24;
	/* column-oriented mode */
	fprintf(stdout, "\t0x%xU/* %u */,\n", val, val);
	return 0;
}

static int
pr_file(FILE *fp, const char *var, int(*cb)(const char*, size_t, va_list), ...)
{
	va_list vap;
	char *line = NULL;
	size_t len = 0;
	ssize_t nrd;

	/* prologue */
	va_start(vap, cb);
	cb(var, PROLOGUE, vap);
	va_end(vap);
	/* main loop */
	while ((nrd = getline(&line, &len, fp)) >= 0) {
		va_start(vap, cb);
		if (cb(line, nrd, vap) < 0) {
			fprintf(stderr, "line buggered: %s", line);
		}
		va_end(vap);
	}
	/* epilogue */
	va_start(vap, cb);
	cb(var, EPILOGUE, vap);
	va_end(vap);
	/* standard epilogue */
	fprintf(stdout, "\
const size_t n%s = countof(%s);\n\n", var, var);

	if (line != NULL) {
		free(line);
	}
	return 0;
}


static int col = 0;

static int
parse_file(const char *file)
{
	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL) {
		return -1;
	}

	fprintf(stdout, "\
/*** autogenerated by: ltrcc %s */\n\
\n\
#include <stdint.h>\n\
#include <limits.h>\n\
#include \"leaps.h\"\n\
\n\
#if !defined INCLUDED_ltrcc_generated_def_\n\
#define INCLUDED_ltrcc_generated_def_\n\
\n\
#if !defined countof\n\
# define countof(x)	(sizeof(x) / sizeof(*x))\n\
#endif	/* !countof */\n\
\n", file);

	if (col) {
		pr_file(fp, "leaps_corr", pr_line_corr);
		rewind(fp);
	}

	pr_file(fp, "leaps_ymd", pr_line_d, DT_YMD, col);
	rewind(fp);
	pr_file(fp, "leaps_ymcw", pr_line_d, DT_YMCW, col);
	rewind(fp);
	pr_file(fp, "leaps_d", pr_line_d, DT_DAISY, col);
	rewind(fp);
	pr_file(fp, "leaps_s", pr_line_dt, DT_YMD, col);
	rewind(fp);
	pr_file(fp, "leaps_hms", pr_line_t, DT_HMS, col);

	fputs("\
#endif  /* INCLUDED_ltrcc_generated_def_ */\n", stdout);
	return 0;
}


static void
pr_version(FILE *where)
{
	fputs("ltrcc " PACKAGE_VERSION "\n", where);
	return;
}

static void
pr_usage(FILE *where)
{
	pr_version(where);
	fputs("\n\
Usage: ltrcc LEAPS_FILE\n\
\n\
Compile LEAPS_FILE into C source code.\n\
\n\
  -h                    Print help and exit\n\
  -V                    Print version and exit\n\
\n\
  -C                    Column-oriented output\n\
", where);
	return;
}

int
main(int argc, char *argv[])
{
	const char *leaps;

	for (int c; (c = getopt(argc, argv, "ChVv")) != -1;) {
		switch (c) {
		case 'C':
			col = 1;
			break;
		case 'h':
			pr_usage(stdout);
			return 0;
		case 'v':
		case 'V':
			pr_version(stdout);
			return 0;
		case '?':
			pr_usage(stderr);
			return 1;
		default:
			return 2;
		}
	}
	if (optind >= argc) {
		pr_usage(stderr);
		return 1;
	}

	leaps = argv[optind];
	if (parse_file(leaps) < 0) {
		perror("Cannot parse file");
		return 1;
	}
	return 0;
}

/* ltrcc.c ends here */