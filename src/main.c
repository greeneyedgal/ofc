#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "file.h"
#include "prep.h"
#include "parse/file.h"
#include "reformat.h"

void print_usage(const char* name)
{
	printf("%s [OPTIONS] FILE\n", name);
	printf("Options:\n");
	printf("  -free-form, -fixed-form, -tab-form    selects form type\n");
	printf("  -tab-width-<n>                        sets tab with to <n>\n");
	printf("  -d, -debug                            selects debug mode, defaults to false\n");
	printf("  -columns-<n>                          sets number of columns to <n>\n");
	printf("  -case-sen                             selects case sensitivity, defaults to false\n");
}

const char *get_file_ext(const char *path) {
	if (!path)
		return NULL;

	const char *dot = NULL;
	unsigned i;
	for (i = 0; path[i] != '\0'; i++)
	{
		if (path[i] == '/')
			dot = NULL;
		else if (path[i] == '.')
			dot = &path[i];
	}
  return (dot ? &dot[1] : NULL);
}

typedef enum
{
	FIXED_FORM,
	FREE_FORM,
	TAB_FORM,
	TAB_WIDTH,
	DEBUG,
	COLUMNS,
	CASE_SEN,
	INVALID
} args_e;

args_e get_options(char* arg, int* num)
{
	char* option[4];
	char* token = strtok(arg, "-");
	int count = 0;

	/* Get options and values */
	while (token)
	{
		option[count++] = token;
		token = strtok(NULL, "-");
	}

	/* Parse -free-form, -tab-form, -fixed-form*/
	if ((count == 2) && (strcmp(option[1], "form") == 0))
	{
		if (strcmp(option[0], "fixed") == 0)
		{
			printf("Select form type, %s, %s \n", option[1], option[0]);
			return FIXED_FORM;
		}
		else if (strcmp(option[0], "free") == 0)
		{
			printf("Select form type, %s, %s \n", option[1], option[0]);
			return FREE_FORM;
		}
		else if (strcmp(option[0], "tab") == 0)
		{
			printf("Select form type, %s, %s \n", option[1], option[0]);
			return TAB_FORM;
		}
		else
		{
			fprintf(stderr, "Error: invalid option\n");
			return INVALID;
		}
	}
	/* Parse -tab-width-n */
	else if ((count == 3)
		&& (strcmp(option[0], "tab"  ) == 0)
		&& (strcmp(option[1], "width") == 0))
	{
		int width = strtol(option[2], (char **)NULL, 10);
		if (width >= 0)
		{
			printf("Select tab width %s\n", option[2]);
			*num = width;
			return TAB_WIDTH;
		}
		else
		{
			fprintf(stderr, "Error: invalid tab width\n");
			return INVALID;
		}
	}
	/* Parse -debug, -d */
	else if ((count == 1) && ((strcmp(option[0], "debug") == 0)
		|| (strcmp(option[0], "d") == 0)))
	{
			printf("Select debug\n");
			return DEBUG;
	}
	/* Parse -columns-n, -c-n */
	else if ((count == 2) && ((strcmp(option[0], "columns") == 0)
		|| (strcmp(option[0], "c") == 0)))
	{
		int col = strtol(option[1], (char **)NULL, 10);
		if (col > 0)
		{
			printf("Select columns %s\n", option[1]);
			*num = col;
			return COLUMNS;
		}
		else
		{
			fprintf(stderr, "Error: invalid number of columns\n");
			return INVALID;
		}
	}
	/* Parse -case-sen */
	else if ((count == 2) && (strcmp(option[0], "case") == 0)
		&& (strcmp(option[1], "sen") == 0))
	{
		printf("Select case sentivity\n");
		return CASE_SEN;
	}
	else
	{
		fprintf(stderr, "Error: invalid option\n");
		return INVALID;
	}
}

int main(int argc, const char* argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "Error: Expected source path\n");
		return EXIT_FAILURE;
	}

	const char* path = argv[argc - 1];

	const char* source_file_ext = get_file_ext(path);

	lang_opts_t opts = LANG_OPTS_F77;

	if (source_file_ext
		&& (strcasecmp(source_file_ext, "F90") == 0))
		opts = LANG_OPTS_F90;

	int i;
	for (i = 1; i < (argc - 1); i++)
	{
		char* arg = strdup(argv[i]);
		int num = 0;
		args_e name = get_options(arg, &num);
		free(arg);

		switch(name)
		{
			case FIXED_FORM:
				opts.form = LANG_FORM_FIXED;
				break;
			case FREE_FORM:
				opts.form = LANG_FORM_FREE;
				break;
			case TAB_FORM:
				opts.form = LANG_FORM_TAB;
				break;
			case TAB_WIDTH:
				opts.tab_width = num;
				break;
			case DEBUG:
				opts.debug = true;
				break;
			case COLUMNS:
				opts.columns = num;
				break;
			case CASE_SEN:
				opts.case_sensitive = true;
				break;
			default:
				print_usage(argv[0]);
				return EXIT_FAILURE;
		}
	}

	file_t* file = file_create(path, opts);
	if (!file)
	{
		fprintf(stderr, "Error: Failed read source file '%s'\n", path);
		return EXIT_FAILURE;
	}

	sparse_t* condense = prep(file);
	file_delete(file);
	if (!condense)
	{
		fprintf(stderr, "Error: Failed preprocess source file '%s'\n", path);
		return EXIT_FAILURE;
	}

	parse_stmt_list_t* program
		= parse_file(condense);

	if (!program)
	{
		fprintf(stderr, "Error: Failed to parse program\n");
		sparse_delete(condense);
		return EXIT_FAILURE;
	}

	FILE *print_f90 = tmpfile();

	if (!parse_stmt_list_print(
		fileno(print_f90), program, 0))
	{
		fprintf(stderr, "Error: Failed to reprint program\n");
		sparse_delete(condense);
		return EXIT_FAILURE;
	}

	rewind(print_f90);
	parse_reformat_print(print_f90);

	parse_stmt_list_delete(program);
	sparse_delete(condense);
	return EXIT_SUCCESS;
}
