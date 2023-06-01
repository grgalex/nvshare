/**
 * XOpt - command line parsing library
 *
 * Copyright (c) 2015-2019 Josh Junon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef XOPT_NOSTANDARD
#	define HAVE_STDARG_H 1
#	define HAVE_STDLIB_H 1
#	define HAVE_ASPRINTF_H 1
#	define vasprintf rpl_vasprintf
#	ifndef _GNU_SOURCE
#		define _GNU_SOURCE
#	endif
#endif

#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "./xopt.h"
#include "./snprintf.c"

#define EXTRAS_INIT 10
#define ERRBUF_SIZE 1024 * 4

static char errbuf[ERRBUF_SIZE];

struct xoptContext {
	const xoptOption *options;
	long flags;
	const char *name;
	bool doubledash;
	size_t options_count;
	bool *required;
	jmp_buf *jmp;
};

static void _xopt_set_err(xoptContext *ctx, const char **err, const char *const fmt, ...);
static int _xopt_parse_arg(xoptContext *ctx, int argc, const char **argv,
		int *argi, void *data, const char **err);
static void _xopt_assert_increment(xoptContext *ctx, const char ***extras, int extrasCount,
		size_t *extrasCapac, const char **err);
static int _xopt_get_size(const char *arg);
static int _xopt_get_arg(const xoptContext *ctx, const char *arg, size_t len,
		int size, const xoptOption **option, size_t *option_index);
static void _xopt_set(xoptContext *ctx, void *data, const xoptOption *option, const char *val,
		bool longArg, const char **err);
static void _xopt_default_callback(const char *value, void *data,
		const xoptOption *option, bool longArg, const char **err);

xoptContext* xopt_context(const char *name, const xoptOption *options, long flags,
		const char **err) {
	xoptContext* ctx;
	*err = 0;

	/* malloc context and check */
	ctx = malloc(sizeof(xoptContext));
	if (!ctx) {
		ctx = 0;
		_xopt_set_err(NULL, err, "could not allocate context");
	} else {
		const xoptOption *cur;

		ctx->options = options;
		ctx->flags = flags;
		ctx->name = name;
		ctx->doubledash = false;
		ctx->required = NULL;
		ctx->jmp = NULL;

		ctx->options_count = 0;
		cur = options;
		for (; cur->longArg || cur->shortArg; cur++) ++ctx->options_count;
	}

	return ctx;
}

static int _xopt_parse_impl(xoptContext *ctx, int argc, const char **argv, void *data,
		const char ***inextras, const char **err, int *extrasCount, size_t *extrasCapac,
		const char ***extras, int *argi) {
	int parseResult;
	size_t i;

	*err = 0;
	*argi = 0;
	*extrasCount = 0;
	*extrasCapac = EXTRAS_INIT;
	*extras = malloc(sizeof(**extras) * EXTRAS_INIT);

	jmp_buf jmp;
	ctx->jmp = &jmp;
	if (setjmp(jmp)) {
		goto end;
	}

	/* check if extras malloc'd okay */
	if (!*extras) {
		_xopt_set_err(ctx, err, "could not allocate extras array");
	}

	/* increment argument counter if we aren't
		 instructed to check argv[0] */
	if (!(ctx->flags & XOPT_CTX_KEEPFIRST)) {
		++(*argi);
	}

	/* set up required parameters list */
	ctx->required = malloc(sizeof(*ctx->required) * ctx->options_count);
	for (i = 0; i < ctx->options_count; i++) {
		ctx->required[i] = (ctx->options[i].options & XOPT_REQUIRED) > 0;
	}

	/* iterate over passed command line arguments */
	for (; *argi < argc; (*argi)++) {
		/* parse, breaking if there was a failure
			 parseResult is 0 if option, 1 if extra, or 2 if double-dash was encountered */
		parseResult = _xopt_parse_arg(ctx, argc, argv, argi, data, err);

		/* is the argument an extra? */
		switch (parseResult) {
		case 0: /* option */
			/* make sure we're super-posix'd if specified to be
				 (check that no extras have been specified when an option is parsed,
				 enforcing options to be specific before [extra] arguments */
			if ((ctx->flags & XOPT_CTX_POSIXMEHARDER) && *extrasCount) {
				_xopt_set_err(ctx, err, "options cannot be specified after arguments: %s", argv[*argi]);
				goto end;
			}
			break;
		case 1: /* extra */
			/* make sure we have enough room, or realloc if we don't -
				 check that it succeeded */
			_xopt_assert_increment(ctx, extras, *extrasCount, extrasCapac, err);

			/* add extra to list */
			(*extras)[(*extrasCount)++] = argv[*argi];
			break;
		case 2: /* "--" was encountered */
			/* nothing to do here - "--" was already handled for us */
			break;
		}
	}

end:
	if (!*err) {
		for (i = 0; i < ctx->options_count; i++) {
			if (ctx->required[i]) {
				const xoptOption *opt = &ctx->options[i];
				if (opt->longArg) {
					_xopt_set_err(ctx, err, "missing required option: --%s", opt->longArg);
				} else {
					_xopt_set_err(ctx, err, "missing required option: -%c", opt->shortArg);
				}
				break;
			}
		}
	}

	free(ctx->required);

	if (!*err) {
		/* append null terminator to extras */
		_xopt_assert_increment(ctx, extras, *extrasCount, extrasCapac, err);
		if (!*err) {
			(*extras)[*extrasCount] = 0;
		}
	}

	if (*err) {
		free(*extras);
		*inextras = 0;
		return 0;
	}

	*inextras = *extras;
	return *extrasCount;
}

int xopt_parse(xoptContext *ctx, int argc, const char **argv, void *data,
		const char ***inextras, const char **err) {
	/* avoid longjmp clobbering */
	int extrasCount;
	size_t extrasCapac;
	const char **extras;
	int argi;
	return _xopt_parse_impl(ctx, argc, argv, data, inextras, err, &extrasCount, &extrasCapac, &extras, &argi);
}

void xopt_autohelp(xoptContext *ctx, FILE *stream, const xoptAutohelpOptions *options,
		const char **err) {
	const xoptOption *o;
	size_t i, width = 0, twidth;
	const char *nl = "";
	size_t spacer = options ? options->spacer : 2;

	*err = 0;

	/* make sure that if we ever write a call to _set_err() in the future here,
	   that we won't accidentally cause segfaults - we have an assertion in place
	   for ctx->jmp != NULL, so we make sure we'd trigger that assertion */
	ctx->jmp = NULL;

	if (options && options->usage) {
		fprintf(stream, "%susage: %s %s\n", nl, ctx->name, options->usage);
		nl = "\n";
	}

	if (options && options->prefix) {
		fprintf(stream, "%s%s\n\n", nl, options->prefix);
		nl = "\n";
	}

	/* find max width */
	for (i = 0; ctx->options[i].longArg || ctx->options[i].shortArg; i++) {
		o = &ctx->options[i];
		twidth = 0;
		if (o->longArg) {
			twidth += 2 + strlen(o->longArg);
			if (o->argDescrip) {
				twidth += 1 + strlen(o->argDescrip);
			}
		}
		if (ctx->options[i].shortArg) {
			twidth += 2;
		}
		if (ctx->options[i].shortArg && ctx->options[i].longArg) {
			twidth += 2; /* `, ` */
		}

		width = width > twidth ? width : twidth;
	}

	/* print */
	for (i = 0; ctx->options[i].longArg || ctx->options[i].shortArg; i++) {
		o = &ctx->options[i];
		twidth = 0;
		if (o->shortArg) {
			fprintf(stream, "-%c", o->shortArg);
			twidth += 2;
		}

		if (o->shortArg && o->longArg) {
			fprintf(stream, ", ");
			twidth += 2;
		}

		if (o->longArg) {
			fprintf(stream, "--%s", o->longArg);
			twidth += 2 + strlen(o->longArg);
			if (o->argDescrip) {
				fprintf(stream, "=%s", o->argDescrip);
				twidth += 1 + strlen(o->argDescrip);
			}
		}

		if (o->descrip) {
			for (; twidth < (width + spacer); twidth++) {
				fprintf(stream, " ");
			}

			if (o->options & XOPT_REQUIRED) {
				fprintf(stream, "(Required) %s\n", o->descrip);
			} else {
				fprintf(stream, "%s\n", o->descrip);
			}
		}
	}

	if (options && options->suffix) {
		fprintf(stream, "%s%s\n", nl, options->suffix);
	}
}

static void _xopt_set_err(xoptContext *ctx, const char **err, const char *const fmt, ...) {
	va_list list;
	va_start(list, fmt);
	rpl_vsnprintf(&errbuf[0], ERRBUF_SIZE, fmt, list);
	va_end(list);
	*err = &errbuf[0];

	if (ctx != NULL) {
		assert(ctx->jmp != NULL);
		longjmp(*ctx->jmp, 1);
	}
}

static int _xopt_parse_arg(xoptContext *ctx, int argc, const char **argv,
		int *argi, void *data, const char **err) {
	int size;
	size_t length;
	bool isExtra = false;
	const xoptOption *option = NULL;
	size_t option_index = 0;
	const char* arg = argv[*argi];

	/* are we in doubledash mode? */
	if (ctx->doubledash) {
		return 1;
	}

	/* get argument 'size' (long/short/extra) */
	size = _xopt_get_size(arg);

	/* adjust to parse from beginning of actual content */
	arg += size;
	length = strlen(arg);

	if (size == 1 && length == 0) {
		/* it's just a singular dash - treat it as an extra arg */
		return 1;
	}

	if (size == 2 && length == 0) {
		/* double-dash - everything after this is an extra */
		ctx->doubledash = 1;
		return 2;
	}

	switch (size) {
		int argRequirement;
		char *valStart;
	case 1: /* short */
		/* parse all */
		while (length--) {
			/* get argument or error if not found and strict mode enabled. */
			argRequirement = _xopt_get_arg(ctx, arg++, 1, size, &option, &option_index);
			if (!option) {
				if (ctx->flags & XOPT_CTX_STRICT) {
					_xopt_set_err(ctx, err, "invalid option: -%c", arg[-1]);
				}
				break;
			}

			if (argRequirement > 0 && length > 0 && !(ctx->flags & XOPT_CTX_SLOPPYSHORTS)) {
				_xopt_set_err(ctx, err, "short option parameters must be separated, not condensed: %s", argv[*argi]);
			}

			switch (argRequirement) {
			case 0: /* flag; doesn't take an argument */
				if (length > 0 && (ctx->flags & XOPT_CTX_NOCONDENSE)) {
					_xopt_set_err(ctx, err, "short options cannot be combined: %s", argv[*argi]);
				}

				_xopt_set(ctx, data, option, 0, false, err);
				break;
			case 1: /* argument is optional */
				/* is there another argument, and is it a non-option? */
				if (*argi + 1 < argc && _xopt_get_size(argv[*argi + 1]) == 0) {
					_xopt_set(ctx, data, option, argv[++*argi], false, err);
				} else {
					_xopt_set(ctx, data, option, 0, false, err);
				}
				break;
			case 2: /* requires an argument */
				/* is it the last in a set of condensed options? */
				if (length == 0) {
					/* is there another argument? */
					if (*argi + 1 < argc) {
						/* is the next argument actually an option?
							 this indicates no value was passed */
						if (_xopt_get_size(argv[*argi + 1])) {
							_xopt_set_err(ctx, err, "missing option value: -%c",
									option->shortArg);
						} else {
							_xopt_set(ctx, data, option, argv[++*argi], false, err);
						}
					} else {
						_xopt_set_err(ctx, err, "missing option value: -%c",
								option->shortArg);
					}
				} else {
					_xopt_set(ctx, data, option, arg, false, err);
					length = 0;
				}
				break;
			}
		}
		break;
	case 2: /* long */
		/* find first equals sign */
		valStart = strchr(arg, '=');

		/* is there a value? */
		if (valStart) {
			/* we also increase valStart here in order to lop off
				 the equals sign */
			length = valStart++ - arg;

			/* but not really, if it's null */
			if (!*valStart) {
				valStart = 0;
			}
		}

		/* get the option */
		argRequirement = _xopt_get_arg(ctx, arg, length, size, &option, &option_index);
		if (!option) {
			_xopt_set_err(ctx, err, "invalid option: --%.*s", length, arg);
		} else {
			switch (argRequirement) {
			case 0: /* flag; doesn't take an argument */
				if (valStart) {
					_xopt_set_err(ctx, err, "option doesn't take a value: --%s", arg);
				}

				_xopt_set(ctx, data, option, valStart, true, err);
				break;
			case 2: /* requires an argument */
				if (!valStart) {
					_xopt_set_err(ctx, err, "missing option value: --%s", arg);
				}
				break;
			}

			_xopt_set(ctx, data, option, valStart, true, err);
		}

		break;
	case 0: /* extra */
		isExtra = true;
		break;
	}

	if (option) {
		/* indicate that we've seen this option and thus is no longer required */
		ctx->required[option_index] = false;
	}

	return isExtra ? 1 : 0;
}

static void _xopt_assert_increment(xoptContext *ctx, const char ***extras, int extrasCount,
		size_t *extrasCapac, const char **err) {
	/* have we hit the list size limit? */
	if ((size_t) extrasCount == *extrasCapac) {
		/* increase capcity, realloc, and check for success */
		*extrasCapac += EXTRAS_INIT;
		*extras = realloc(*extras, sizeof(**extras) * *extrasCapac);
		if (!*extras) {
			_xopt_set_err(ctx, err, "could not realloc arguments array");
		}
	}
}

static int _xopt_get_size(const char *arg) {
	int size;
	for (size = 0; size < 2; size++) {
		if (arg[size] != '-') {
			break;
		}
	}
	return size;
}

static int _xopt_get_arg(const xoptContext *ctx, const char *arg, size_t len,
		int size, const xoptOption **option, size_t *option_index) {
	size_t i;

	*option = 0;

	/* find the argument */
	for (i = 0; i < ctx->options_count; i++) {
		const xoptOption *opt = &ctx->options[i];

		if ((size == 1 && opt->shortArg == arg[0])
		    || (opt->longArg && strlen(opt->longArg) == len && !strncmp(opt->longArg, arg, len))) {
			*option_index = i;
			*option = opt;
			break;
		}
	}

	/* determine the optionality of a value */
	if (!*option || (*option)->options & XOPT_TYPE_BOOL) {
		return 0;
	} else if ((*option)->options & XOPT_PARAM_OPTIONAL) {
		return 1;
	} else {
		return 2;
	}
}

static void _xopt_set(xoptContext *ctx, void *data, const xoptOption *option, const char *val,
		bool longArg, const char **err) {
	/* determine callback */
	xoptCallback callback = option->callback ? option->callback : &_xopt_default_callback;

	/* dispatch callback */
	callback(val, data, option, longArg, err);

	/* we check err here instead of relying upon longjmp()
	   since we can't call _set_err() with a context */
	if (*err) {
		assert(ctx->jmp != NULL);
		longjmp(*ctx->jmp, 1);
	}
}

static void _xopt_default_callback(const char *value, void *data,
		const xoptOption *option, bool longArg, const char **err) {
	void *target;
	char *parsePtr = 0;

	/* is a value specified? */
	if ((!value || !strlen(value)) && !(option->options & XOPT_TYPE_BOOL)) {
		/* we reach this point when they specified an optional, non-boolean
			 option but didn't specify a custom handler (therefore, it's not
			 optional).

			 to fix, just remove the optional flag or specify a callback to handle
			 it yourself.
			 */
		return;
	}

	/* get location */
	target = ((char*) data) + option->offset;

	/* switch on the type */
	switch (option->options & 0x3F) {
	case XOPT_TYPE_BOOL:
		/* booleans are special in that they won't have an argument passed
			 into this callback */
		*((_Bool*) target) = true;
		break;
	case XOPT_TYPE_STRING:
		/* lifetime here works out fine; argv can usually be assumed static-like
			 in nature */
		*((const char**) target) = value;
		break;
	case XOPT_TYPE_INT:
		*((int*) target) = (int) strtol(value, &parsePtr, 0);
		break;
	case XOPT_TYPE_LONG:
		*((long*) target) = strtol(value, &parsePtr, 0);
		break;
	case XOPT_TYPE_FLOAT:
		*((float*) target) = (float) strtod(value, &parsePtr);
		break;
	case XOPT_TYPE_DOUBLE:
		*((double*) target) = strtod(value, &parsePtr);
		break;
	default: /* something wonky, or the implementation specifies two types */
		fprintf(stderr, "warning: XOpt argument type invalid: %ld\n",
			option->options & 0x2F);
		break;
	}

	/* check that our parsing functions worked */
	if (parsePtr && *parsePtr) {
		if (longArg) {
			_xopt_set_err(NULL, err, "value isn't a valid number: --%s=%s",
					(void*) option->longArg, value);
		} else {
			_xopt_set_err(NULL, err, "value isn't a valid number: -%c %s",
					option->shortArg, value);
		}
	}
}

