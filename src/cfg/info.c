/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "info.h"

#include <assert.h> /* assert() */
#include <ctype.h> /* isdigit() */
#include <stddef.h> /* NULL size_t */
#include <stdio.h> /* FILE fpos_t fclose() fgetpos() fgets() fprintf() fputc()
                      fread() fscanf() fsetpos() fwrite() snprintf() */
#include <stdlib.h> /* abs() free() */
#include <string.h> /* memcpy() memset() strtol() strcmp() strchr() strlen() */

#include "../compat/fs_limits.h"
#include "../compat/os.h"
#include "../compat/reallocarray.h"
#include "../engine/cmds.h"
#include "../engine/options.h"
#include "../ui/fileview.h"
#include "../ui/ui.h"
#include "../utils/file_streams.h"
#include "../utils/filemon.h"
#include "../utils/filter.h"
#include "../utils/fs.h"
#include "../utils/hist.h"
#include "../utils/log.h"
#include "../utils/macros.h"
#include "../utils/matcher.h"
#include "../utils/matchers.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../utils/string_array.h"
#include "../utils/utils.h"
#include "../bmarks.h"
#include "../cmd_core.h"
#include "../dir_stack.h"
#include "../filelist.h"
#include "../flist_hist.h"
#include "../filetype.h"
#include "../filtering.h"
#include "../marks.h"
#include "../opt_handlers.h"
#include "../registers.h"
#include "../status.h"
#include "../trash.h"
#include "config.h"
#include "info_chars.h"

static void get_sort_info(view_t *view, const char line[]);
static void append_to_history(hist_t *hist, void (*saver)(const char[]),
		const char item[]);
static void ensure_history_not_full(hist_t *hist);
static void get_history(view_t *view, int reread, const char dir[],
		const char file[], int rel_pos);
static void set_manual_filter(view_t *view, const char value[]);
static void set_view_property(view_t *view, char type, const char value[]);
static int copy_file(const char src[], const char dst[]);
static int copy_file_internal(FILE *src, FILE *dst);
static void update_info_file(const char filename[], int merge);
static void process_hist_entry(view_t *view, const char dir[],
		const char file[], int pos, char ***lh, int *nlh, int **lhp, size_t *nlhp);
static char * convert_old_trash_path(const char trash_path[]);
static void write_options(FILE *fp);
static void write_assocs(FILE *fp, const char str[], char mark,
		assoc_list_t *assocs, int prev_count, char *prev[]);
static void write_doubling_commas(FILE *fp, const char str[]);
static void write_commands(FILE *fp, char *cmds_list[], char *cmds[],
		int ncmds);
static void write_marks(FILE *fp, const char non_conflicting_marks[],
		char *marks[], const int timestamps[], int nmarks);
static void write_bmarks(FILE *fp, char *bmarks[], const int timestamps[],
		int nbmarks);
static void write_bmark(const char path[], const char tags[], time_t timestamp,
		void *arg);
static void write_tui_state(FILE *fp);
static void write_view_history(FILE *fp, view_t *view, const char str[],
		char mark, int prev_count, char *prev[], int pos[]);
static void write_history(FILE *fp, const char str[], char mark, int prev_count,
		char *prev[], const hist_t *hist);
static void write_registers(FILE *fp, char *regs[], int nregs);
static void write_dir_stack(FILE *fp, char *old_dir_stack[],
		int nold_dir_stack);
static void write_trash(FILE *fp, char *trash[], int ntrash);
static void write_general_state(FILE *fp);
static char * read_vifminfo_line(FILE *fp, char buffer[]);
static void remove_leading_whitespace(char line[]);
static const char * escape_spaces(const char *str);
static void put_sort_info(FILE *fp, char leading_char, const view_t *view);
static int read_optional_number(FILE *f);
static int read_number(const char line[], long *value);
static size_t add_to_int_array(int **array, size_t len, int what);

/* Monitor to check for changes of vifminfo file. */
static filemon_t vifminfo_mon;

void
read_info_file(int reread)
{
	/* TODO: refactor this function read_info_file() */

	FILE *fp;
	char info_file[PATH_MAX + 16];
	char *line = NULL, *line2 = NULL, *line3 = NULL, *line4 = NULL;

	snprintf(info_file, sizeof(info_file), "%s/vifminfo", cfg.config_dir);

	if((fp = os_fopen(info_file, "r")) == NULL)
		return;

	(void)filemon_from_file(info_file, FMT_MODIFIED, &vifminfo_mon);

	while((line = read_vifminfo_line(fp, line)) != NULL)
	{
		const char type = line[0];
		const char *const line_val = line + 1;

		if(type == LINE_TYPE_COMMENT || type == '\0')
			continue;

		if(type == LINE_TYPE_OPTION)
		{
			if(line_val[0] == '[' || line_val[0] == ']')
			{
				view_t *v = curr_view;
				curr_view = (line_val[0] == '[') ? &lwin : &rwin;
				process_set_args(line_val + 1, 1, 1);
				curr_view = v;
			}
			else
			{
				process_set_args(line_val, 1, 1);
			}
		}
		else if(type == LINE_TYPE_FILETYPE || type == LINE_TYPE_XFILETYPE)
		{
			if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				char *error;
				matchers_t *ms;
				const int x = (type == LINE_TYPE_XFILETYPE);

				/* Prevent loading of old builtin fake associations. */
				if(ends_with(line2, "}" VIFM_PSEUDO_CMD))
				{
					continue;
				}

				ms = matchers_alloc(line_val, 0, 1, "", &error);
				if(ms == NULL)
				{
					/* Ignore error description. */
					free(error);
				}
				else
				{
					ft_set_programs(ms, line2, x,
							curr_stats.exec_env_type == EET_EMULATOR_WITH_X);
				}
			}
		}
		else if(type == LINE_TYPE_FILEVIEWER)
		{
			if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				char *error;
				matchers_t *const ms = matchers_alloc(line_val, 0, 1, "", &error);
				if(ms == NULL)
				{
					/* Ignore error description. */
					free(error);
				}
				else
				{
					ft_set_viewers(ms, line2);
				}
			}
		}
		else if(type == LINE_TYPE_COMMAND)
		{
			if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				char *cmdadd_cmd;
				if((cmdadd_cmd = format_str("command %s %s", line_val, line2)) != NULL)
				{
					exec_commands(cmdadd_cmd, curr_view, CIT_COMMAND);
					free(cmdadd_cmd);
				}
			}
		}
		else if(type == LINE_TYPE_MARK)
		{
			if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				if((line3 = read_vifminfo_line(fp, line3)) != NULL)
				{
					const int timestamp = read_optional_number(fp);
					setup_user_mark(line_val[0], line2, line3, timestamp);
				}
			}
		}
		else if(type == LINE_TYPE_BOOKMARK)
		{
			if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				long timestamp;
				if((line3 = read_vifminfo_line(fp, line3)) != NULL &&
						read_number(line3, &timestamp))
				{
					(void)bmarks_setup(line_val, line2, (size_t)timestamp);
				}
			}
		}
		else if(type == LINE_TYPE_ACTIVE_VIEW)
		{
			/* Don't change active view on :restart command. */
			if(line_val[0] == 'r' && !reread)
			{
				ui_views_update_titles();

				curr_view = &rwin;
				other_view = &lwin;
			}
		}
		else if(type == LINE_TYPE_QUICK_VIEW_STATE)
		{
			const int i = atoi(line_val);
			stats_set_quickview(i == 1);
		}
		else if(type == LINE_TYPE_WIN_COUNT)
		{
			if(!reread)
			{
				const int i = atoi(line_val);
				curr_stats.number_of_windows = (i == 1) ? 1 : 2;
			}
		}
		else if(type == LINE_TYPE_SPLIT_ORIENTATION)
		{
			curr_stats.split = (line_val[0] == 'v') ? VSPLIT : HSPLIT;
		}
		else if(type == LINE_TYPE_SPLIT_POSITION)
		{
			curr_stats.splitter_pos = atof(line_val);
		}
		else if(type == LINE_TYPE_LWIN_SORT)
		{
			get_sort_info(&lwin, line_val);
		}
		else if(type == LINE_TYPE_RWIN_SORT)
		{
			get_sort_info(&rwin, line_val);
		}
		else if(type == LINE_TYPE_LWIN_HIST || type == LINE_TYPE_RWIN_HIST)
		{
			view_t *const view = (type == LINE_TYPE_LWIN_HIST) ? &lwin : &rwin;
			if(line_val[0] == '\0')
			{
				if(!reread && view->history_num > 0)
				{
					copy_str(view->curr_dir, sizeof(view->curr_dir),
							view->history[view->history_pos].dir);
				}
			}
			else if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				const int rel_pos = read_optional_number(fp);
				get_history(view, reread, line_val, line2, rel_pos < 0 ? 0 : rel_pos);
			}
		}
		else if(type == LINE_TYPE_CMDLINE_HIST)
		{
			append_to_history(&curr_stats.cmd_hist, &hists_commands_save, line_val);
		}
		else if(type == LINE_TYPE_SEARCH_HIST)
		{
			append_to_history(&curr_stats.search_hist, &hists_search_save, line_val);
		}
		else if(type == LINE_TYPE_PROMPT_HIST)
		{
			append_to_history(&curr_stats.prompt_hist, &hists_prompt_save, line_val);
		}
		else if(type == LINE_TYPE_FILTER_HIST)
		{
			append_to_history(&curr_stats.filter_hist, &hists_filter_save, line_val);
		}
		else if(type == LINE_TYPE_DIR_STACK)
		{
			if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				if((line3 = read_vifminfo_line(fp, line3)) != NULL)
				{
					if((line4 = read_vifminfo_line(fp, line4)) != NULL)
					{
						dir_stack_push(line_val, line2, line3 + 1, line4);
					}
				}
			}
		}
		else if(type == LINE_TYPE_TRASH)
		{
			if((line2 = read_vifminfo_line(fp, line2)) != NULL)
			{
				char *const trash_name = convert_old_trash_path(line_val);
				(void)add_to_trash(line2, trash_name);
				free(trash_name);
			}
		}
		else if(type == LINE_TYPE_REG)
		{
			regs_append(line_val[0], line_val + 1);
		}
		else if(type == LINE_TYPE_LWIN_FILT)
		{
			set_manual_filter(&lwin, line_val);
		}
		else if(type == LINE_TYPE_RWIN_FILT)
		{
			set_manual_filter(&rwin, line_val);
		}
		else if(type == LINE_TYPE_LWIN_FILT_INV)
		{
			const int i = atoi(line_val);
			lwin.invert = (i != 0);
		}
		else if(type == LINE_TYPE_RWIN_FILT_INV)
		{
			const int i = atoi(line_val);
			rwin.invert = (i != 0);
		}
		else if(type == LINE_TYPE_USE_SCREEN)
		{
			const int i = atoi(line_val);
			cfg_set_use_term_multiplexer(i != 0);
		}
		else if(type == LINE_TYPE_COLORSCHEME)
		{
			copy_str(curr_stats.color_scheme, sizeof(curr_stats.color_scheme),
					line_val);
		}
		else if(type == LINE_TYPE_LWIN_SPECIFIC || type == LINE_TYPE_RWIN_SPECIFIC)
		{
			view_t *view = (type == LINE_TYPE_LWIN_SPECIFIC) ? &lwin : &rwin;
			set_view_property(view, line_val[0], line_val + 1);
		}
	}

	free(line);
	free(line2);
	free(line3);
	free(line4);
	fclose(fp);

	dir_stack_freeze();
}

/* Parses sort description line of the view and initialized its sort field. */
static void
get_sort_info(view_t *view, const char line[])
{
	signed char *const sort = curr_stats.restart_in_progress
	                        ? ui_view_sort_list_get(view, view->sort)
	                        : view->sort;

	int j = 0;
	while(*line != '\0' && j < SK_COUNT)
	{
		char *endptr;
		const int sort_opt = strtol(line, &endptr, 10);
		if(endptr != line)
		{
			line = endptr;
			view->sort_g[j++] = MIN(SK_LAST, MAX(-SK_LAST, sort_opt));
		}
		else
		{
			line++;
		}
		line = skip_char(line, ',');
	}
	memset(&view->sort_g[j], SK_NONE, sizeof(view->sort_g) - j);
	if(j == 0)
	{
		view->sort_g[0] = SK_DEFAULT;
	}
	memcpy(sort, view->sort_g, sizeof(view->sort));

	fview_sorting_updated(view);
}

/* Appends item to the hist extending the history to fit it if needed. */
static void
append_to_history(hist_t *hist, void (*saver)(const char[]),
		const char item[])
{
	ensure_history_not_full(hist);
	saver(item);
}

/* Checks that history has at least one more empty slot or extends history by
 * one more element. */
static void
ensure_history_not_full(hist_t *hist)
{
	if(hist->pos + 1 == cfg.history_len)
	{
		cfg_resize_histories(cfg.history_len + 1);
		assert(hist->pos + 1 != cfg.history_len && "Failed to resize history.");
	}
}

/* Loads single history entry from vifminfo into the view. */
static void
get_history(view_t *view, int reread, const char dir[], const char file[],
		int rel_pos)
{
	const int list_rows = view->list_rows;

	if(view->history_num == cfg.history_len)
	{
		cfg_resize_histories(cfg.history_len + 1);
	}

	if(!reread)
	{
		view->list_rows = 1;
	}
	flist_hist_save(view, dir, file, rel_pos);
	if(!reread)
	{
		view->list_rows = list_rows;
	}
}

/* Sets manual filter of the view and its previous state to given value. */
static void
set_manual_filter(view_t *view, const char value[])
{
	char *error;
	matcher_t *matcher;

	(void)replace_string(&view->prev_manual_filter, value);
	matcher = matcher_alloc(value, FILTER_DEF_CASE_SENSITIVITY, 0, "", &error);
	free(error);

	/* If setting filter value has failed, try to setup an empty value instead. */
	if(matcher == NULL)
	{
		(void)replace_string(&view->prev_manual_filter, "");
		matcher = matcher_alloc("", FILTER_DEF_CASE_SENSITIVITY, 0, "", &error);
		free(error);
		assert(matcher != NULL && "Can't init manual filter.");
	}

	matcher_free(view->manual_filter);
	view->manual_filter = matcher;
}

/* Sets view property specified by the type to the value. */
static void
set_view_property(view_t *view, char type, const char value[])
{
	if(type == PROP_TYPE_DOTFILES)
	{
		dot_filter_set(view, !atoi(value));
	}
	else if(type == PROP_TYPE_AUTO_FILTER)
	{
		if(filter_set(&view->auto_filter, value) != 0)
		{
			LOG_ERROR_MSG("Error setting auto filename filter to: %s", value);
		}
	}
	else
	{
		LOG_ERROR_MSG("Unknown view property type (%c) with value: %s", type,
				value);
	}
}

void
write_info_file(void)
{
	char info_file[PATH_MAX + 16];
	char tmp_file[PATH_MAX + 16];

	(void)snprintf(info_file, sizeof(info_file), "%s/vifminfo", cfg.config_dir);
	(void)snprintf(tmp_file, sizeof(tmp_file), "%s_%u", info_file, get_pid());

	if(os_access(info_file, R_OK) != 0 || copy_file(info_file, tmp_file) == 0)
	{
		filemon_t current_vifminfo_mon;
		int vifminfo_changed;

		vifminfo_changed =
			filemon_from_file(info_file, FMT_MODIFIED, &current_vifminfo_mon) != 0 ||
			!filemon_equal(&vifminfo_mon, &current_vifminfo_mon);

		update_info_file(tmp_file, vifminfo_changed);
		(void)filemon_from_file(tmp_file, FMT_MODIFIED, &vifminfo_mon);

		if(rename_file(tmp_file, info_file) != 0)
		{
			LOG_ERROR_MSG("Can't replace vifminfo file with its temporary copy");
			(void)remove(tmp_file);
		}
	}
}

/* Copies the src file to the dst location.  Returns zero on success. */
static int
copy_file(const char src[], const char dst[])
{
	FILE *const src_fp = os_fopen(src, "rb");
	FILE *const dst_fp = os_fopen(dst, "wb");
	int result;

	result = copy_file_internal(src_fp, dst_fp);

	if(dst_fp != NULL)
	{
		(void)fclose(dst_fp);
	}
	if(src_fp != NULL)
	{
		(void)fclose(src_fp);
	}

	if(result != 0)
	{
		(void)remove(dst);
	}

	return result;
}

/* Internal sub-function of the copy_file() function.  Returns zero on
 * success. */
static int
copy_file_internal(FILE *src, FILE *dst)
{
	char buffer[4*1024];
	size_t nread;

	if(src == NULL || dst == NULL)
	{
		return 1;
	}

	while((nread = fread(&buffer[0], 1, sizeof(buffer), src)))
	{
		if(fwrite(&buffer[0], 1, nread, dst) != nread)
		{
			break;
		}
	}

	return nread > 0;
}

/* Reads contents of the filename file as an info file and updates it with the
 * state of current instance. */
static void
update_info_file(const char filename[], int merge)
{
	/* TODO: refactor this function update_info_file() */

	FILE *fp;
	char **cmds_list;
	int ncmds_list = -1;
	char **ft = NULL, **fx = NULL, **fv = NULL, **cmds = NULL, **marks = NULL;
	char **lh = NULL, **rh = NULL, **cmdh = NULL, **srch = NULL, **regs = NULL;
	int *lhp = NULL, *rhp = NULL, *bt = NULL, *bmt = NULL;
	char **prompt = NULL, **filter = NULL, **trash = NULL;
	char **bmarks = NULL;
	int nft = 0, nfx = 0, nfv = 0, ncmds = 0, nmarks = 0, nlh = 0, nrh = 0;
	int ncmdh = 0, nsrch = 0, nregs = 0, nprompt = 0, nfilter = 0, ntrash = 0;
	int nbmarks = 0;
	char **dir_stack = NULL;
	int ndir_stack = 0;
	char *non_conflicting_marks;

	if(cfg.vifm_info == 0)
		return;

	cmds_list = vle_cmds_list_udcs();
	ncmds_list = count_strings(cmds_list);

	non_conflicting_marks = strdup(valid_marks);

	if(merge && (fp = os_fopen(filename, "r")) != NULL)
	{
		size_t nlhp = 0UL, nrhp = 0UL, nbt = 0UL, nbmt = 0UL;
		char *line = NULL, *line2 = NULL, *line3 = NULL, *line4 = NULL;
		while((line = read_vifminfo_line(fp, line)) != NULL)
		{
			const char type = line[0];
			const char *const line_val = line + 1;

			if(type == LINE_TYPE_COMMENT || type == '\0')
				continue;

			if(type == LINE_TYPE_FILETYPE)
			{
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					if(!ft_assoc_exists(&filetypes, line_val, line2))
					{
						nft = add_to_string_array(&ft, nft, 2, line_val, line2);
					}
				}
			}
			else if(type == LINE_TYPE_XFILETYPE)
			{
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					if(!ft_assoc_exists(&xfiletypes, line_val, line2))
					{
						nfx = add_to_string_array(&fx, nfx, 2, line_val, line2);
					}
				}
			}
			else if(type == LINE_TYPE_FILEVIEWER)
			{
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					if(!ft_assoc_exists(&fileviewers, line_val, line2))
					{
						nfv = add_to_string_array(&fv, nfv, 2, line_val, line2);
					}
				}
			}
			else if(type == LINE_TYPE_COMMAND)
			{
				if(line_val[0] == '\0')
					continue;
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					int i;
					const char *p = line_val;
					for(i = 0; i < ncmds_list; i += 2)
					{
						int cmp = strcmp(cmds_list[i], p);
						if(cmp < 0)
							continue;
						if(cmp == 0)
							p = NULL;
						break;
					}
					if(p == NULL)
						continue;
					ncmds = add_to_string_array(&cmds, ncmds, 2, line_val, line2);
				}
			}
			else if(type == LINE_TYPE_LWIN_HIST || type == LINE_TYPE_RWIN_HIST)
			{
				if(line_val[0] == '\0')
					continue;
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					const int pos = read_optional_number(fp);

					if(type == LINE_TYPE_LWIN_HIST)
					{
						process_hist_entry(&lwin, line_val, line2, pos, &lh, &nlh, &lhp,
								&nlhp);
					}
					else
					{
						process_hist_entry(&rwin, line_val, line2, pos, &rh, &nrh, &rhp,
								&nrhp);
					}
				}
			}
			else if(type == LINE_TYPE_MARK)
			{
				const char mark = line_val[0];
				if(line_val[1] != '\0')
				{
					LOG_ERROR_MSG("Expected end of line, but got: %s", line_val + 1);
				}
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					if((line3 = read_vifminfo_line(fp, line3)) != NULL)
					{
						const int timestamp = read_optional_number(fp);
						const char mark_str[] = { mark, '\0' };

						if(!char_is_one_of(valid_marks, mark))
						{
							continue;
						}

						if(is_mark_older(mark, timestamp))
						{
							char *const pos = strchr(non_conflicting_marks, mark);
							if(pos != NULL)
							{
								nmarks = add_to_string_array(&marks, nmarks, 3, mark_str, line2,
										line3);
								nbt = add_to_int_array(&bt, nbt, timestamp);

								*pos = '\xff';
							}
						}
					}
				}
			}
			else if(type == LINE_TYPE_BOOKMARK)
			{
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					if((line3 = read_vifminfo_line(fp, line3)) != NULL)
					{
						long timestamp;
						if(read_number(line3, &timestamp) &&
								bmark_is_older(line_val, timestamp))
						{
							nbmarks = add_to_string_array(&bmarks, nbmarks, 2, line_val,
									line2);
							nbmt = add_to_int_array(&bmt, nbmt, timestamp);
						}
					}
				}
			}
			else if(type == LINE_TYPE_TRASH)
			{
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					char *const trash_name = convert_old_trash_path(line_val);
					if(!trash_includes(line2))
					{
						ntrash = add_to_string_array(&trash, ntrash, 2, trash_name, line2);
					}
					free(trash_name);
				}
			}
			else if(type == LINE_TYPE_CMDLINE_HIST)
			{
				if(!hist_contains(&curr_stats.cmd_hist, line_val))
				{
					ncmdh = add_to_string_array(&cmdh, ncmdh, 1, line_val);
				}
			}
			else if(type == LINE_TYPE_SEARCH_HIST)
			{
				if(!hist_contains(&curr_stats.search_hist, line_val))
				{
					nsrch = add_to_string_array(&srch, nsrch, 1, line_val);
				}
			}
			else if(type == LINE_TYPE_PROMPT_HIST)
			{
				if(!hist_contains(&curr_stats.prompt_hist, line_val))
				{
					nprompt = add_to_string_array(&prompt, nprompt, 1, line_val);
				}
			}
			else if(type == LINE_TYPE_FILTER_HIST)
			{
				if(!hist_contains(&curr_stats.filter_hist, line_val))
				{
					nfilter = add_to_string_array(&filter, nfilter, 1, line_val);
				}
			}
			else if(type == LINE_TYPE_DIR_STACK)
			{
				if((line2 = read_vifminfo_line(fp, line2)) != NULL)
				{
					if((line3 = read_vifminfo_line(fp, line3)) != NULL)
					{
						if((line4 = read_vifminfo_line(fp, line4)) != NULL)
						{
							ndir_stack = add_to_string_array(&dir_stack, ndir_stack, 4,
									line_val, line2, line3 + 1, line4);
						}
					}
				}
			}
			else if(type == LINE_TYPE_REG)
			{
				if(regs_exists(line_val[0]))
				{
					continue;
				}
				nregs = add_to_string_array(&regs, nregs, 1, line);
			}
		}
		free(line);
		free(line2);
		free(line3);
		free(line4);
		fclose(fp);
	}

	if((fp = os_fopen(filename, "w")) != NULL)
	{
		fprintf(fp, "# You can edit this file by hand, but it's recommended not to "
				"do that.\n");

		if(cfg.vifm_info & VINFO_OPTIONS)
		{
			write_options(fp);
		}

		if(cfg.vifm_info & VINFO_FILETYPES)
		{
			write_assocs(fp, "Filetypes", LINE_TYPE_FILETYPE, &filetypes, nft, ft);
			write_assocs(fp, "X Filetypes", LINE_TYPE_XFILETYPE, &xfiletypes, nfx,
					fx);
			write_assocs(fp, "Fileviewers", LINE_TYPE_FILEVIEWER, &fileviewers, nfv,
					fv);
		}

		if(cfg.vifm_info & VINFO_COMMANDS)
		{
			write_commands(fp, cmds_list, cmds, ncmds);
		}

		if(cfg.vifm_info & VINFO_MARKS)
		{
			write_marks(fp, non_conflicting_marks, marks, bt, nmarks);
		}

		if(cfg.vifm_info & VINFO_BOOKMARKS)
		{
			write_bmarks(fp, bmarks, bmt, nbmarks);
		}

		if(cfg.vifm_info & VINFO_TUI)
		{
			write_tui_state(fp);
		}

		if((cfg.vifm_info & VINFO_DHISTORY) && cfg.history_len > 0)
		{
			write_view_history(fp, &lwin, "Left", LINE_TYPE_LWIN_HIST, nlh, lh, lhp);
			write_view_history(fp, &rwin, "Right", LINE_TYPE_RWIN_HIST, nrh, rh, rhp);
		}

		if(cfg.vifm_info & VINFO_CHISTORY)
		{
			write_history(fp, "Command line", LINE_TYPE_CMDLINE_HIST,
					MIN(ncmdh, cfg.history_len - curr_stats.cmd_hist.pos), cmdh,
					&curr_stats.cmd_hist);
		}

		if(cfg.vifm_info & VINFO_SHISTORY)
		{
			write_history(fp, "Search", LINE_TYPE_SEARCH_HIST, nsrch, srch,
					&curr_stats.search_hist);
		}

		if(cfg.vifm_info & VINFO_PHISTORY)
		{
			write_history(fp, "Prompt", LINE_TYPE_PROMPT_HIST, nprompt, prompt,
					&curr_stats.prompt_hist);
		}

		if(cfg.vifm_info & VINFO_FHISTORY)
		{
			write_history(fp, "Local filter", LINE_TYPE_FILTER_HIST, nfilter, filter,
					&curr_stats.filter_hist);
		}

		if(cfg.vifm_info & VINFO_REGISTERS)
		{
			write_registers(fp, regs, nregs);
		}

		if(cfg.vifm_info & VINFO_DIRSTACK)
		{
			write_dir_stack(fp, dir_stack, ndir_stack);
		}

		write_trash(fp, trash, ntrash);

		if(cfg.vifm_info & VINFO_STATE)
		{
			write_general_state(fp);
		}

		if(cfg.vifm_info & VINFO_CS)
		{
			fputs("\n# Color scheme:\n", fp);
			fprintf(fp, "c%s\n", cfg.cs.name);
		}

		fclose(fp);
	}

	free_string_array(ft, nft);
	free_string_array(fv, nfv);
	free_string_array(fx, nfx);
	free_string_array(cmds, ncmds);
	free_string_array(marks, nmarks);
	free_string_array(cmds_list, ncmds_list);
	free_string_array(lh, nlh);
	free_string_array(rh, nrh);
	free(lhp);
	free(rhp);
	free(bt);
	free(bmt);
	free_string_array(cmdh, ncmdh);
	free_string_array(srch, nsrch);
	free_string_array(regs, nregs);
	free_string_array(prompt, nprompt);
	free_string_array(filter, nfilter);
	free_string_array(trash, ntrash);
	free_string_array(bmarks, nbmarks);
	free_string_array(dir_stack, ndir_stack);
	free(non_conflicting_marks);
}

/* Handles single directory history entry, possibly skipping merging it in. */
static void
process_hist_entry(view_t *view, const char dir[], const char file[], int pos,
		char ***lh, int *nlh, int **lhp, size_t *nlhp)
{
	if(view->history_pos + *nlh/2 == cfg.history_len - 1 ||
			flist_hist_contains(view, dir) || !is_dir(dir))
	{
		return;
	}

	*nlh = add_to_string_array(lh, *nlh, 2, dir, file);
	if(*nlh/2U > *nlhp)
	{
		*nlhp = add_to_int_array(lhp, *nlhp, pos);
		*nlhp = MIN(*nlh/2U, *nlhp);
	}
}

/* Performs conversions on files in trash required for partial backward
 * compatibility.  Returns newly allocated string that should be freed by the
 * caller. */
static char *
convert_old_trash_path(const char trash_path[])
{
	if(!is_path_absolute(trash_path) && is_dir_writable(cfg.trash_dir))
	{
		char *const full_path = format_str("%s/%s", cfg.trash_dir, trash_path);
		if(path_exists(full_path, NODEREF))
		{
			return full_path;
		}
		free(full_path);
	}
	return strdup(trash_path);
}

/* Writes current values of all options into vifminfo file. */
static void
write_options(FILE *fp)
{
	const char *str;

	fputs("\n# Options:\n", fp);
	fprintf(fp, "=aproposprg=%s\n", escape_spaces(cfg.apropos_prg));
	fprintf(fp, "=%sautochpos\n", cfg.auto_ch_pos ? "" : "no");
	fprintf(fp, "=cdpath=%s\n", cfg.cd_path);
	fprintf(fp, "=%schaselinks\n", cfg.chase_links ? "" : "no");
	fprintf(fp, "=columns=%d\n", cfg.columns);
	fprintf(fp, "=cpoptions=%s\n",
			escape_spaces(vle_opts_get("cpoptions", OPT_GLOBAL)));
	fprintf(fp, "=deleteprg=%s\n", escape_spaces(cfg.delete_prg));
	fprintf(fp, "=%sfastrun\n", cfg.fast_run ? "" : "no");
	if(strcmp(cfg.border_filler, " ") != 0)
	{
		fprintf(fp, "=fillchars+=vborder:%s\n", cfg.border_filler);
	}
	fprintf(fp, "=findprg=%s\n", escape_spaces(cfg.find_prg));
	fprintf(fp, "=%sfollowlinks\n", cfg.follow_links ? "" : "no");
	fprintf(fp, "=fusehome=%s\n", escape_spaces(cfg.fuse_home));
	fprintf(fp, "=%sgdefault\n", cfg.gdefault ? "" : "no");
	fprintf(fp, "=grepprg=%s\n", escape_spaces(cfg.grep_prg));
	fprintf(fp, "=histcursor=%s\n",
			escape_spaces(vle_opts_get("histcursor", OPT_GLOBAL)));
	fprintf(fp, "=history=%d\n", cfg.history_len);
	fprintf(fp, "=%shlsearch\n", cfg.hl_search ? "" : "no");
	fprintf(fp, "=%siec\n", cfg.sizefmt.ieci_prefixes ? "" : "no");
	fprintf(fp, "=%signorecase\n", cfg.ignore_case ? "" : "no");
	fprintf(fp, "=%sincsearch\n", cfg.inc_search ? "" : "no");
	fprintf(fp, "=%slaststatus\n", cfg.display_statusline ? "" : "no");
	fprintf(fp, "=%stitle\n", cfg.set_title ? "" : "no");
	fprintf(fp, "=lines=%d\n", cfg.lines);
	fprintf(fp, "=locateprg=%s\n", escape_spaces(cfg.locate_prg));
	fprintf(fp, "=mediaprg=%s\n", escape_spaces(cfg.media_prg));
	fprintf(fp, "=mintimeoutlen=%d\n", cfg.min_timeout_len);
	fprintf(fp, "=%squickview\n", curr_stats.preview.on ? "" : "no");
	fprintf(fp, "=rulerformat=%s\n", escape_spaces(cfg.ruler_format));
	fprintf(fp, "=%srunexec\n", cfg.auto_execute ? "" : "no");
	fprintf(fp, "=%sscrollbind\n", cfg.scroll_bind ? "" : "no");
	fprintf(fp, "=scrolloff=%d\n", cfg.scroll_off);
	fprintf(fp, "=shell=%s\n", escape_spaces(cfg.shell));
	fprintf(fp, "=shellcmdflag=%s\n", escape_spaces(cfg.shell_cmd_flag));
	fprintf(fp, "=shortmess=%s\n",
			escape_spaces(vle_opts_get("shortmess", OPT_GLOBAL)));
	fprintf(fp, "=showtabline=%s\n",
			escape_spaces(vle_opts_get("showtabline", OPT_GLOBAL)));
	fprintf(fp, "=sizefmt=%s\n",
			escape_spaces(vle_opts_get("sizefmt", OPT_GLOBAL)));
#ifndef _WIN32
	fprintf(fp, "=slowfs=%s\n", escape_spaces(cfg.slow_fs_list));
#endif
	fprintf(fp, "=%ssmartcase\n", cfg.smart_case ? "" : "no");
	fprintf(fp, "=%ssortnumbers\n", cfg.sort_numbers ? "" : "no");
	fprintf(fp, "=statusline=%s\n", escape_spaces(cfg.status_line));
	fprintf(fp, "=syncregs=%s\n",
			escape_spaces(vle_opts_get("syncregs", OPT_GLOBAL)));
	fprintf(fp, "=tabscope=%s\n",
			escape_spaces(vle_opts_get("tabscope", OPT_GLOBAL)));
	fprintf(fp, "=tabstop=%d\n", cfg.tab_stop);
	fprintf(fp, "=timefmt=%s\n", escape_spaces(cfg.time_format));
	fprintf(fp, "=timeoutlen=%d\n", cfg.timeout_len);
	fprintf(fp, "=%strash\n", cfg.use_trash ? "" : "no");
	fprintf(fp, "=tuioptions=%s\n",
			escape_spaces(vle_opts_get("tuioptions", OPT_GLOBAL)));
	fprintf(fp, "=undolevels=%d\n", cfg.undo_levels);
	fprintf(fp, "=vicmd=%s%s\n", escape_spaces(cfg.vi_command),
			cfg.vi_cmd_bg ? " &" : "");
	fprintf(fp, "=vixcmd=%s%s\n", escape_spaces(cfg.vi_x_command),
			cfg.vi_cmd_bg ? " &" : "");
	fprintf(fp, "=%swrapscan\n", cfg.wrap_scan ? "" : "no");
	fprintf(fp, "=[viewcolumns=%s\n", escape_spaces(lwin.view_columns_g));
	fprintf(fp, "=]viewcolumns=%s\n", escape_spaces(rwin.view_columns_g));
	fprintf(fp, "=[sortgroups=%s\n", escape_spaces(lwin.sort_groups_g));
	fprintf(fp, "=]sortgroups=%s\n", escape_spaces(rwin.sort_groups_g));
	fprintf(fp, "=[lsoptions=%s\n", lwin.ls_transposed_g ? "transposed" : "");
	fprintf(fp, "=]lsoptions=%s\n", rwin.ls_transposed_g ? "transposed" : "");
	fprintf(fp, "=[%slsview\n", lwin.ls_view_g ? "" : "no");
	fprintf(fp, "=]%slsview\n", rwin.ls_view_g ? "" : "no");
	fprintf(fp, "=[milleroptions=lsize:%d,csize:%d,rsize:%d\n",
			lwin.miller_ratios_g[0], lwin.miller_ratios_g[1],
			lwin.miller_ratios_g[2]);
	fprintf(fp, "=]milleroptions=lsize:%d,csize:%d,rsize:%d\n",
			rwin.miller_ratios_g[0], rwin.miller_ratios_g[1],
			rwin.miller_ratios_g[2]);
	fprintf(fp, "=[%smillerview\n", lwin.miller_view_g ? "" : "no");
	fprintf(fp, "=]%smillerview\n", rwin.miller_view_g ? "" : "no");
	fprintf(fp, "=[%snumber\n", (lwin.num_type_g & NT_SEQ) ? "" : "no");
	fprintf(fp, "=]%snumber\n", (rwin.num_type_g & NT_SEQ) ? "" : "no");
	fprintf(fp, "=[numberwidth=%d\n", lwin.num_width_g);
	fprintf(fp, "=]numberwidth=%d\n", rwin.num_width_g);
	fprintf(fp, "=[%srelativenumber\n", (lwin.num_type_g & NT_REL) ? "" : "no");
	fprintf(fp, "=]%srelativenumber\n", (rwin.num_type_g & NT_REL) ? "" : "no");
	fprintf(fp, "=[%sdotfiles\n", lwin.hide_dot_g ? "" : "no");
	fprintf(fp, "=]%sdotfiles\n", rwin.hide_dot_g ? "" : "no");
	fprintf(fp, "=[previewprg=%s\n", escape_spaces(lwin.preview_prg_g));
	fprintf(fp, "=]previewprg=%s\n", escape_spaces(rwin.preview_prg_g));

	fprintf(fp, "=confirm=%s\n",
			escape_spaces(vle_opts_get("confirm", OPT_GLOBAL)));
	fprintf(fp, "=dotdirs=%s\n",
			escape_spaces(vle_opts_get("dotdirs", OPT_GLOBAL)));
	fprintf(fp, "=caseoptions=%s\n",
			escape_spaces(vle_opts_get("caseoptions", OPT_GLOBAL)));
	fprintf(fp, "=suggestoptions=%s\n",
			escape_spaces(vle_opts_get("suggestoptions", OPT_GLOBAL)));
	fprintf(fp, "=iooptions=%s\n",
			escape_spaces(vle_opts_get("iooptions", OPT_GLOBAL)));

	fprintf(fp, "=dirsize=%s", cfg.view_dir_size == VDS_SIZE ? "size" : "nitems");

	str = classify_to_str();
	fprintf(fp, "=classify=%s\n", escape_spaces(str == NULL ? "" : str));

	fprintf(fp, "=vifminfo=%s\n",
			escape_spaces(vle_opts_get("vifminfo", OPT_GLOBAL)));

	fprintf(fp, "=%svimhelp\n", cfg.use_vim_help ? "" : "no");
	fprintf(fp, "=%swildmenu\n", cfg.wild_menu ? "" : "no");
	fprintf(fp, "=wildstyle=%s\n", cfg.wild_popup ? "popup" : "bar");
	fprintf(fp, "=wordchars=%s\n",
			escape_spaces(vle_opts_get("wordchars", OPT_GLOBAL)));
	fprintf(fp, "=%swrap\n", cfg.wrap_quick_view ? "" : "no");
}

/* Stores list of associations to the file. */
static void
write_assocs(FILE *fp, const char str[], char mark, assoc_list_t *assocs,
		int prev_count, char *prev[])
{
	int i;

	fprintf(fp, "\n# %s:\n", str);

	for(i = 0; i < assocs->count; ++i)
	{
		int j;

		assoc_t assoc = assocs->list[i];

		for(j = 0; j < assoc.records.count; ++j)
		{
			assoc_record_t ft_record = assoc.records.list[j];

			/* The type check is to prevent builtin fake associations to be written
			 * into vifminfo file. */
			if(ft_record.command[0] == '\0' || ft_record.type == ART_BUILTIN)
			{
				continue;
			}

			if(ft_record.description[0] == '\0')
			{
				fprintf(fp, "%c%s\n\t", mark, matchers_get_expr(assoc.matchers));
			}
			else
			{
				fprintf(fp, "%c%s\n\t{%s}", mark, matchers_get_expr(assoc.matchers),
						ft_record.description);
			}
			write_doubling_commas(fp, ft_record.command);
			fputc('\n', fp);
		}
	}

	for(i = 0; i < prev_count; i += 2)
	{
		fprintf(fp, "%c%s\n\t%s\n", mark, prev[i], prev[i + 1]);
	}
}

/* Prints the string into the file doubling commas in process. */
static void
write_doubling_commas(FILE *fp, const char str[])
{
	while(*str != '\0')
	{
		if(*str == ',')
		{
			fputc(',', fp);
		}
		fputc(*str++, fp);
	}
}

/* Writes user-defined commands to vifminfo file.  cmds_list is a NULL
 * terminated list of commands existing in current session, cmds is a list of
 * length ncmds with unseen commands read from vifminfo. */
static void
write_commands(FILE *fp, char *cmds_list[], char *cmds[], int ncmds)
{
	int i;

	fputs("\n# Commands:\n", fp);
	for(i = 0; cmds_list[i] != NULL; i += 2)
	{
		fprintf(fp, "!%s\n\t%s\n", cmds_list[i], cmds_list[i + 1]);
	}
	for(i = 0; i < ncmds; i += 2)
	{
		fprintf(fp, "!%s\n\t%s\n", cmds[i], cmds[i + 1]);
	}
}

/* Writes marks to vifminfo file.  marks is a list of length nmarks marks read
 * from vifminfo. */
static void
write_marks(FILE *fp, const char non_conflicting_marks[],
		char *marks[], const int timestamps[], int nmarks)
{
	int active_marks[NUM_MARKS];
	const int len = init_active_marks(valid_marks, active_marks);
	int i;

	fputs("\n# Marks:\n", fp);
	for(i = 0; i < len; ++i)
	{
		const int index = active_marks[i];
		const char m = index2mark(index);
		if(!is_spec_mark(index) && char_is_one_of(non_conflicting_marks, m))
		{
			const mark_t *const mark = get_mark(index);

			fprintf(fp, "%c%c\n", LINE_TYPE_MARK, m);
			fprintf(fp, "\t%s\n", mark->directory);
			fprintf(fp, "\t%s\n", mark->file);
			fprintf(fp, "%lld\n", (long long)mark->timestamp);
		}
	}
	for(i = 0; i < nmarks; i += 3)
	{
		fprintf(fp, "%c%c\n", LINE_TYPE_MARK, marks[i][0]);
		fprintf(fp, "\t%s\n", marks[i + 1]);
		fprintf(fp, "\t%s\n", marks[i + 2]);
		fprintf(fp, "%d\n", timestamps[i/3]);
	}
}

/* Writes bookmarks to vifminfo file.  bmarks is a list of length nbmarks marks
 * read from vifminfo. */
static void
write_bmarks(FILE *fp, char *bmarks[], const int timestamps[],
		int nbmarks)
{
	int i;

	fputs("\n# Bookmarks:\n", fp);

	bmarks_list(&write_bmark, fp);

	for(i = 0; i < nbmarks; i += 2)
	{
		fprintf(fp, "%c%s\n", LINE_TYPE_BOOKMARK, bmarks[i]);
		fprintf(fp, "\t%s\n", bmarks[i + 1]);
		fprintf(fp, "\t%d\n", timestamps[i/2]);
	}
}

/* bmarks_list() callback that writes a bookmark into vifminfo. */
static void
write_bmark(const char path[], const char tags[], time_t timestamp, void *arg)
{
	FILE *const fp = arg;
	fprintf(fp, "%c%s\n", LINE_TYPE_BOOKMARK, path);
	fprintf(fp, "\t%s\n", tags);
	fprintf(fp, "\t%d\n", (int)timestamp);
}

/* Writes state of the TUI to vifminfo file. */
static void
write_tui_state(FILE *fp)
{
	fputs("\n# TUI:\n", fp);
	fprintf(fp, "a%c\n", (curr_view == &rwin) ? 'r' : 'l');
	fprintf(fp, "q%u\n", curr_stats.preview.on);
	fprintf(fp, "v%d\n", curr_stats.number_of_windows);
	fprintf(fp, "o%c\n", (curr_stats.split == VSPLIT) ? 'v' : 'h');
	fprintf(fp, "m%d\n", curr_stats.splitter_pos);

	put_sort_info(fp, 'l', &lwin);
	put_sort_info(fp, 'r', &rwin);
}

/* Stores history of the view to the file. */
static void
write_view_history(FILE *fp, view_t *view, const char str[], char mark,
		int prev_count, char *prev[], int pos[])
{
	int i;
	flist_hist_save(view, NULL, NULL, -1);
	fprintf(fp, "\n# %s window history (oldest to newest):\n", str);
	for(i = 0; i < prev_count; i += 2)
	{
		fprintf(fp, "%c%s\n\t%s\n%d\n", mark, prev[i], prev[i + 1], pos[i/2]);
	}
	for(i = 0; i <= view->history_pos && i < view->history_num; i++)
	{
		fprintf(fp, "%c%s\n\t%s\n%d\n", mark, view->history[i].dir,
				view->history[i].file, view->history[i].rel_pos);
	}
	if(cfg.vifm_info & VINFO_SAVEDIRS)
	{
		fprintf(fp, "%c\n", mark);
	}
}

/* Stores history items to the file. */
static void
write_history(FILE *fp, const char str[], char mark, int prev_count,
		char *prev[], const hist_t *hist)
{
	int i;
	fprintf(fp, "\n# %s history (oldest to newest):\n", str);
	for(i = 0; i < prev_count; i++)
	{
		fprintf(fp, "%c%s\n", mark, prev[i]);
	}
	for(i = hist->pos; i >= 0; i--)
	{
		fprintf(fp, "%c%s\n", mark, hist->items[i]);
	}
}

/* Writes registers to vifminfo file.  regs is a list of length nregs registers
 * read from vifminfo. */
static void
write_registers(FILE *fp, char *regs[], int nregs)
{
	int i;

	fputs("\n# Registers:\n", fp);
	for(i = 0; i < nregs; i++)
	{
		fprintf(fp, "%s\n", regs[i]);
	}
	for(i = 0; valid_registers[i] != '\0'; i++)
	{
		const reg_t *const reg = regs_find(valid_registers[i]);
		if(reg != NULL)
		{
			int j;
			for(j = 0; j < reg->nfiles; ++j)
			{
				if(reg->files[j] != NULL)
				{
					fprintf(fp, "\"%c%s\n", reg->name, reg->files[j]);
				}
			}
		}
	}
}

/* Writes directory stack to vifminfo file.  old_dir_stack is a list of length
 * nold_dir_stack entries (4 lines per entry) read from vifminfo. */
static void
write_dir_stack(FILE *fp, char *old_dir_stack[], int nold_dir_stack)
{
	fputs("\n# Directory stack (oldest to newest):\n", fp);
	if(dir_stack_changed())
	{
		unsigned int i;
		for(i = 0U; i < dir_stack_top; ++i)
		{
			dir_stack_entry_t *const entry = &dir_stack[i];
			fprintf(fp, "S%s\n\t%s\n", entry->lpane_dir, entry->lpane_file);
			fprintf(fp, "S%s\n\t%s\n", entry->rpane_dir, entry->rpane_file);
		}
	}
	else
	{
		int i;
		for(i = 0; i < nold_dir_stack; i += 4)
		{
			fprintf(fp, "S%s\n\t%s\n", old_dir_stack[i], old_dir_stack[i + 1]);
			fprintf(fp, "S%s\n\t%s\n", old_dir_stack[i + 2], old_dir_stack[i + 3]);
		}
	}
}

/* Writes trash entries to vifminfo file.  trash is a list of length ntrash
 * entries read from vifminfo. */
static void
write_trash(FILE *fp, char *trash[], int ntrash)
{
	int i;
	fputs("\n# Trash content:\n", fp);
	for(i = 0; i < nentries; i++)
	{
		fprintf(fp, "t%s\n\t%s\n", trash_list[i].trash_name, trash_list[i].path);
	}
	for(i = 0; i < ntrash; i += 2)
	{
		fprintf(fp, "t%s\n\t%s\n", trash[i], trash[i + 1]);
	}
}

/* Writes general state to vifminfo file. */
static void
write_general_state(FILE *fp)
{
	fputs("\n# State:\n", fp);
	fprintf(fp, "f%s\n", matcher_get_expr(lwin.manual_filter));
	fprintf(fp, "i%d\n", lwin.invert);
	fprintf(fp, "[.%d\n", lwin.hide_dot);
	fprintf(fp, "[F%s\n", lwin.auto_filter.raw);
	fprintf(fp, "F%s\n", matcher_get_expr(rwin.manual_filter));
	fprintf(fp, "I%d\n", rwin.invert);
	fprintf(fp, "].%d\n", rwin.hide_dot);
	fprintf(fp, "]F%s\n", rwin.auto_filter.raw);
	fprintf(fp, "s%d\n", cfg.use_term_multiplexer);
}

/* Reads line from configuration file.  Takes care of trailing newline character
 * (removes it) and leading whitespace.  Buffer should be NULL or valid memory
 * buffer allocated on heap.  Returns reallocated buffer or NULL on error or
 * when end of file is reached. */
static char *
read_vifminfo_line(FILE *fp, char buffer[])
{
	if((buffer = read_line(fp, buffer)) != NULL)
	{
		remove_leading_whitespace(buffer);
	}
	return buffer;
}

/* Removes leading whitespace from the line in place. */
static void
remove_leading_whitespace(char line[])
{
	const char *const non_whitespace = skip_whitespace(line);
	if(non_whitespace != line)
	{
		memmove(line, non_whitespace, strlen(non_whitespace) + 1);
	}
}

/* Escapes spaces in the string.  Returns pointer to a statically allocated
 * buffer. */
static const char *
escape_spaces(const char str[])
{
	static char buf[4096];
	char *p;

	p = buf;
	while(*str != '\0')
	{
		if(*str == '\\' || *str == ' ')
			*p++ = '\\';
		*p++ = *str;

		str++;
	}
	*p = '\0';
	return buf;
}

/* Writes sort description line of the view to the fp file prepending the
 * leading_char to it. */
static void
put_sort_info(FILE *fp, char leading_char, const view_t *view)
{
	int i = -1;
	const signed char *const sort = ui_view_sort_list_get(view, view->sort_g);

	fputc(leading_char, fp);
	while(++i < SK_COUNT && abs(sort[i]) <= SK_LAST)
	{
		int is_last_option = i >= SK_COUNT - 1 || abs(sort[i + 1]) > SK_LAST;
		fprintf(fp, "%d%s", sort[i], is_last_option ? "" : ",");
	}
	fputc('\n', fp);
}

/* Ensures that the next character of the stream is a digit and reads a number.
 * Returns read number or -1 in case there is no digit. */
static int
read_optional_number(FILE *f)
{
	int num;
	int nread;
	int c;
	fpos_t pos;

	if(fgetpos(f, &pos) != 0)
	{
		return -1;
	}

	c = getc(f);
	if(c == EOF)
	{
		return -1;
	}
	ungetc(c, f);

	if(!isdigit(c) && c != '-' && c != '+')
	{
		return -1;
	}

	nread = fscanf(f, "%30d\n", &num);
	if(nread != 1)
	{
		fsetpos(f, &pos);
		return -1;
	}

	return num;
}

/* Converts line to number.  Returns non-zero on success and zero otherwise. */
static int
read_number(const char line[], long *value)
{
	char *endptr;
	*value = strtol(line, &endptr, 10);
	return *line != '\0' && *endptr == '\0';
}

static size_t
add_to_int_array(int **array, size_t len, int what)
{
	int *p;

	p = reallocarray(*array, len + 1, sizeof(*p));
	if(p != NULL)
	{
		*array = p;
		(*array)[len++] = what;
	}

	return len;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
