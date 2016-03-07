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

#include "normal.h"

#include <curses.h>

#include <regex.h> /* regexec() */

#include <sys/types.h> /* ssize_t */

#include <assert.h> /* assert() */
#include <ctype.h> /* tolower() */
#include <stddef.h> /* size_t wchar_t */
#include <stdio.h>  /* snprintf() */
#include <stdlib.h> /* free() */
#include <string.h> /* strncmp() */
#include <wctype.h> /* towupper() */
#include <wchar.h> /* wcscpy() */

#include "../cfg/config.h"
#include "../cfg/hist.h"
#include "../compat/fs_limits.h"
#include "../compat/reallocarray.h"
#include "../engine/keys.h"
#include "../engine/mode.h"
#include "../modes/dialogs/msg_dialog.h"
#include "../ui/cancellation.h"
#include "../ui/fileview.h"
#include "../ui/quickview.h"
#include "../ui/statusbar.h"
#include "../ui/ui.h"
#include "../utils/macros.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../utils/utf8.h"
#include "../utils/utils.h"
#include "../cmd_core.h"
#include "../filelist.h"
#include "../fileops.h"
#include "../filtering.h"
#include "../marks.h"
#include "../registers.h"
#include "../running.h"
#include "../search.h"
#include "../status.h"
#include "../types.h"
#include "../undo.h"
#include "../vifm.h"
#include "dialogs/attr_dialog.h"
#include "file_info.h"
#include "cmdline.h"
#include "modes.h"
#include "view.h"
#include "visual.h"

static void cmd_ctrl_a(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_b(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_c(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_d(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_e(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_f(key_info_t key_info, keys_info_t *keys_info);
static void page_scroll(int base, int direction);
static void cmd_ctrl_g(key_info_t key_info, keys_info_t *keys_info);
static void cmd_space(key_info_t key_info, keys_info_t *keys_info);
static void cmd_emarkemark(key_info_t key_info, keys_info_t *keys_info);
static void cmd_emark_selector(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_i(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_l(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_m(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_o(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_r(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_u(key_info_t key_info, keys_info_t *keys_info);
static void scroll_view(ssize_t offset);
static void cmd_ctrl_wH(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wJ(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wK(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wL(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wb(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wh(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wj(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wk(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wl(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wo(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_ws(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wt(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wv(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_ww(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wx(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_wz(key_info_t key_info, keys_info_t *keys_info);
static int is_left_or_top(void);
static FileView * pick_view(void);
static void cmd_ctrl_x(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_y(key_info_t key_info, keys_info_t *keys_info);
static void cmd_shift_tab(key_info_t key_info, keys_info_t *keys_info);
static void go_to_other_window(void);
static int try_switch_into_view_mode(void);
static void cmd_quote(key_info_t key_info, keys_info_t *keys_info);
static void cmd_dollar(key_info_t key_info, keys_info_t *keys_info);
static void cmd_percent(key_info_t key_info, keys_info_t *keys_info);
static void cmd_equal(key_info_t key_info, keys_info_t *keys_info);
static void cmd_comma(key_info_t key_info, keys_info_t *keys_info);
static void cmd_dot(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zero(key_info_t key_info, keys_info_t *keys_info);
static void cmd_colon(key_info_t key_info, keys_info_t *keys_info);
static void cmd_semicolon(key_info_t key_info, keys_info_t *keys_info);
static void cmd_slash(key_info_t key_info, keys_info_t *keys_info);
static void cmd_question(key_info_t key_info, keys_info_t *keys_info);
static void cmd_C(key_info_t key_info, keys_info_t *keys_info);
static void cmd_F(key_info_t key_info, keys_info_t *keys_info);
static void find_goto(int ch, int count, int backward, keys_info_t *keys_info);
static void cmd_G(key_info_t key_info, keys_info_t *keys_info);
static void cmd_H(key_info_t key_info, keys_info_t *keys_info);
static void cmd_L(key_info_t key_info, keys_info_t *keys_info);
static void cmd_M(key_info_t key_info, keys_info_t *keys_info);
static void pick_or_move(keys_info_t *keys_info, int new_pos);
static void cmd_N(key_info_t key_info, keys_info_t *keys_info);
static void cmd_P(key_info_t key_info, keys_info_t *keys_info);
static void cmd_V(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ZQ(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ZZ(key_info_t key_info, keys_info_t *keys_info);
static void cmd_al(key_info_t key_info, keys_info_t *keys_info);
static void cmd_av(key_info_t key_info, keys_info_t *keys_info);
static void cmd_cW(key_info_t key_info, keys_info_t *keys_info);
#ifndef _WIN32
static void cmd_cg(key_info_t key_info, keys_info_t *keys_info);
#endif
static void cmd_cl(key_info_t key_info, keys_info_t *keys_info);
#ifndef _WIN32
static void cmd_co(key_info_t key_info, keys_info_t *keys_info);
#endif
static void cmd_cp(key_info_t key_info, keys_info_t *keys_info);
static void cmd_cw(key_info_t key_info, keys_info_t *keys_info);
static void cmd_DD(key_info_t key_info, keys_info_t *keys_info);
static void cmd_dd(key_info_t key_info, keys_info_t *keys_info);
static void delete(key_info_t key_info, int use_trash);
static void cmd_D_selector(key_info_t key_info, keys_info_t *keys_info);
static void cmd_d_selector(key_info_t key_info, keys_info_t *keys_info);
static int delete_confirmed(int use_trash);
static void delete_with_selector(key_info_t key_info, keys_info_t *keys_info,
		int use_trash);
static void call_delete(key_info_t key_info, keys_info_t *keys_info,
		int use_trash);
static void cmd_e(key_info_t key_info, keys_info_t *keys_info);
static void cmd_f(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gA(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ga(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gf(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gg(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gh(key_info_t key_info, keys_info_t *keys_info);
#ifdef _WIN32
static void cmd_gr(key_info_t key_info, keys_info_t *keys_info);
#endif
static void cmd_gs(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gU(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gUgg(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gu(key_info_t key_info, keys_info_t *keys_info);
static void cmd_gugg(key_info_t key_info, keys_info_t *keys_info);
static void do_gu(key_info_t key_info, keys_info_t *keys_info, int upper);
static void cmd_gv(key_info_t key_info, keys_info_t *keys_info);
static void cmd_h(key_info_t key_info, keys_info_t *keys_info);
static void cmd_i(key_info_t key_info, keys_info_t *keys_info);
static void cmd_j(key_info_t key_info, keys_info_t *keys_info);
static void cmd_k(key_info_t key_info, keys_info_t *keys_info);
static void go_to_prev(key_info_t key_info, keys_info_t *keys_info, int step);
static void cmd_n(key_info_t key_info, keys_info_t *keys_info);
static void search(key_info_t key_info, int backward);
static void cmd_l(key_info_t key_info, keys_info_t *keys_info);
static void go_to_next(key_info_t key_info, keys_info_t *keys_info, int step);
static void cmd_p(key_info_t key_info, keys_info_t *keys_info);
static void call_put_files(key_info_t key_info, int move);
static void cmd_m(key_info_t key_info, keys_info_t *keys_info);
static void cmd_rl(key_info_t key_info, keys_info_t *keys_info);
static void call_put_links(key_info_t key_info, int relative);
static void cmd_q_colon(key_info_t key_info, keys_info_t *keys_info);
static void cmd_q_slash(key_info_t key_info, keys_info_t *keys_info);
static void cmd_q_question(key_info_t key_info, keys_info_t *keys_info);
static void activate_search(int count, int back, int external);
static void cmd_q_equals(key_info_t key_info, keys_info_t *keys_info);
static void cmd_t(key_info_t key_info, keys_info_t *keys_info);
static void cmd_u(key_info_t key_info, keys_info_t *keys_info);
static void cmd_yy(key_info_t key_info, keys_info_t *keys_info);
static int calc_pick_files_end_pos(const FileView *view, int count);
static void cmd_y_selector(key_info_t key_info, keys_info_t *keys_info);
static void yank(key_info_t key_info, keys_info_t *keys_info);
static void free_list_of_file_indexes(keys_info_t *keys_info);
static void cmd_zM(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zO(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zR(key_info_t key_info, keys_info_t *keys_info);
static void cmd_za(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zd(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zf(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zm(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zo(key_info_t key_info, keys_info_t *keys_info);
static void cmd_zr(key_info_t key_info, keys_info_t *keys_info);
static void cmd_left_paren(key_info_t key_info, keys_info_t *keys_info);
static void cmd_right_paren(key_info_t key_info, keys_info_t *keys_info);
static void cmd_left_curly_bracket(key_info_t key_info, keys_info_t *keys_info);
static void cmd_right_curly_bracket(key_info_t key_info,
		keys_info_t *keys_info);
static void pick_files(FileView *view, int end, keys_info_t *keys_info);
static void selector_S(key_info_t key_info, keys_info_t *keys_info);
static void selector_a(key_info_t key_info, keys_info_t *keys_info);
static void selector_s(key_info_t key_info, keys_info_t *keys_info);

static int last_fast_search_char;
static int last_fast_search_backward = -1;

/* Number of search repeats (e.g. counter passed to n or N key). */
static int search_repeat;

static keys_add_info_t builtin_cmds[] = {
	{L"\x01", {{&cmd_ctrl_a}}},
	{L"\x02", {{&cmd_ctrl_b}}},
	{L"\x03", {{&cmd_ctrl_c}}},
	{L"\x04", {{&cmd_ctrl_d}}},
	{L"\x05", {{&cmd_ctrl_e}}},
	{L"\x06", {{&cmd_ctrl_f}}},
	{L"\x07", {{&cmd_ctrl_g}}},
	{L"\x09", {{&cmd_ctrl_i}}},
	{L"\x0c", {{&cmd_ctrl_l}}},
	{L"\x0d", {{&cmd_ctrl_m}}},
	{L"\x0e", {{&cmd_j}}},
	{L"\x0f", {{&cmd_ctrl_o}}},
	{L"\x10", {{&cmd_k}}},
	{L"\x12", {{&cmd_ctrl_r}}},
	{L"\x15", {{&cmd_ctrl_u}}},
	{L"\x17\x02", {{&cmd_ctrl_wb}}},
	{L"\x17H", {{&cmd_ctrl_wH}}},
	{L"\x17J", {{&cmd_ctrl_wJ}}},
	{L"\x17K", {{&cmd_ctrl_wK}}},
	{L"\x17L", {{&cmd_ctrl_wL}}},
	{L"\x17"L"b", {{&cmd_ctrl_wb}}},
	{L"\x17\x08", {{&cmd_ctrl_wh}}},
	{L"\x17h", {{&cmd_ctrl_wh}}},
	{L"\x17\x09", {{&cmd_ctrl_wj}}},
	{L"\x17j", {{&cmd_ctrl_wj}}},
	{L"\x17\x0b", {{&cmd_ctrl_wk}}},
	{L"\x17k", {{&cmd_ctrl_wk}}},
	{L"\x17\x0c", {{&cmd_ctrl_wl}}},
	{L"\x17l", {{&cmd_ctrl_wl}}},
	{L"\x17\x0f", {{&cmd_ctrl_wo}}},
	{L"\x17o", {{&cmd_ctrl_wo}}},
	{L"\x17\x10", {{&cmd_ctrl_ww}}},
	{L"\x17p", {{&cmd_ctrl_ww}}},
	{L"\x17\x13", {{&cmd_ctrl_ws}}},
	{L"\x17s", {{&cmd_ctrl_ws}}},
	{L"\x17\x14", {{&cmd_ctrl_wt}}},
	{L"\x17t", {{&cmd_ctrl_wt}}},
	{L"\x17\x16", {{&cmd_ctrl_wv}}},
	{L"\x17v", {{&cmd_ctrl_wv}}},
	{L"\x17\x17", {{&cmd_ctrl_ww}}},
	{L"\x17w", {{&cmd_ctrl_ww}}},
	{L"\x17\x18", {{&cmd_ctrl_wx}}},
	{L"\x17x", {{&cmd_ctrl_wx}}},
	{L"\x17\x1a", {{&cmd_ctrl_wz}}},
	{L"\x17z", {{&cmd_ctrl_wz}}},
	{L"\x17=", {{&normal_cmd_ctrl_wequal}, .nim = 1}},
	{L"\x17<", {{&normal_cmd_ctrl_wless}, .nim = 1}},
	{L"\x17>", {{&normal_cmd_ctrl_wgreater}, .nim = 1}},
	{L"\x17+", {{&normal_cmd_ctrl_wplus}, .nim = 1}},
	{L"\x17-", {{&normal_cmd_ctrl_wminus}, .nim = 1}},
	{L"\x17|", {{&normal_cmd_ctrl_wpipe}, .nim = 1}},
	{L"\x17_", {{&normal_cmd_ctrl_wpipe}, .nim = 1}},
	{L"\x18", {{&cmd_ctrl_x}}},
	{L"\x19", {{&cmd_ctrl_y}}},
#ifdef ENABLE_EXTENDED_KEYS
	{{KEY_BTAB, 0}, {{&cmd_shift_tab}}},
#else
	{L"\033"L"[Z", {{&cmd_shift_tab}}},
#endif
	/* escape */
	{L"\x1b", {{&cmd_ctrl_c}}},
	{L"'", {{&cmd_quote}, FOLLOWED_BY_MULTIKEY}},
	{L" ", {{&cmd_space}}},
	{L"!", {{&cmd_emark_selector}, FOLLOWED_BY_SELECTOR}},
	{L"!!", {{&cmd_emarkemark}}},
	{L"^", {{&cmd_zero}}},
	{L"$", {{&cmd_dollar}}},
	{L"%", {{&cmd_percent}}},
	{L"=", {{&cmd_equal}}},
	{L",", {{&cmd_comma}}},
	{L".", {{&cmd_dot}}},
	{L"0", {{&cmd_zero}}},
	{L":", {{&cmd_colon}}},
	{L";", {{&cmd_semicolon}}},
	{L"/", {{&cmd_slash}}},
	{L"?", {{&cmd_question}}},
	{L"C", {{&cmd_C}}},
	{L"F", {{&cmd_F}, FOLLOWED_BY_MULTIKEY}},
	{L"G", {{&cmd_G}}},
	{L"H", {{&cmd_H}}},
	{L"L", {{&cmd_L}}},
	{L"M", {{&cmd_M}}},
	{L"N", {{&cmd_N}}},
	{L"P", {{&cmd_P}}},
	{L"V", {{&cmd_V}}},
	{L"Y", {{&cmd_yy}}},
	{L"ZQ", {{&cmd_ZQ}}},
	{L"ZZ", {{&cmd_ZZ}}},
	{L"al", {{&cmd_al}}},
	{L"av", {{&cmd_av}}},
	{L"cW", {{&cmd_cW}}},
#ifndef _WIN32
	{L"cg", {{&cmd_cg}}},
#endif
	{L"cl", {{&cmd_cl}}},
#ifndef _WIN32
	{L"co", {{&cmd_co}}},
#endif
	{L"cp", {{&cmd_cp}}},
	{L"cw", {{&cmd_cw}}},
	{L"DD", {{&cmd_DD}, .nim = 1}},
	{L"dd", {{&cmd_dd}, .nim = 1}},
	{L"D", {{&cmd_D_selector}, FOLLOWED_BY_SELECTOR}},
	{L"d", {{&cmd_d_selector}, FOLLOWED_BY_SELECTOR}},
	{L"e", {{&cmd_e}}},
	{L"f", {{&cmd_f}, FOLLOWED_BY_MULTIKEY}},
	{L"gA", {{&cmd_gA}}},
	{L"ga", {{&cmd_ga}}},
	{L"gf", {{&cmd_gf}}},
	{L"gg", {{&cmd_gg}}},
	{L"gh", {{&cmd_gh}}},
	{L"gj", {{&cmd_j}}},
	{L"gk", {{&cmd_k}}},
	{L"gl", {{&cmd_ctrl_m}}},
#ifdef _WIN32
	{L"gr", {{&cmd_gr}}},
#endif
	{L"gs", {{&cmd_gs}}},
	{L"gU", {{&cmd_gU}, FOLLOWED_BY_SELECTOR}},
	{L"gUgU", {{&cmd_gU}}},
	{L"gUgg", {{&cmd_gUgg}}},
	{L"gUU", {{&cmd_gU}}},
	{L"gu", {{&cmd_gu}, FOLLOWED_BY_SELECTOR}},
	{L"gugg", {{&cmd_gugg}}},
	{L"gugu", {{&cmd_gu}}},
	{L"guu", {{&cmd_gu}}},
	{L"gv", {{&cmd_gv}}},
	{L"h", {{&cmd_h}}},
	{L"i", {{&cmd_i}}},
	{L"j", {{&cmd_j}}},
	{L"k", {{&cmd_k}}},
	{L"l", {{&cmd_l}}},
	{L"m", {{&cmd_m}, FOLLOWED_BY_MULTIKEY}},
	{L"n", {{&cmd_n}}},
	{L"p", {{&cmd_p}}},
	{L"rl", {{&cmd_rl}}},
	{L"q:", {{&cmd_q_colon}}},
	{L"q/", {{&cmd_q_slash}}},
	{L"q?", {{&cmd_q_question}}},
	{L"q=", {{&cmd_q_equals}}},
	{L"t", {{&cmd_t}}},
	{L"u", {{&cmd_u}}},
	{L"yy", {{&cmd_yy}, .nim = 1}},
	{L"y", {{&cmd_y_selector}, FOLLOWED_BY_SELECTOR}},
	{L"v", {{&cmd_V}}},
	{L"zM", {{&cmd_zM}}},
	{L"zO", {{&cmd_zO}}},
	{L"zR", {{&cmd_zR}}},
	{L"za", {{&cmd_za}}},
	{L"zd", {{&cmd_zd}}},
	{L"zb", {{&normal_cmd_zb}}},
	{L"zf", {{&cmd_zf}}},
	{L"zm", {{&cmd_zm}}},
	{L"zo", {{&cmd_zo}}},
	{L"zr", {{&cmd_zr}}},
	{L"zt", {{&normal_cmd_zt}}},
	{L"zz", {{&normal_cmd_zz}}},
	{L"(", {{&cmd_left_paren}}},
	{L")", {{&cmd_right_paren}}},
	{L"{", {{&cmd_left_curly_bracket}}},
	{L"}", {{&cmd_right_curly_bracket}}},
#ifdef ENABLE_EXTENDED_KEYS
	{{KEY_PPAGE}, {{&cmd_ctrl_b}}},
	{{KEY_NPAGE}, {{&cmd_ctrl_f}}},
	{{KEY_LEFT}, {{&cmd_h}}},
	{{KEY_DOWN}, {{&cmd_j}}},
	{{KEY_UP}, {{&cmd_k}}},
	{{KEY_RIGHT}, {{&cmd_l}}},
	{{KEY_HOME}, {{&cmd_gg}}},
	{{KEY_END}, {{&cmd_G}}},
#endif /* ENABLE_EXTENDED_KEYS */
};

static keys_add_info_t selectors[] = {
	{L"'", {{.handler = cmd_quote}, FOLLOWED_BY_MULTIKEY}},
	{L"%", {{.handler = cmd_percent}}},
	{L"^", {{.handler = cmd_zero}}},
	{L"$", {{.handler = cmd_dollar}}},
	{L",", {{.handler = cmd_comma}}},
	{L"0", {{.handler = cmd_zero}}},
	{L";", {{.handler = cmd_semicolon}}},
	{L"\x0e", {{.handler = cmd_j}}}, /* Ctrl-N */
	{L"\x10", {{.handler = cmd_k}}}, /* Ctrl-P */
	{L"F", {{.handler = cmd_F}, FOLLOWED_BY_MULTIKEY}},
	{L"G", {{.handler = cmd_G}}},
	{L"H", {{.handler = cmd_H}}},
	{L"L", {{.handler = cmd_L}}},
	{L"M", {{.handler = cmd_M}}},
	{L"S", {{.handler = selector_S}}},
	{L"a", {{.handler = selector_a}}},
	{L"f", {{.handler = cmd_f}, FOLLOWED_BY_MULTIKEY}},
	{L"gg", {{.handler = cmd_gg}}},
	{L"h", {{.handler = cmd_h}}},
	{L"j", {{.handler = cmd_j}}},
	{L"k", {{.handler = cmd_k}}},
	{L"l", {{.handler = cmd_l}}},
	{L"s", {{.handler = selector_s}}},
	{L"(", {{.handler = cmd_left_paren}}},
	{L")", {{.handler = cmd_right_paren}}},
	{L"{", {{.handler = cmd_left_curly_bracket}}},
	{L"}", {{.handler = cmd_right_curly_bracket}}},
#ifdef ENABLE_EXTENDED_KEYS
	{{KEY_DOWN}, {{.handler = cmd_j}}},
	{{KEY_UP}, {{.handler = cmd_k}}},
	{{KEY_HOME}, {{.handler = cmd_gg}}},
	{{KEY_END}, {{.handler = cmd_G}}},
#endif /* ENABLE_EXTENDED_KEYS */
};

void
init_normal_mode(void)
{
	int ret_code;

	ret_code = vle_keys_add(builtin_cmds, ARRAY_LEN(builtin_cmds), NORMAL_MODE);
	assert(ret_code == 0);

	ret_code = vle_keys_add_selectors(selectors, ARRAY_LEN(selectors),
			NORMAL_MODE);
	assert(ret_code == 0);

	(void)ret_code;
}

/* Increments first number in names of marked files of the view [count=1]
 * times. */
static void
cmd_ctrl_a(key_info_t key_info, keys_info_t *keys_info)
{
	check_marking(curr_view, 0, NULL);
	curr_stats.save_msg = incdec_names(curr_view, def_count(key_info.count));
}

static void
cmd_ctrl_b(key_info_t key_info, keys_info_t *keys_info)
{
	if(can_scroll_up(curr_view))
	{
		page_scroll(get_last_visible_cell(curr_view), -1);
	}
}

/* Resets selection and search highlight. */
static void
cmd_ctrl_c(key_info_t key_info, keys_info_t *keys_info)
{
	ui_view_reset_search_highlight(curr_view);
	clean_selected_files(curr_view);
	redraw_current_view();
}

/* Scroll view down by half of its height. */
static void
cmd_ctrl_d(key_info_t key_info, keys_info_t *keys_info)
{
	if(!at_last_line(curr_view))
	{
		scroll_view(curr_view->window_cells/2);
	}
}

/* Scroll pane one line down. */
static void
cmd_ctrl_e(key_info_t key_info, keys_info_t *keys_info)
{
	if(correct_list_pos_on_scroll_down(curr_view, 1))
	{
		scroll_down(curr_view, 1);
		redraw_current_view();
	}
}

static void
cmd_ctrl_f(key_info_t key_info, keys_info_t *keys_info)
{
	if(can_scroll_down(curr_view))
	{
		page_scroll(curr_view->top_line, 1);
	}
}

/* Scrolls pane by one view in both directions. The direction should be 1 or
 * -1. */
static void
page_scroll(int base, int direction)
{
	/* Two lines gap. */
	int offset = (curr_view->window_rows - 1)*curr_view->column_count;
	curr_view->list_pos = base + direction*offset;
	scroll_by_files(curr_view, direction*offset);
	redraw_current_view();
}

static void
cmd_ctrl_g(key_info_t key_info, keys_info_t *keys_info)
{
	enter_file_info_mode(curr_view);
}

static void
cmd_space(key_info_t key_info, keys_info_t *keys_info)
{
	go_to_other_pane();
}

/* Processes !! normal mode command, which can be prepended by a count, which is
 * treated as number of lines to be processed. */
static void
cmd_emarkemark(key_info_t key_info, keys_info_t *keys_info)
{
	char prefix[16];
	if(key_info.count != NO_COUNT_GIVEN && key_info.count != 1)
	{
		if(curr_view->list_pos + key_info.count >= curr_view->list_rows)
		{
			strcpy(prefix, ".,$!");
		}
		else
		{
			snprintf(prefix, ARRAY_LEN(prefix), ".,.+%d!", key_info.count - 1);
		}
	}
	else
	{
		strcpy(prefix, ".!");
	}
	enter_cmdline_mode(CLS_COMMAND, prefix, NULL);
}

/* Processes !<selector> normal mode command.  Processes results of applying
 * selector and invokes cmd_emarkemark(...) to do the rest. */
static void
cmd_emark_selector(key_info_t key_info, keys_info_t *keys_info)
{
	int i, m;

	if(keys_info->count == 0)
	{
		cmd_emarkemark(key_info, keys_info);
		return;
	}

	m = keys_info->indexes[0];
	for(i = 1; i < keys_info->count; i++)
	{
		if(keys_info->indexes[i] > m)
		{
			m = keys_info->indexes[i];
		}
	}

	free_list_of_file_indexes(keys_info);

	key_info.count = m - curr_view->list_pos + 1;
	cmd_emarkemark(key_info, keys_info);
}

static void
cmd_ctrl_i(key_info_t key_info, keys_info_t *keys_info)
{
	if(cfg.tab_switches_pane)
	{
		cmd_space(key_info, keys_info);
	}
	else
	{
		navigate_forward_in_history(curr_view);
	}
}

/* Clear screen and redraw. */
static void
cmd_ctrl_l(key_info_t key_info, keys_info_t *keys_info)
{
	update_screen(UT_FULL);
}

/* Enters directory or runs file. */
static void
cmd_ctrl_m(key_info_t key_info, keys_info_t *keys_info)
{
	open_file(curr_view, FHE_RUN);
	clean_selected_files(curr_view);
	redraw_current_view();
}

static void
cmd_ctrl_o(key_info_t key_info, keys_info_t *keys_info)
{
	navigate_backward_in_history(curr_view);
}

static void
cmd_ctrl_r(key_info_t key_info, keys_info_t *keys_info)
{
	int ret;

	curr_stats.confirmed = 0;
	ui_cancellation_reset();

	status_bar_message("Redoing...");

	ret = redo_group();

	if(ret == 0)
	{
		ui_views_reload_visible_filelists();
		status_bar_message("Redone one group");
	}
	else if(ret == -2)
	{
		ui_views_reload_visible_filelists();
		status_bar_error("Redone one group with errors");
	}
	else if(ret == -1)
	{
		status_bar_message("Nothing to redo");
	}
	else if(ret == -3)
	{
		status_bar_error("Can't redo group, it was skipped");
	}
	else if(ret == -4)
	{
		status_bar_error("Can't redo what wasn't undone");
	}
	else if(ret == -6)
	{
		status_bar_message("Group redo skipped by user");
	}
	else if(ret == -7)
	{
		ui_views_reload_visible_filelists();
		status_bar_message("Redoing was cancelled");
	}
	else if(ret == 1)
	{
		status_bar_error("Redo operation was skipped due to previous errors");
	}
	curr_stats.save_msg = 1;
}

/* Scroll view up by half of its height. */
static void
cmd_ctrl_u(key_info_t key_info, keys_info_t *keys_info)
{
	if(!at_first_line(curr_view))
	{
		scroll_view(-(ssize_t)curr_view->window_cells/2);
	}
}

/* Scrolls view by specified number of files. */
static void
scroll_view(ssize_t offset)
{
	offset = ROUND_DOWN(offset, curr_view->column_count);
	curr_view->list_pos += offset;
	correct_list_pos(curr_view, offset);
	go_to_start_of_line(curr_view);
	scroll_by_files(curr_view, offset);
	redraw_current_view();
}

/* Go to bottom-right window. */
static void
cmd_ctrl_wb(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_view != &rwin)
		go_to_other_window();
}

static void
cmd_ctrl_wh(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_stats.split == VSPLIT && curr_view == &rwin)
		go_to_other_window();
}

static void
cmd_ctrl_wj(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_stats.split == HSPLIT && curr_view == &lwin)
		go_to_other_window();
}

static void
cmd_ctrl_wk(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_stats.split == HSPLIT && curr_view == &rwin)
		go_to_other_window();
}

static void
cmd_ctrl_wl(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_stats.split == VSPLIT && curr_view == &lwin)
		go_to_other_window();
}

/* Leave only one (current) pane. */
static void
cmd_ctrl_wo(key_info_t key_info, keys_info_t *keys_info)
{
	only();
}

/* To split pane horizontally. */
static void
cmd_ctrl_ws(key_info_t key_info, keys_info_t *keys_info)
{
	split_view(HSPLIT);
}

/* Go to top-left window. */
static void
cmd_ctrl_wt(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_view != &lwin)
		go_to_other_window();
}

/* To split pane vertically. */
static void
cmd_ctrl_wv(key_info_t key_info, keys_info_t *keys_info)
{
	split_view(VSPLIT);
}

static void
cmd_ctrl_ww(key_info_t key_info, keys_info_t *keys_info)
{
	go_to_other_window();
}

static void
cmd_ctrl_wH(key_info_t key_info, keys_info_t *keys_info)
{
	move_window(curr_view, 0, 1);
}

static void
cmd_ctrl_wJ(key_info_t key_info, keys_info_t *keys_info)
{
	move_window(curr_view, 1, 0);
}

static void
cmd_ctrl_wK(key_info_t key_info, keys_info_t *keys_info)
{
	move_window(curr_view, 1, 1);
}

static void
cmd_ctrl_wL(key_info_t key_info, keys_info_t *keys_info)
{
	move_window(curr_view, 0, 0);
}

void
normal_cmd_ctrl_wequal(key_info_t key_info, keys_info_t *keys_info)
{
	curr_stats.splitter_pos = -1;
	update_screen(UT_REDRAW);
}

void
normal_cmd_ctrl_wless(key_info_t key_info, keys_info_t *keys_info)
{
	move_splitter(def_count(key_info.count), is_left_or_top() ? -1 : +1);
}

void
normal_cmd_ctrl_wgreater(key_info_t key_info, keys_info_t *keys_info)
{
	move_splitter(def_count(key_info.count), is_left_or_top() ? +1 : -1);
}

void
normal_cmd_ctrl_wplus(key_info_t key_info, keys_info_t *keys_info)
{
	move_splitter(def_count(key_info.count), is_left_or_top() ? +1 : -1);
}

void
normal_cmd_ctrl_wminus(key_info_t key_info, keys_info_t *keys_info)
{
	move_splitter(def_count(key_info.count), is_left_or_top() ? -1 : +1);
}

void
normal_cmd_ctrl_wpipe(key_info_t key_info, keys_info_t *keys_info)
{
	if(key_info.count == NO_COUNT_GIVEN)
	{
		key_info.count = (curr_stats.split == HSPLIT)
		               ? getmaxy(stdscr)
		               : getmaxx(stdscr);
	}

	ui_view_resize(pick_view(), key_info.count);
}

/* Checks whether current view is left/top or right/bottom.  Returns non-zero
 * in first case and zero in the second one. */
static int
is_left_or_top(void)
{
	return (pick_view() == &lwin);
}

/* Picks view to operate on for Ctrl-W set of shortcuts.  Returns the view. */
static FileView *
pick_view(void)
{
	if(vle_mode_is(VIEW_MODE))
	{
		return curr_view->explore_mode ? curr_view : other_view;
	}

	return curr_view;
}

/* Switches views. */
static void
cmd_ctrl_wx(key_info_t key_info, keys_info_t *keys_info)
{
	switch_panes();
	if(curr_stats.view)
	{
		change_window();
		(void)try_switch_into_view_mode();
	}
}

/* Quits preview pane or view modes. */
static void
cmd_ctrl_wz(key_info_t key_info, keys_info_t *keys_info)
{
	preview_close();
}

/* Decrements first number in names of marked files of the view [count=1]
 * times. */
static void
cmd_ctrl_x(key_info_t key_info, keys_info_t *keys_info)
{
	check_marking(curr_view, 0, NULL);
	curr_stats.save_msg = incdec_names(curr_view, -def_count(key_info.count));
}

static void
cmd_ctrl_y(key_info_t key_info, keys_info_t *keys_info)
{
	if(correct_list_pos_on_scroll_up(curr_view, 1))
	{
		scroll_up(curr_view, 1);
		redraw_current_view();
	}
}

static void
cmd_shift_tab(key_info_t key_info, keys_info_t *keys_info)
{
	if(!try_switch_into_view_mode())
	{
		if(other_view->explore_mode)
		{
			go_to_other_window();
		}
	}
}

/* Activates view mode on the preview, or just switches active pane. */
static void
go_to_other_window(void)
{
	if(!try_switch_into_view_mode())
	{
		go_to_other_pane();
	}
}

/* Tries to go into view mode in case the other pane displays preview.  Returns
 * non-zero on success, otherwise zero is returned. */
static int
try_switch_into_view_mode(void)
{
	if(curr_stats.view)
	{
		enter_view_mode(other_view, 0);
		return 1;
	}
	return 0;
}

/* Clone selection.  Count specifies number of copies of each file or directory
 * to create (one by default). */
static void
cmd_C(key_info_t key_info, keys_info_t *keys_info)
{
	check_marking(curr_view, 0, NULL);
	curr_stats.save_msg = clone_files(curr_view, NULL, 0, 0,
			def_count(key_info.count));
}

static void
cmd_F(key_info_t key_info, keys_info_t *keys_info)
{
	last_fast_search_char = key_info.multi;
	last_fast_search_backward = 1;

	if(key_info.count == NO_COUNT_GIVEN)
		key_info.count = 1;
	find_goto(key_info.multi, key_info.count, 1, keys_info);
}

/* Returns negative number if nothing was found */
int
ffind(int ch, int backward, int wrap)
{
	int x;
	int upcase = cfg.ignore_case && !(cfg.smart_case && iswupper(ch));

	if(upcase)
		ch = towupper(ch);

	x = curr_view->list_pos;
	do
	{
		if(backward)
		{
			x--;
			if(x < 0)
			{
				if(wrap)
					x = curr_view->list_rows - 1;
				else
					return -1;
			}
		}
		else
		{
			x++;
			if(x > curr_view->list_rows - 1)
			{
				if(wrap)
					x = 0;
				else
					return -1;
			}
		}

		if(ch > 255)
		{
			wchar_t wc = get_first_wchar(curr_view->dir_entry[x].name);
			if(upcase)
				wc = towupper(wc);
			if(wc == ch)
				break;
		}
		else
		{
			int c = curr_view->dir_entry[x].name[0];
			if(upcase)
				c = towupper(c);
			if(c == ch)
				break;
		}
	}
	while(x != curr_view->list_pos);

	return x;
}

static void
find_goto(int ch, int count, int backward, keys_info_t *keys_info)
{
	int pos;
	int old_pos = curr_view->list_pos;

	pos = ffind(ch, backward, !keys_info->selector);
	if(pos < 0 || pos == curr_view->list_pos)
		return;
	while(--count > 0)
	{
		curr_view->list_pos = pos;
		pos = ffind(ch, backward, !keys_info->selector);
	}

	if(keys_info->selector)
	{
		curr_view->list_pos = old_pos;
		if(pos >= 0)
			pick_files(curr_view, pos, keys_info);
	}
	else
	{
		flist_set_pos(curr_view, pos);
	}
}

/* Jump to bottom of the list or to specified line. */
static void
cmd_G(key_info_t key_info, keys_info_t *keys_info)
{
	int new_pos;
	if(key_info.count == NO_COUNT_GIVEN)
		key_info.count = curr_view->list_rows;

	new_pos = ROUND_DOWN(key_info.count - 1, curr_view->column_count);
	pick_or_move(keys_info, new_pos);
}

/* Calculate size of selected directories ignoring cached sizes. */
static void
cmd_gA(key_info_t key_info, keys_info_t *keys_info)
{
	calculate_size_bg(curr_view, 1);
}

/* Calculate size of selected directories taking cached sizes into account. */
static void
cmd_ga(key_info_t key_info, keys_info_t *keys_info)
{
	calculate_size_bg(curr_view, 0);
}

static void
cmd_gf(key_info_t key_info, keys_info_t *keys_info)
{
	clean_selected_files(curr_view);
	follow_file(curr_view);
	redraw_current_view();
}

/* Jump to the top of the list or to specified line. */
static void
cmd_gg(key_info_t key_info, keys_info_t *keys_info)
{
	if(key_info.count == NO_COUNT_GIVEN)
		key_info.count = 1;

	pick_or_move(keys_info, key_info.count - 1);
}

/* Goes to parent directory regardless of ls-like state. */
static void
cmd_gh(key_info_t key_info, keys_info_t *keys_info)
{
	cd_updir(curr_view, def_count(key_info.count));
}

#ifdef _WIN32
static void
cmd_gr(key_info_t key_info, keys_info_t *keys_info)
{
	open_file(curr_view, FHE_ELEVATE_AND_RUN);
	clean_selected_files(curr_view);
	redraw_current_view();
}
#endif

/* Restores previous selection of files or selects files listed in a
 * register. */
static void
cmd_gs(key_info_t key_info, keys_info_t *keys_info)
{
	reg_t *reg;

	if(key_info.reg == NO_REG_GIVEN)
	{
		flist_sel_restore(curr_view, NULL);
		return;
	}

	reg = regs_find(tolower(key_info.reg));
	if(reg == NULL || reg->nfiles < 1)
	{
		status_bar_error(reg == NULL ? "No such register" : "Register is empty");
		curr_stats.save_msg = 1;
		return;
	}

	flist_sel_restore(curr_view, reg);
}

/* Handles gU<selector>, gUgU and gUU normal mode commands, which convert file
 * name symbols to uppercase. */
static void
cmd_gU(key_info_t key_info, keys_info_t *keys_info)
{
	do_gu(key_info, keys_info, 1);
}

/* Special handler for combination of gU<selector> and gg motion. */
static void
cmd_gUgg(key_info_t key_info, keys_info_t *keys_info)
{
	pick_files(curr_view, 0, keys_info);
	do_gu(key_info, keys_info, 1);
}

/* Handles gu<selector>, gugu and guu normal mode commands, which convert file
 * name symbols to lowercase. */
static void
cmd_gu(key_info_t key_info, keys_info_t *keys_info)
{
	do_gu(key_info, keys_info, 0);
}

/* Special handler for combination of gu<selector> and gg motion. */
static void
cmd_gugg(key_info_t key_info, keys_info_t *keys_info)
{
	pick_files(curr_view, 0, keys_info);
	do_gu(key_info, keys_info, 0);
}

/* Handles gUU, gU<selector>, gUgU, gu<selector>, guu and gugu commands. */
static void
do_gu(key_info_t key_info, keys_info_t *keys_info, int upper)
{
	/* If nothing was selected, do selection count elements down. */
	if(keys_info->count == 0)
	{
		const int count = (key_info.count == NO_COUNT_GIVEN)
			? 0
			: (key_info.count - 1);
		pick_files(curr_view, curr_view->list_pos + count, keys_info);
	}

	check_marking(curr_view, keys_info->count, keys_info->indexes);
	curr_stats.save_msg = change_case(curr_view, upper);
	free_list_of_file_indexes(keys_info);
}

static void
cmd_gv(key_info_t key_info, keys_info_t *keys_info)
{
	enter_visual_mode(VS_RESTORE);
}

/* Go to the first file in window. */
static void
cmd_H(key_info_t key_info, keys_info_t *keys_info)
{
	size_t new_pos = get_window_top_pos(curr_view);
	pick_or_move(keys_info, new_pos);
}

/* Go to the last file in window. */
static void
cmd_L(key_info_t key_info, keys_info_t *keys_info)
{
	size_t new_pos = get_window_bottom_pos(curr_view);
	pick_or_move(keys_info, new_pos);
}

/* Go to middle line of window. */
static void
cmd_M(key_info_t key_info, keys_info_t *keys_info)
{
	size_t new_pos = get_window_middle_pos(curr_view);
	pick_or_move(keys_info, new_pos);
}

/* Picks files or moves cursor depending whether key was pressed as a selector
 * or not. */
static void
pick_or_move(keys_info_t *keys_info, int new_pos)
{
	if(keys_info->selector)
	{
		pick_files(curr_view, new_pos, keys_info);
	}
	else
	{
		flist_set_pos(curr_view, new_pos);
	}
}

static void
cmd_N(key_info_t key_info, keys_info_t *keys_info)
{
	search(key_info, !curr_stats.last_search_backward);
}

/* Move files. */
static void
cmd_P(key_info_t key_info, keys_info_t *keys_info)
{
	call_put_files(key_info, 1);
}

/* Visual selection of files. */
static void
cmd_V(key_info_t key_info, keys_info_t *keys_info)
{
	enter_visual_mode(VS_NORMAL);
}

static void
cmd_ZQ(key_info_t key_info, keys_info_t *keys_info)
{
	vifm_try_leave(0, 0, 0);
}

static void
cmd_ZZ(key_info_t key_info, keys_info_t *keys_info)
{
	vifm_try_leave(1, 0, 0);
}

/* Goto mark. */
static void
cmd_quote(key_info_t key_info, keys_info_t *keys_info)
{
	if(keys_info->selector)
	{
		const int pos = check_mark_directory(curr_view, key_info.multi);
		if(pos >= 0)
		{
			pick_files(curr_view, pos, keys_info);
		}
	}
	else
	{
		curr_stats.save_msg = goto_mark(curr_view, key_info.multi);
		if(!cfg.auto_ch_pos)
		{
			flist_set_pos(curr_view, 0);
		}
	}
}

/* Move cursor to the last column in ls-view sub-mode. */
static void
cmd_dollar(key_info_t key_info, keys_info_t *keys_info)
{
	if(!at_last_column(curr_view) || keys_info->selector)
	{
		pick_or_move(keys_info, get_end_of_line(curr_view));
	}
}

/* Jump to percent of file. */
static void
cmd_percent(key_info_t key_info, keys_info_t *keys_info)
{
	int line;
	if(key_info.count == NO_COUNT_GIVEN)
		return;
	if(key_info.count > 100)
		return;
	line = (key_info.count * curr_view->list_rows)/100;
	pick_or_move(keys_info, line - 1);
}

/* Go to local filter mode. */
static void
cmd_equal(key_info_t key_info, keys_info_t *keys_info)
{
	enter_cmdline_mode(CLS_FILTER, curr_view->local_filter.filter.raw, NULL);
}

static void
cmd_comma(key_info_t key_info, keys_info_t *keys_info)
{
	if(last_fast_search_backward == -1)
		return;

	if(key_info.count == NO_COUNT_GIVEN)
		key_info.count = 1;
	find_goto(last_fast_search_char, key_info.count, !last_fast_search_backward,
			keys_info);
}

/* Repeat last change. */
static void
cmd_dot(key_info_t key_info, keys_info_t *keys_info)
{
	curr_stats.save_msg = exec_commands(curr_stats.last_cmdline_command,
			curr_view, CIT_COMMAND);
}

/* Move cursor to the first column in ls-view sub-mode. */
static void
cmd_zero(key_info_t key_info, keys_info_t *keys_info)
{
	if(!at_first_column(curr_view) || keys_info->selector)
	{
		pick_or_move(keys_info, get_start_of_line(curr_view));
	}
}

/* Switch to command-line mode. */
static void
cmd_colon(key_info_t key_info, keys_info_t *keys_info)
{
	char prefix[16];
	prefix[0] = '\0';
	if(key_info.count != NO_COUNT_GIVEN)
	{
		snprintf(prefix, ARRAY_LEN(prefix), ".,.+%d", key_info.count - 1);
	}
	enter_cmdline_mode(CLS_COMMAND, prefix, NULL);
}

static void
cmd_semicolon(key_info_t key_info, keys_info_t *keys_info)
{
	if(last_fast_search_backward == -1)
		return;

	if(key_info.count == NO_COUNT_GIVEN)
		key_info.count = 1;
	find_goto(last_fast_search_char, key_info.count, last_fast_search_backward,
			keys_info);
}

/* Search forward. */
static void
cmd_slash(key_info_t key_info, keys_info_t *keys_info)
{
	activate_search(key_info.count, 0, 0);
}

/* Search backward. */
static void
cmd_question(key_info_t key_info, keys_info_t *keys_info)
{
	activate_search(key_info.count, 1, 0);
}

/* Creates link with absolute path. */
static void
cmd_al(key_info_t key_info, keys_info_t *keys_info)
{
	call_put_links(key_info, 0);
}

/* Enters selection amending submode of visual mode. */
static void
cmd_av(key_info_t key_info, keys_info_t *keys_info)
{
	enter_visual_mode(VS_AMEND);
}

/* Change word (rename file without extension). */
static void
cmd_cW(key_info_t key_info, keys_info_t *keys_info)
{
	rename_current_file(curr_view, 1);
}

#ifndef _WIN32
/* Change group. */
static void
cmd_cg(key_info_t key_info, keys_info_t *keys_info)
{
	change_group();
}
#endif

/* Change symbolic link. */
static void
cmd_cl(key_info_t key_info, keys_info_t *keys_info)
{
	curr_stats.save_msg = change_link(curr_view);
}

#ifndef _WIN32
/* Change owner. */
static void
cmd_co(key_info_t key_info, keys_info_t *keys_info)
{
	change_owner();
}
#endif

/* Change file attributes (permissions or properties). */
static void
cmd_cp(key_info_t key_info, keys_info_t *keys_info)
{
	enter_attr_mode(curr_view);
}

/* Change word (rename file or files). */
static void
cmd_cw(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_view->selected_files > 1)
	{
		check_marking(curr_view, 0, NULL);
		rename_files(curr_view, NULL, 0, 0);
		return;
	}

	rename_current_file(curr_view, 0);
}

/* Delete file. */
static void
cmd_DD(key_info_t key_info, keys_info_t *keys_info)
{
	delete(key_info, 0);
}

/* Delete file. */
static void
cmd_dd(key_info_t key_info, keys_info_t *keys_info)
{
	delete(key_info, 1);
}

/* Performs file deletion either by moving them to trash or removing
 * permanently. */
static void
delete(key_info_t key_info, int use_trash)
{
	keys_info_t keys_info = {};

	if(!can_change_view_files(curr_view))
	{
		return;
	}

	if(!confirm_deletion(use_trash))
	{
		return;
	}

	if(key_info.count != NO_COUNT_GIVEN)
	{
		const int end_pos = calc_pick_files_end_pos(curr_view, key_info.count);
		pick_files(curr_view, end_pos, &keys_info);
	}

	if(!cfg.selection_is_primary && key_info.count == NO_COUNT_GIVEN)
	{
		pick_files(curr_view, curr_view->list_pos, &keys_info);
	}

	call_delete(key_info, &keys_info, use_trash);
}

/* Permanently removes files defined by selector. */
static void
cmd_D_selector(key_info_t key_info, keys_info_t *keys_info)
{
	if(!can_change_view_files(curr_view))
	{
		return;
	}

	if(delete_confirmed(0))
	{
		delete_with_selector(key_info, keys_info, 0);
	}
}

static void
cmd_d_selector(key_info_t key_info, keys_info_t *keys_info)
{
	if(delete_confirmed(1))
	{
		delete_with_selector(key_info, keys_info, 1);
	}
}

/* Confirms with the user whether deletion of files should take place.  Returns
 * non-zero if so, otherwise zero is returned. */
static int
delete_confirmed(int use_trash)
{
	curr_stats.confirmed = 0;
	if(cfg.confirm)
	{
		const char *const title = use_trash ? "Deletion" : "Permanent deletion";
		if(!prompt_msg(title, "Are you sure you want to delete file(s)?"))
		{
			return 0;
		}
		curr_stats.confirmed = 1;
	}

	return 1;
}

/* Removes (permanently or just moving to trash) files using selector.
 * Processes d<selector> and D<selector> normal mode commands.  On moving to
 * trash files are put in specified register (unnamed by default). */
static void
delete_with_selector(key_info_t key_info, keys_info_t *keys_info, int use_trash)
{
	if(keys_info->count != 0)
	{
		call_delete(key_info, keys_info, use_trash);
	}
}

/* Invokes actual file deletion procedure. */
static void
call_delete(key_info_t key_info, keys_info_t *keys_info, int use_trash)
{
	check_marking(curr_view, keys_info->count, keys_info->indexes);
	curr_stats.save_msg = delete_files(curr_view, def_reg(key_info.reg),
			use_trash);
	free_list_of_file_indexes(keys_info);
}

static void
cmd_e(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_stats.view)
	{
		status_bar_error("Another type of file viewing is activated");
		curr_stats.save_msg = 1;
		return;
	}
	enter_view_mode(curr_view, 1);
}

static void
cmd_f(key_info_t key_info, keys_info_t *keys_info)
{
	last_fast_search_char = key_info.multi;
	last_fast_search_backward = 0;

	if(key_info.count == NO_COUNT_GIVEN)
		key_info.count = 1;
	find_goto(key_info.multi, key_info.count, 0, keys_info);
}

/* Updir or one file left in less-like sub-mode. */
static void
cmd_h(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_view->ls_view)
	{
		go_to_prev(key_info, keys_info, 1);
	}
	else
	{
		cmd_gh(key_info, keys_info);
	}
}

static void
cmd_i(key_info_t key_info, keys_info_t *keys_info)
{
	open_file(curr_view, FHE_NO_RUN);
	clean_selected_files(curr_view);
	redraw_current_view();
}

/* Move down one line. */
static void
cmd_j(key_info_t key_info, keys_info_t *keys_info)
{
	if(!at_last_line(curr_view))
	{
		go_to_next(key_info, keys_info, curr_view->column_count);
	}
}

/* Move up one line. */
static void
cmd_k(key_info_t key_info, keys_info_t *keys_info)
{
	if(!at_first_line(curr_view))
	{
		go_to_prev(key_info, keys_info, curr_view->column_count);
	}
}

/* Moves cursor to one of previous files in the list. */
static void
go_to_prev(key_info_t key_info, keys_info_t *keys_info, int step)
{
	const int distance = def_count(key_info.count)*step;
	pick_or_move(keys_info, curr_view->list_pos - distance);
}

static void
cmd_l(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_view->ls_view)
	{
		go_to_next(key_info, keys_info, 1);
	}
	else
	{
		cmd_ctrl_m(key_info, keys_info);
	}
}

/* Moves cursor to one of next files in the list. */
static void
go_to_next(key_info_t key_info, keys_info_t *keys_info, int step)
{
	const int distance = def_count(key_info.count)*step;
	pick_or_move(keys_info, curr_view->list_pos + distance);
}

/* Set mark. */
static void
cmd_m(key_info_t key_info, keys_info_t *keys_info)
{
	const dir_entry_t *const entry = &curr_view->dir_entry[curr_view->list_pos];
	curr_stats.save_msg = set_user_mark(key_info.multi, entry->origin,
			entry->name);
}

static void
cmd_n(key_info_t key_info, keys_info_t *keys_info)
{
	search(key_info, curr_stats.last_search_backward);
}

static void
search(key_info_t key_info, int backward)
{
	/* TODO: extract common part of this function and visual.c:search(). */

	int found;

	if(hist_is_empty(&cfg.search_hist))
	{
		return;
	}

	if(key_info.count == NO_COUNT_GIVEN)
		key_info.count = 1;

	found = 0;
	if(curr_view->matches == 0)
	{
		const char *const pattern = cfg_get_last_search_pattern();
		curr_stats.save_msg = find_pattern(curr_view, pattern, backward, 1, &found,
				0);
		key_info.count--;
	}

	while(key_info.count-- > 0)
	{
		found += goto_search_match(curr_view, backward) != 0;
	}

	if(found)
	{
		print_search_next_msg(curr_view, backward);
	}
	else
	{
		print_search_fail_msg(curr_view, backward);
	}

	curr_stats.save_msg = 1;
}

/* Put files. */
static void
cmd_p(key_info_t key_info, keys_info_t *keys_info)
{
	call_put_files(key_info, 0);
}

/* Invokes file putting procedure. */
static void
call_put_files(key_info_t key_info, int move)
{
	curr_stats.save_msg = put_files(curr_view, def_reg(key_info.reg), move);
	ui_views_reload_filelists();
}

/* Creates link with absolute path. */
static void
cmd_rl(key_info_t key_info, keys_info_t *keys_info)
{
	call_put_links(key_info, 1);
}

/* Invokes links putting procedure. */
static void
call_put_links(key_info_t key_info, int relative)
{
	curr_stats.save_msg = put_links(curr_view, def_reg(key_info.reg), relative);
	ui_views_reload_filelists();
}

/* Runs external editor to get command-line command and then executes it. */
static void
cmd_q_colon(key_info_t key_info, keys_info_t *keys_info)
{
	get_and_execute_command("", 0U, CIT_COMMAND);
}

/* Runs external editor to get search pattern and then executes it. */
static void
cmd_q_slash(key_info_t key_info, keys_info_t *keys_info)
{
	activate_search(key_info.count, 0, 1);
}

/* Runs external editor to get search pattern and then executes it. */
static void
cmd_q_question(key_info_t key_info, keys_info_t *keys_info)
{
	activate_search(key_info.count, 1, 1);
}

/* Activates search of different kinds. */
static void
activate_search(int count, int back, int external)
{
	/* TODO: generalize with visual.c:activate_search(). */

	search_repeat = (count == NO_COUNT_GIVEN) ? 1 : count;
	curr_stats.last_search_backward = back;
	if(external)
	{
		CmdInputType type = back ? CIT_BSEARCH_PATTERN : CIT_FSEARCH_PATTERN;
		get_and_execute_command("", 0U, type);
	}
	else
	{
		const CmdLineSubmode submode = back ? CLS_BSEARCH : CLS_FSEARCH;
		enter_cmdline_mode(submode, "", NULL);
	}
}

/* Runs external editor to get local filter pattern and then executes it. */
static void
cmd_q_equals(key_info_t key_info, keys_info_t *keys_info)
{
	get_and_execute_command("", 0U, CIT_FILTER_PATTERN);
}

/* Tag file. */
static void
cmd_t(key_info_t key_info, keys_info_t *keys_info)
{
	if(curr_view->dir_entry[curr_view->list_pos].selected == 0)
	{
		/* The ../ dir cannot be selected */
		if(is_parent_dir(curr_view->dir_entry[curr_view->list_pos].name))
			return;

		curr_view->dir_entry[curr_view->list_pos].selected = 1;
		curr_view->selected_files++;
	}
	else
	{
		curr_view->dir_entry[curr_view->list_pos].selected = 0;
		curr_view->selected_files--;
	}

	fview_cursor_redraw(curr_view);
}

/* Undo last command group. */
static void
cmd_u(key_info_t key_info, keys_info_t *keys_info)
{
	int ret;

	curr_stats.confirmed = 0;
	ui_cancellation_reset();

	status_bar_message("Undoing...");

	ret = undo_group();

	if(ret == 0)
	{
		ui_views_reload_visible_filelists();
		status_bar_message("Undone one group");
	}
	else if(ret == -2)
	{
		ui_views_reload_visible_filelists();
		status_bar_error("Undone one group with errors");
	}
	else if(ret == -1)
	{
		status_bar_message("Nothing to undo");
	}
	else if(ret == -3)
	{
		status_bar_error("Can't undo group, it was skipped");
	}
	else if(ret == -4)
	{
		status_bar_error("Can't undo what wasn't redone");
	}
	else if(ret == -5)
	{
		status_bar_error("Operation cannot be undone");
	}
	else if(ret == -6)
	{
		status_bar_message("Group undo skipped by user");
	}
	else if(ret == -7)
	{
		ui_views_reload_visible_filelists();
		status_bar_message("Undoing was cancelled");
	}
	else if(ret == 1)
	{
		status_bar_error("Undo operation was skipped due to previous errors");
	}
	curr_stats.save_msg = 1;
}

/* Yanks files. */
static void
cmd_yy(key_info_t key_info, keys_info_t *keys_info)
{
	if(key_info.count != NO_COUNT_GIVEN)
	{
		const int end_pos = calc_pick_files_end_pos(curr_view, key_info.count);
		pick_files(curr_view, end_pos, keys_info);
	}
	else if(!cfg.selection_is_primary)
	{
		pick_files(curr_view, curr_view->list_pos, keys_info);
	}

	check_marking(curr_view, keys_info->count, keys_info->indexes);
	yank(key_info, keys_info);
}

/* Calculates end position for pick_files(...) function using cursor position
 * and count of a command.  Considers possible integer overflow. */
static int
calc_pick_files_end_pos(const FileView *view, int count)
{
	/* Way of comparing values makes difference!  This way it will work even when
	 * count equals to INT_MAX.  Don't change it! */
	if(count > view->list_rows - view->list_pos)
	{
		return view->list_rows - 1;
	}
	else
	{
		return view->list_pos + count - 1;
	}
}

/* Processes y<selector> normal mode command, which copies files to one of
 * registers (unnamed by default). */
static void
cmd_y_selector(key_info_t key_info, keys_info_t *keys_info)
{
	if(keys_info->count != 0)
	{
		mark_files_at(curr_view, keys_info->count, keys_info->indexes);
		yank(key_info, keys_info);
	}
}

static void
yank(key_info_t key_info, keys_info_t *keys_info)
{
	curr_stats.save_msg = yank_files(curr_view, def_reg(key_info.reg));
	free_list_of_file_indexes(keys_info);

	clean_selected_files(curr_view);
	ui_view_schedule_redraw(curr_view);
}

/* Frees memory allocated for selected files list in keys_info_t structure and
 * clears it. */
static void
free_list_of_file_indexes(keys_info_t *keys_info)
{
	free(keys_info->indexes);
	keys_info->indexes = NULL;
	keys_info->count = 0;
}

/* Filter the files matching the filename filter. */
static void
cmd_zM(key_info_t key_info, keys_info_t *keys_info)
{
	restore_filename_filter(curr_view);
	local_filter_restore(curr_view);
	set_dot_files_visible(curr_view, 0);
}

/* Remove filename filter. */
static void
cmd_zO(key_info_t key_info, keys_info_t *keys_info)
{
	remove_filename_filter(curr_view);
}

/* Show all hidden files. */
static void
cmd_zR(key_info_t key_info, keys_info_t *keys_info)
{
	remove_filename_filter(curr_view);
	local_filter_remove(curr_view);
	set_dot_files_visible(curr_view, 1);
}

/* Toggle dot files visibility. */
static void
cmd_za(key_info_t key_info, keys_info_t *keys_info)
{
	toggle_dot_files(curr_view);
}

/* Excludes entries from custom view. */
static void
cmd_zd(key_info_t key_info, keys_info_t *keys_info)
{
	flist_custom_exclude(curr_view);
}

/* Redraw with file in bottom of list. */
void
normal_cmd_zb(key_info_t key_info, keys_info_t *keys_info)
{
	if(can_scroll_up(curr_view))
	{
		int bottom = get_window_bottom_pos(curr_view);
		scroll_up(curr_view, bottom - curr_view->list_pos);
		redraw_current_view();
	}
}

/* Filter selected files. */
static void
cmd_zf(key_info_t key_info, keys_info_t *keys_info)
{
	filter_selected_files(curr_view);
}

/* Hide dot files. */
static void
cmd_zm(key_info_t key_info, keys_info_t *keys_info)
{
	set_dot_files_visible(curr_view, 0);
}

/* Show all the dot files. */
static void
cmd_zo(key_info_t key_info, keys_info_t *keys_info)
{
	set_dot_files_visible(curr_view, 1);
}

/* Reset local filter. */
static void
cmd_zr(key_info_t key_info, keys_info_t *keys_info)
{
	local_filter_remove(curr_view);
}

/* Moves cursor to the beginning of the previous group of files defined by the
 * primary sorting key. */
static void
cmd_left_paren(key_info_t key_info, keys_info_t *keys_info)
{
	pick_or_move(keys_info, flist_find_group(curr_view, 0));
}

/* Moves cursor to the beginning of the next group of files defined by the
 * primary sorting key. */
static void
cmd_right_paren(key_info_t key_info, keys_info_t *keys_info)
{
	pick_or_move(keys_info, flist_find_group(curr_view, 1));
}

/* Moves cursor to the beginning of the previous group of files defined by them
 * being directory or files. */
static void
cmd_left_curly_bracket(key_info_t key_info, keys_info_t *keys_info)
{
	pick_or_move(keys_info, flist_find_dir_group(curr_view, 0));
}

/* Moves cursor to the beginning of the next group of files defined by them
 * being directory or files. */
static void
cmd_right_curly_bracket(key_info_t key_info, keys_info_t *keys_info)
{
	pick_or_move(keys_info, flist_find_dir_group(curr_view, 1));
}

/* Redraw with file in top of list. */
void
normal_cmd_zt(key_info_t key_info, keys_info_t *keys_info)
{
	if(can_scroll_down(curr_view))
	{
		int top = get_window_top_pos(curr_view);
		scroll_down(curr_view, curr_view->list_pos - top);
		redraw_current_view();
	}
}

/* Redraw with file in center of list. */
void
normal_cmd_zz(key_info_t key_info, keys_info_t *keys_info)
{
	if(!all_files_visible(curr_view))
	{
		int middle = get_window_middle_pos(curr_view);
		scroll_by_files(curr_view, curr_view->list_pos - middle);
		redraw_current_view();
	}
}

static void
pick_files(FileView *view, int end, keys_info_t *keys_info)
{
	int delta, i, x;

	end = MAX(0, end);
	end = MIN(view->list_rows - 1, end);

	keys_info->count = abs(view->list_pos - end) + 1;
	keys_info->indexes = calloc(keys_info->count, sizeof(int));
	if(keys_info->indexes == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	delta = (view->list_pos > end) ? -1 : +1;
	i = 0;
	x = view->list_pos - delta;
	do
	{
		x += delta;
		keys_info->indexes[i++] = x;
	}
	while(x != end);

	if(end < view->list_pos)
	{
		flist_set_pos(view, end);
	}
}

static void
selector_S(key_info_t key_info, keys_info_t *keys_info)
{
	int i, x;

	keys_info->count = curr_view->list_rows - curr_view->selected_files;
	keys_info->indexes = reallocarray(NULL, keys_info->count,
			sizeof(keys_info->indexes[0]));
	if(keys_info->indexes == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	i = 0;
	for(x = 0; x < curr_view->list_rows; x++)
	{
		if(is_parent_dir(curr_view->dir_entry[x].name))
			continue;
		if(curr_view->dir_entry[x].selected)
			continue;
		keys_info->indexes[i++] = x;
	}
	keys_info->count = i;
}

static void
selector_a(key_info_t key_info, keys_info_t *keys_info)
{
	int i, x;

	keys_info->count = curr_view->list_rows;
	keys_info->indexes = reallocarray(NULL, keys_info->count,
			sizeof(keys_info->indexes[0]));
	if(keys_info->indexes == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	i = 0;
	for(x = 0; x < curr_view->list_rows; x++)
	{
		if(is_parent_dir(curr_view->dir_entry[x].name))
			continue;
		keys_info->indexes[i++] = x;
	}
	keys_info->count = i;
}

static void
selector_s(key_info_t key_info, keys_info_t *keys_info)
{
	int i, x;

	keys_info->count = 0;
	for(x = 0; x < curr_view->list_rows; x++)
		if(curr_view->dir_entry[x].selected)
			keys_info->count++;

	if(keys_info->count == 0)
		return;

	keys_info->indexes = reallocarray(NULL, keys_info->count,
			sizeof(keys_info->indexes[0]));
	if(keys_info->indexes == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	i = 0;
	for(x = 0; x < curr_view->list_rows; x++)
	{
		if(curr_view->dir_entry[x].selected)
			keys_info->indexes[i++] = x;
	}
}

int
find_npattern(FileView *view, const char pattern[], int backward,
		int interactive)
{
	int i;
	int found;
	int msg;

	msg = find_pattern(view, pattern, backward, 1, &found, interactive);
	/* Take wrong regular expression message into account, otherwise we can't
	 * distinguish "no files matched" situation from "wrong regexp". */
	found += msg;

	for(i = 0; i < search_repeat - 1; i++)
	{
		found += goto_search_match(view, backward) != 0;
	}

	/* Reset number of repeats so that future calls are not affected by the
	 * previous ones. */
	search_repeat = 1;

	return found;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
