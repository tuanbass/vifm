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

#include "find_menu.h"

#include <stdlib.h> /* free() */
#include <string.h> /* strdup() */

#include "../cfg/config.h"
#include "../modes/dialogs/msg_dialog.h"
#include "../ui/statusbar.h"
#include "../ui/ui.h"
#include "../utils/macros.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../macros.h"
#include "menus.h"


static int execute_find_cb(view_t *view, menu_data_t *m);

int
show_find_menu(view_t *view, int with_path, const char args[])
{
	enum { M_s, M_a, M_A, M_u, M_U, };

	int save_msg;
	char *custom_args = NULL;
	char *targets = NULL;
	char *cmd;

	custom_macro_t macros[] = {
		[M_s] = { .letter = 's', .value = NULL, .uses_left = 1, .group = -1 },
		[M_a] = { .letter = 'a', .value = NULL, .uses_left = 1, .group =  1 },
		[M_A] = { .letter = 'A', .value = NULL, .uses_left = 0, .group =  1 },

		[M_u] = { .letter = 'u', .value = "",   .uses_left = 1, .group = -1 },
		[M_U] = { .letter = 'U', .value = "",   .uses_left = 1, .group = -1 },
	};

	static menu_data_t m;

	if(with_path)
	{
		macros[M_s].value = args;
		macros[M_a].value = "";
		macros[M_A].value = "";
	}
	else
	{
		targets = menus_get_targets(view);
		if(targets == NULL)
		{
			show_error_msg("Find", "Failed to setup target directory.");
			return 0;
		}

		macros[M_s].value = targets;
		macros[M_A].value = args;

		if(args[0] == '-')
		{
			macros[M_a].value = args;
		}
		else
		{
			char *const escaped_args = shell_like_escape(args, 0);
			custom_args = format_str("%s", escaped_args); 
			macros[M_a].value = custom_args;
			free(escaped_args);
		}
	}

	menus_init_data(&m, view, format_str("Find %s", args),
			strdup("No files found"));

	m.stashable = 1;
	m.execute_handler = &execute_find_cb;
	m.key_handler = &menus_def_khandler;

	cmd = ma_expand_custom(cfg.find_prg, ARRAY_LEN(macros), macros);

	free(targets);
	free(custom_args);

	ui_sb_msg("find...");
	save_msg = menus_capture(view, cmd, 0, &m, macros[M_u].explicit_use,
			macros[M_U].explicit_use);
	free(cmd);

	return save_msg;
}

/* Callback that is called when menu item is selected.  Should return non-zero
 * to stay in menu mode. */
static int
execute_find_cb(view_t *view, menu_data_t *m)
{
	(void)menus_goto_file(m, view, m->items[m->pos], 0);
	return 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
