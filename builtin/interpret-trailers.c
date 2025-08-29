/*
 * Builtin "git interpret-trailers"
 *
 * Copyright (c) 2013, 2014 Christian Couder <chriscool@tuxfamily.org>
 *
 */
#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "environment.h"
#include "gettext.h"
#include "parse-options.h"
#include "string-list.h"
#include "trailer.h"
#include "config.h"

static const char * const git_interpret_trailers_usage[] = {
	N_("git interpret-trailers [--in-place] [--trim-empty]\n"
	   "                       [(--trailer (<key>|<key-alias>)[(=|:)<value>])...]\n"
	   "                       [--parse] [<file>...]"),
	NULL
};

static enum trailer_where where;
static enum trailer_if_exists if_exists;
static enum trailer_if_missing if_missing;

static int option_parse_where(const struct option *opt,
			      const char *arg, int unset UNUSED)
{
	/* unset implies NULL arg, which is handled in our helper */
	return trailer_set_where(opt->value, arg);
}

static int option_parse_if_exists(const struct option *opt,
				  const char *arg, int unset UNUSED)
{
	/* unset implies NULL arg, which is handled in our helper */
	return trailer_set_if_exists(opt->value, arg);
}

static int option_parse_if_missing(const struct option *opt,
				   const char *arg, int unset UNUSED)
{
	/* unset implies NULL arg, which is handled in our helper */
	return trailer_set_if_missing(opt->value, arg);
}

static void new_trailers_clear(struct list_head *trailers)
{
	struct list_head *pos, *tmp;
	struct new_trailer_item *item;

	list_for_each_safe(pos, tmp, trailers) {
		item = list_entry(pos, struct new_trailer_item, list);
		list_del(pos);
		free(item);
	}
}

static int option_parse_trailer(const struct option *opt,
				   const char *arg, int unset)
{
	struct list_head *trailers = opt->value;
	struct new_trailer_item *item;

	if (unset) {
		new_trailers_clear(trailers);
		return 0;
	}

	if (!arg)
		return -1;

	item = xmalloc(sizeof(*item));
	item->text = arg;
	item->where = where;
	item->if_exists = if_exists;
	item->if_missing = if_missing;
	list_add_tail(&item->list, trailers);
	return 0;
}

static int parse_opt_parse(const struct option *opt, const char *arg,
			   int unset)
{
	struct process_trailer_options *v = opt->value;

	v->only_trailers = 1;
	v->only_input = 1;
	v->unfold = 1;
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	return 0;
}

static void read_input_file(struct strbuf *sb, const char *file)
{
	if (file) {
		if (strbuf_read_file(sb, file, 0) < 0)
			die_errno(_("could not read input file '%s'"), file);
	} else {
		if (strbuf_read(sb, fileno(stdin), 0) < 0)
			die_errno(_("could not read from stdin"));
	}
	strbuf_complete_line(sb);
}

int cmd_interpret_trailers(int argc,
			   const char **argv,
			   const char *prefix,
			   struct repository *repo UNUSED)
{
	struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;
	LIST_HEAD(trailers);

	struct option options[] = {
		OPT_BOOL(0, "in-place", &opts.in_place, N_("edit files in place")),
		OPT_BOOL(0, "trim-empty", &opts.trim_empty, N_("trim empty trailers")),

		OPT_CALLBACK(0, "where", &where, N_("placement"),
			     N_("where to place the new trailer"), option_parse_where),
		OPT_CALLBACK(0, "if-exists", &if_exists, N_("action"),
			     N_("action if trailer already exists"), option_parse_if_exists),
		OPT_CALLBACK(0, "if-missing", &if_missing, N_("action"),
			     N_("action if trailer is missing"), option_parse_if_missing),

		OPT_BOOL(0, "only-trailers", &opts.only_trailers, N_("output only the trailers")),
		OPT_BOOL(0, "only-input", &opts.only_input, N_("do not apply trailer.* configuration variables")),
		OPT_BOOL(0, "unfold", &opts.unfold, N_("reformat multiline trailer values as single-line values")),
		OPT_CALLBACK_F(0, "parse", &opts, NULL, N_("alias for --only-trailers --only-input --unfold"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, parse_opt_parse),
		OPT_BOOL(0, "no-divider", &opts.no_divider, N_("do not treat \"---\" as the end of input")),
		OPT_CALLBACK(0, "trailer", &trailers, N_("trailer"),
				N_("trailer(s) to add"), option_parse_trailer),
		OPT_END()
	};

	repo_config(the_repository, git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options,
			     git_interpret_trailers_usage, 0);

	if (opts.only_input && !list_empty(&trailers))
		usage_msg_opt(
			_("--trailer with --only-input does not make sense"),
			git_interpret_trailers_usage,
			options);

	trailer_config_init();

	if (argc) {
		int i;
		for (i = 0; i < argc; i++) {
			struct strbuf in_buf = STRBUF_INIT;
			struct strbuf out_buf = STRBUF_INIT;

			read_input_file(&in_buf, argv[i]);
			if (trailer_process(&opts, in_buf.buf, &trailers, &out_buf) < 0)
				die(_("failed to process trailers for %s"), argv[i]);
			if (opts.in_place)
				write_file_buf(argv[i], out_buf.buf, out_buf.len);
			else
				fwrite(out_buf.buf, 1, out_buf.len, stdout);
			strbuf_release(&in_buf);
			strbuf_release(&out_buf);
		}
	} else {
		struct strbuf in_buf = STRBUF_INIT;
		struct strbuf out_buf = STRBUF_INIT;

		if (opts.in_place)
			die(_("no input file given for in-place editing"));

		read_input_file(&in_buf, NULL);
		if (trailer_process(&opts, in_buf.buf, &trailers, &out_buf) < 0)
			die(_("failed to process trailers"));
		fwrite(out_buf.buf, 1, out_buf.len, stdout);
		strbuf_release(&in_buf);
		strbuf_release(&out_buf);
	}

	new_trailers_clear(&trailers);

	return 0;
}
