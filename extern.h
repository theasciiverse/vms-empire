/*
 * SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
 * SPDX-License-Identifier: GPL-2.0+
 *
 * See the file COPYING, distributed with empire, for restriction
 * and warranty information.
 */

/*
extern.h -- define global non-constant storage.
*/
#include <curses.h>	// Needed for chtype
#include <stdarg.h>
#include <stddef.h>

extern gamestate_t game;

size_t buf_append(char **cursor, size_t *remaining, const char *fmt, ...);
size_t buf_vappend(char **cursor, size_t *remaining, const char *fmt,
                   va_list ap);

/* constant data */
extern piece_attr_t piece_attr[];
extern int dir_offset[];
extern char *func_name[];
extern int move_order[];
extern char type_chars[];
extern char tt_attack[];
extern char army_attack[];
extern char fighter_attack[];
extern char ship_attack[];

extern move_info_t tt_load;
extern move_info_t tt_explore;
extern move_info_t tt_unload;
extern move_info_t army_fight;
extern move_info_t army_load;
extern move_info_t fighter_fight;
extern move_info_t ship_fight;
extern move_info_t ship_repair;
extern move_info_t user_army;
extern move_info_t user_army_attack;
extern move_info_t user_fighter;
extern move_info_t user_ship;
extern move_info_t user_ship_repair;

extern char *help_cmd[];
extern char *help_edit[];
extern char *help_user[];
extern int cmd_lines;
extern int edit_lines;
extern int user_lines;

/* Screen updating macros */
#define display_loc_u(loc) display_loc(USER, game.user_map, loc)
#define display_loc_c(loc) display_loc(COMP, game.comp_map, loc)
#define print_sector_u(sector) print_sector(USER, game.user_map, sector)
#define print_sector_c(sector) print_sector(COMP, game.comp_map, sector)
#define loc_row(loc) ((loc) / MAP_WIDTH)
#define loc_col(loc) ((loc) % MAP_WIDTH)
#define row_col_loc(row, col) ((long)((row)*MAP_WIDTH + (col)))
#define sector_row(sector) ((sector) % SECTOR_ROWS)
#define sector_col(sector) ((sector) / SECTOR_ROWS)
#define row_col_sector(row, col) ((int)((col)*SECTOR_ROWS + (row)))

#define loc_sector(loc)                                                        \
	row_col_sector(loc_row(loc) / ROWS_PER_SECTOR,                         \
	               loc_col(loc) / COLS_PER_SECTOR)

#define sector_loc(sector)                                                     \
	row_col_loc(                                                           \
	    sector_row(sector) * ROWS_PER_SECTOR + ROWS_PER_SECTOR / 2,        \
	    sector_col(sector) * COLS_PER_SECTOR + COLS_PER_SECTOR / 2)

/* global routines */

void empire(void);
void configure_geometry(int width, int height);

void attack(piece_info_t *att_obj, long loc);
void comp_move(int nmoves);
void user_move(void);
void edit(long edit_cursor);

/* map routines */
void vmap_cont(int *cont_map, const view_map_t *vmap, long loc, char bad_terrain);
void rmap_cont(int *cont_map, long loc, char bad_terrain);
void vmap_mark_up_cont(int *cont_map, const view_map_t *vmap, long loc,
                       char bad_terrain);
scan_counts_t vmap_cont_scan(const int *cont_map, const view_map_t *vmap);
scan_counts_t rmap_cont_scan(const int *cont_map);
bool map_cont_edge(const int *cont_map, long loc);
long vmap_find_aobj(path_map_t path_map[], view_map_t *vmap, long loc,
                    move_info_t *move_info);
long vmap_find_wobj(path_map_t path_map[], view_map_t *vmap, long loc,
                    move_info_t *move_info);
long vmap_find_lobj(path_map_t path_map[], view_map_t *vmap, long loc,
                    move_info_t *move_info);
long vmap_find_lwobj(path_map_t path_map[], view_map_t *vmap, long loc,
                     move_info_t *move_info, int beat_cost);
long vmap_find_wlobj(path_map_t path_map[], view_map_t *vmap, long loc,
                     move_info_t *move_info);
long vmap_find_dest(path_map_t path_map[], view_map_t vmap[], long cur_loc,
                    long dest_loc, int owner, int terrain);
void vmap_prune_explore_locs(view_map_t *vmap);
void vmap_mark_path(path_map_t *path_map, view_map_t *vmap, long dest);
void vmap_mark_adjacent(path_map_t path_map[], long loc);
void vmap_mark_near_path(path_map_t path_map[], long loc);
long vmap_find_dir(path_map_t path_map[], view_map_t *vmap, long loc,
                   const char *terrain, const char *adjchar);
int vmap_count_adjacent(const view_map_t *vmap, long loc, const char *adj_char);
bool vmap_shore(const view_map_t *vmap, long loc);
bool rmap_shore(long loc);
bool vmap_at_sea(const view_map_t *vmap, long loc);
bool rmap_at_sea(long loc);
loc_t wrap_map_loc(loc_t loc);

/* display routines */
void announce(char *);
void redisplay(void);
void kill_display(void);
void sector_change(void);
int cur_sector(void);
long cur_cursor(void);
void display_loc(int whose, view_map_t vmap[], long loc);
void display_locx(int whose, view_map_t vmap[], long loc);
void print_sector(int whose, view_map_t vmap[], int sector);
bool move_cursor(long *cursor, int offset);
void print_zoom(view_map_t *vmap);
void print_pzoom(char *s, path_map_t *pmap, view_map_t *vmap);
void print_xzoom(view_map_t *vmap);
void display_score(void);
#ifdef A_COLOR
void init_colors(void);
#endif /* A_COLOR */
void redraw(void);
void clear_screen(void);
void complain(void);
void delay(void);
void close_disp(void);
void pos_str(int row, int col, char *str, ...);
int direction(chtype c);

void init_game(void); /* game routines */
void save_game(void);
bool restore_game(void);
void save_movie_screen(void);
void replay_movie(void);

bool get_str(char *buf, int sizep); /* input routines */
bool get_strq(char *buf, int sizep);
unsigned char get_chx(void);
int getint(char *message);
unsigned char get_c(void);
unsigned char get_cq(void);
bool getyn(char *message);
int get_range(char *message, int low, int high);

void rndini(void); /* math routines */
long irand(long high);
int dist(long a, long b);
int isqrt(int n);

int find_nearest_city(long loc, int owner, long *city_loc);
city_info_t *find_city(long loc); /* object routines */
piece_info_t *find_obj(int type, long loc);
piece_info_t *find_nfull(int type, long loc, int owner);
long find_transport(int owner, long loc);
piece_info_t *find_obj_at_loc(long loc);
int obj_moves(const piece_info_t *obj);
int obj_capacity(const piece_info_t *obj);
void kill_obj(piece_info_t *obj, long loc);
void kill_city(city_info_t *cityp);
void produce(city_info_t *cityp);
void move_obj(piece_info_t *obj, long new_loc);
void move_sat(piece_info_t *obj);
bool good_loc(const piece_info_t *obj, long loc);
void embark(piece_info_t *ship, piece_info_t *obj);
void disembark(piece_info_t *obj);
void describe_obj(piece_info_t *obj);
void scan(view_map_t vmap[], long loc);
void scan_sat(view_map_t *vmap, long loc);
void set_prod(city_info_t *cityp);

/* terminal routines */
void pdebug(char *s, ...);
void topini(void);
void clreol(int line, int colp);
void topmsg(int line, char *fmt, ...);
void prompt(char *fmt, ...);
void error(char *fmt, ...);
void info(char *a, char *b, char *c);
void comment(char *fmt, ...);
void extra(char *fmt, ...);
void huh(void);
void help(char **text, int nlines);
void set_need_delay(void);
void ksend(char *fmt, ...);

/* utility routines */
void ttinit(void);
void update_display_size(void);
void eassert(char *expression, char *file, int line);
void empend(void);
char upper(char c);
void tupper(char *str);
void check(void);
int loc_disp(int loc);
