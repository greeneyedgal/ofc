#include "../prep.h"
#include "../fctype.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static unsigned prep_unformat__blank_or_comment(
	const char* src, lang_opts_t opts)
{
	if (!src)
		return 0;

	bool is_comment
		= ((toupper(src[0]) == 'C') || (src[0] == '*')
			|| (opts.debug && (toupper(src[0]) == 'D')));

	if ((opts.form != LANG_FORM_FIXED)
		&& (opts.form != LANG_FORM_TAB))
		is_comment = false;

	if (is_comment || (src[0] == '!'))
	{
		unsigned i;
		for (i = 1; (src[i] != '\0') && !is_vspace(src[i]); i++);
		if (is_vspace(src[i])) i++;
		return i;
	}
	else
	{
		unsigned i;
		for (i = 0; (i < opts.columns) && (src[i] !='\0')
			&& !is_vspace(src[i]) && is_hspace(src[i]); i++);
		if ((i >= opts.columns) || (src[i] == '!'))
			for (; (src[i] != '\0') && !is_vspace(src[i]); i++);
		else if ((src[i] != '\0') && !is_vspace(src[i]))
			return 0;

		return (is_vspace(src[i]) ? (i + 1) : i);
	}

	return 0;
}

static unsigned prep_unformat__fixed_form_label(
	const file_t* file, const char* src, lang_opts_t opts,
	bool* has_label, unsigned* label, bool* continuation,
	unsigned* column)
{
	if (!src)
		return 0;

	bool seen_digit = false;
	unsigned label_value = 0;
	bool is_cont = false;
	unsigned col;

	unsigned i = 0;
	if ((toupper(src[i]) == 'D') && !opts.debug)
		i += 1;

	if (opts.form == LANG_FORM_TAB)
	{
		for (; (src[i] != '\0') && !is_vspace(src[i]) && (src[i] != '\t') && (src[i] != '!'); i++)
		{
			if (isdigit(src[i]))
			{
				seen_digit = true;
				unsigned nvalue = (label_value * 10) + (src[i] - '0');
				if (((nvalue / 10) != label_value)
					|| ((nvalue % 10U) != (unsigned)(src[i] - '0')))
				{
					file_error(file, &src[i],
						"Label number too large");
					return 0;
				}
			}
			else if (!is_hspace(src[i]))
			{
				file_error(file, &src[i],
					"Unexpected character in label");
				return 0;
			}
		}

		col = i;
		if ((src[i] != '\0')
			&& !is_vspace(src[i])
			&& (src[i] != '!'))
		{
			/* Skip tab. */
			col += opts.tab_width;
			i += 1;

			is_cont = (isdigit(src[i]) && (src[i] != '0'));
			if (is_cont)
				col += (src[i++] == '\t' ? opts.tab_width : 1);
		}
	}
	else
	{
		for (col = i; (col < 5) && (src[i] != '\0') && !is_vspace(src[i]) && (src[i] != '!'); i++)
		{
			if (isdigit(src[i]))
			{
				seen_digit = true;
				label_value = (label_value * 10) + (src[i] - '0');
			}
			else if (!is_hspace(src[i]))
			{
				file_error(file, &src[i],
					"Unexpected character in label");
				return 0;
			}

			col += (src[i] == '\t' ? opts.tab_width : 1);
		}

		/* Empty maybe labelled statement */
		if ((col == 5)
			&& (src[i] != '\0')
			&& !is_vspace(src[i]))
		{
			is_cont = (!is_hspace(src[i]) && (src[i] != '0'));

			/* Skip contuation character. */
			col += (src[i++] == '\t' ? opts.tab_width : 1);
		}
	}

	if (has_label   ) *has_label	= seen_digit;
	if (label       ) *label        = label_value;
	if (continuation) *continuation = is_cont;
	if (column      ) *column       = col;
	return i;
}

static unsigned prep_unformat__free_form_label(
	const file_t* file, const char* src, unsigned* label)
{
  if (!src)
	  return 0;

	unsigned label_value = 0;
	unsigned i;
	for (i = 0; (src[i] != '\0') && isdigit(src[i]); i++)
	{
		unsigned nvalue = (label_value * 10) + (src[i] - '0');
		if (((nvalue / 10) != label_value)
			|| ((nvalue % 10U) != (unsigned)(src[i] - '0')))
		{
			file_error(file, &src[i],
				"Label number too large");
			return 0;
		}
	}

	if ((i > 0) && is_hspace(src[i]))
	{
		if (label) *label = label_value;
		return i;
	}

	return 0;
}

static unsigned prep_unformat__fixed_form_code(
	unsigned* col, const char* src, lang_opts_t opts, sparse_t* sparse)
{
	if (!src)
		return 0;

	unsigned i;
	for (i = 0; (*col < opts.columns) && !is_vspace(src[i]) && (src[i] != '\0'); i++)
		*col += (src[i] == '\t' ? opts.tab_width : 1);

	if (sparse && !sparse_append_strn(sparse, src, i))
		return 0;

	return i;
}


typedef struct
{
	char string_delim;
	bool was_escape;

	/* We've seen an ident starting character. */
	bool in_ident;
	bool in_number;
} pre_state_t;

static const pre_state_t PRE_STATE_DEFAULT =
{
	.string_delim = '\0',
	.was_escape = false,

	.in_ident = false,
	.in_number = false,
};

static unsigned prep_unformat__free_form_code(
	unsigned* col, pre_state_t* state,
	const file_t* file, const char* src, lang_opts_t opts,
	sparse_t* sparse, bool* continuation)
{
	if (!src)
		return 0;

	bool     valid_ampersand = false;
	unsigned last_ampersand = 0;

	unsigned hollerith_size   = 0;
	unsigned hollerith_remain = 0;

	if (*continuation && (*src == '&'))
	{
		 src += 1;
		*col += 1;
	}

	unsigned i;
	for (i = 0; (*col < opts.columns) && !is_vspace(src[i]) && (src[i] != '\0'); i++)
	{
		/* Allow the last ampersand prior to a bang comment as continuation. */
		if (!is_hspace(src[i]) && (src[i] != '!'))
			valid_ampersand = false;

		if (state->string_delim != '\0')
		{
			if (!state->was_escape
				&& (src[i] == state->string_delim))
				state->string_delim = '\0';

			/* String continuations are valid. */
			if (!state->was_escape && (src[i] == '&'))
			{
				last_ampersand = i;
				valid_ampersand = true;
			}

			state->was_escape = (src[i] == '\\');
		}
		else if (hollerith_remain > 0)
		{
			/* Ignore hollerith characters. */
			hollerith_remain--;
		}
		else
		{
			/* Break if we see the start of a bang comment. */
			if (src[i] == '!')
				break;

			if (src[i] == '&')
			{
				last_ampersand = i;
				valid_ampersand = true;
			}

			if ((src[i] == '\"')
				|| (src[i] == '\''))
			{
				state->string_delim = src[i];
				state->in_ident = false;
				state->in_number = false;
			}
			else if (state->in_ident)
			{
				state->in_ident = is_ident(src[i]);
			}
			else if (state->in_number)
			{
				if (toupper(src[i]) == 'H')
				{
					hollerith_remain = hollerith_size;
					state->in_ident = false;
				}
				else
				{
					state->in_ident  = isalpha(src[i]);
				}

				state->in_number = isdigit(src[i]);
				if (state->in_number)
				{
					unsigned nsize = (hollerith_size * 10) + (src[i] - '0');
					if (((nsize / 10) != hollerith_size)
						|| ((nsize % 10U) != (unsigned)(src[i] - '0')))
					{
						file_error(file, &src[i],
							"Hollerith too long");
						return 0;
					}

					hollerith_size = nsize;
				}
				else
				{
					hollerith_size = 0;
				}
			}
			else
			{
				state->in_number = isdigit(src[i]);
				state->in_ident  = isalpha(src[i]);

				if (state->in_number)
					hollerith_size = (src[i] - '0');
			}
		}

		*col += (src[i] == '\t' ? opts.tab_width : 1);
	}

	unsigned code_len = i;

	/* If we saw an ampersand not in a string or hollerith
	   and there's been nothing but whitespace since,
	   then this is a free form continuation. */
	*continuation = valid_ampersand;
	if (valid_ampersand)
		code_len = last_ampersand;

	if (sparse && !sparse_append_strn(
		sparse, src, code_len))
		return 0;

	return i;
}

static bool prep_unformat__fixed_form(
	const file_t* file, sparse_t* sparse)
{
	const char* src  = file_get_strz(file);
	lang_opts_t opts = file_get_lang_opts(file);

	bool first_code_line = true;

	const char* newline = NULL;

	if (!src)
		return false;

	bool had_label = false;
	unsigned label_prev = 0;
	unsigned label_pos = 0;

	unsigned row, pos;
	for (row = 0, pos = 0; src[pos] != '\0'; row++)
	{
		unsigned len, col;

		len = prep_unformat__blank_or_comment(
			&src[pos], opts);
		pos += len;
		if (len > 0) continue;

		bool has_label = false;
		unsigned label = 0;
		bool continuation = false;

		len = prep_unformat__fixed_form_label(
			file, &src[pos], opts,
			&has_label, &label, &continuation, &col);
		if (len == 0) return false;

		if (first_code_line && continuation)
		{
			file_warning(file, &src[pos],
				"Initial line can't be a continuation"
				", treating as non-continuation line");
			continuation = false;
		}

		if (continuation)
		{
			if (has_label)
			{
				file_warning(file, &src[pos],
					"Labeling a continuation line doesn't make sense"
					", ignoring label");
			}
			has_label = had_label;
			label = label_prev;
			had_label = false;
		}
		else if (had_label)
		{
			file_warning(file, &src[label_pos],
				"Label attached to blank line, will be ignored");
		}

		pos += len;

		/* Skip initial space. */
		if (!continuation)
		{
			for(; is_hspace(src[pos]); pos++)
				col += (src[pos] == '\t' ? opts.tab_width : 1);
		}

		bool has_code = ((col < opts.columns)
			&& (src[pos] != '\0')
			&& !is_vspace(src[pos]));

		/* Insert single newline character at the end of each line of output. */
		if ((has_code || has_label)
			&& !first_code_line && !continuation
			&& !sparse_append_strn(sparse, newline, 1))
			return false;

		if (has_code)
		{
			if (has_label)
			{
				/* Mark current position in unformat stream as label. */
				if (!sparse_label_add(sparse, label))
					return false;
			}

			/* Append non-empty line to output. */
			len = prep_unformat__fixed_form_code(
				&col, &src[pos], opts, sparse);
			pos += len;
			if (len == 0)
				return false;

			first_code_line = false;
		}
		else if (has_label)
		{
			/* Label is on blank line, attach to next code line. */
			had_label  = true;
			label_prev = label;
			label_pos  = pos;
		}

		/* Skip to the actual end of the line, including all ignored characters. */
		for (; (src[pos] != '\0') && !is_vspace(src[pos]); pos++);

		if (has_code)
			newline = &src[pos];

		/* Eat vspace in input code. */
		if (is_vspace(src[pos])) pos++;
	}

	return true;
}

static bool prep_unformat__free_form(
	const file_t* file, sparse_t* sparse)
{
	const char* src  = file_get_strz(file);
	lang_opts_t opts = file_get_lang_opts(file);
	pre_state_t state = PRE_STATE_DEFAULT;

	bool first_code_line = true;
	bool continuation = false;

	const char* newline = NULL;

	if (!src)
		return false;

	bool had_label = false;
	unsigned label_prev = 0;
	unsigned label_pos = 0;

	unsigned row, pos;
	for (row = 0, pos = 0; src[pos] != '\0'; row++)
	{
		unsigned len, col;

		len = prep_unformat__blank_or_comment(
			&src[pos], opts);
		pos += len;
		if (len > 0) {
			continue;
		}

		bool has_label;
		unsigned label = 0;

		len = prep_unformat__free_form_label(
			file, &src[pos], &label);
		has_label = (len > 0);

		if (continuation)
		{
			if (has_label)
			{
				file_warning(file, &src[pos],
					"Labeling a continuation line doesn't make sense"
					", ignoring label");
			}
			has_label = had_label;
			label = label_prev;
			had_label = false;
		}
		else if (had_label)
		{
			file_warning(file, &src[label_pos],
				"Label attached to blank line, will be ignored");
		}

		/* We can increment col by len, because
		   a free-form label can't contain a tab. */
		col  = len;
		pos += len;

		if (!continuation)
		{
			for(; is_hspace(src[pos]); pos++)
				col += (src[pos] == '\t' ? opts.tab_width : 1);
		}

		bool has_code = ((col < opts.columns)
			&& (src[pos] != '\0')
			&& !is_vspace(src[pos]));

		if (has_code)
		{
			if (has_label)
			{
				/* Mark current position in unformat stream as label. */
				if (!sparse_label_add(sparse, label))
					return false;
			}

			if (!first_code_line && !continuation
				&& !sparse_append_strn(sparse, newline, 1))
				return false;

			len = prep_unformat__free_form_code(
				&col, &state, file, &src[pos], opts,
				sparse, &continuation);
			pos += len;
			if (len == 0) return false;

			if (!continuation)
				state = PRE_STATE_DEFAULT;

			first_code_line = false;
		}
		else if (has_label)
		{
			/* Label is on blank line, attach to next code line. */
			had_label  = true;
			label_prev = label;
			label_pos  = pos;
		}

		/* Skip to the actual end of the line, including all ignored characters. */
		for (; (src[pos] != '\0') && !is_vspace(src[pos]); pos++);

		if (has_code)
			newline = &src[pos];

		/* Eat vspace in input code. */
		if (is_vspace(src[pos])) pos++;

	}
	return true;
}

sparse_t* prep_unformat(file_t* file)
{
	sparse_t* unformat
		= sparse_create_file(file);
	if (!unformat) return NULL;

	bool success = false;
	switch (file_get_lang_opts(file).form)
	{
		case LANG_FORM_FIXED:
		case LANG_FORM_TAB:
			success = prep_unformat__fixed_form(file, unformat);
			break;
		case LANG_FORM_FREE:
			success = prep_unformat__free_form(file, unformat);
			break;
		default:
			break;
	}

	if (!success)
	{
		sparse_delete(unformat);
		return NULL;
	}

	sparse_lock(unformat);
	return unformat;
}
