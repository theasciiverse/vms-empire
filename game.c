/*
 * SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
 * SPDX-License-Identifier: GPL-2.0+
 *
 * See the file COPYING, distributed with empire, for restriction
 * and warranty information.
 */

/*
game.c -- Routines to initialize, save, and restore a game.
*/

#include "empire.h"
#include "extern.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void make_map(void);
static void place_cities(void);
static count_t regen_land(count_t placed);
static count_t remove_land(loc_t loc, count_t num_land);
static bool select_cities(void);
static void find_cont(void);
static bool find_next(loc_t *mapi);
static bool good_cont(loc_t mapi);
static void make_pair(void);
static bool xread(FILE *f, char *buf, unsigned int size);
static bool xwrite(FILE *f, const char *buf, unsigned int size);
static void stat_display(const char *mbuf, int round);

static int *height[2];
static loc_t *land;
static int *marked;
static char *mapbuf;

static void *xmalloc(size_t size) {
	void *p = malloc(size);

	if (p == NULL) {
		perror("Cannot allocate map storage");
		exit(1);
	}
	return p;
}

void configure_geometry(int width, int height_arg) {
	game.map_width = width;
	game.map_height = height_arg;
	game.map_size = width * height_arg;
	game.num_city = (100 * (width + height_arg)) / 228;

	game.real_map = xmalloc(sizeof(real_map_t) * MAP_SIZE);
	game.comp_map = xmalloc(sizeof(view_map_t) * MAP_SIZE);
	game.user_map = xmalloc(sizeof(view_map_t) * MAP_SIZE);
	game.city = xmalloc(sizeof(city_info_t) * NUM_CITY);

	height[0] = xmalloc(sizeof(int) * (MAP_SIZE + 1));
	height[1] = xmalloc(sizeof(int) * (MAP_SIZE + 1));
	land = xmalloc(sizeof(loc_t) * MAP_SIZE);
	marked = xmalloc(sizeof(int) * MAP_SIZE);
	mapbuf = xmalloc(sizeof(char) * MAP_SIZE);

	dir_offset[NORTH] = -MAP_WIDTH;
	dir_offset[NORTHEAST] = -MAP_WIDTH + 1;
	dir_offset[EAST] = 1;
	dir_offset[SOUTHEAST] = MAP_WIDTH + 1;
	dir_offset[SOUTH] = MAP_WIDTH;
	dir_offset[SOUTHWEST] = MAP_WIDTH - 1;
	dir_offset[WEST] = -1;
	dir_offset[NORTHWEST] = -MAP_WIDTH - 1;
}

/*
Initialize a new game.  Here we generate a new random map, put cities
on the map, select cities for each opponent, and zero out the lists of
pieces on the board.
*/

void init_game(void) {
	count_t i;

	kill_display(); /* nothing on screen */
	game.automove = false;
	game.resigned = false;
	game.debug = false;
	game.print_debug = false;
	game.print_vmap = false;
	game.trace_pmap = false;
	game.save_movie = false;
	game.win = no_win;
	game.date = 0; /* no date yet */
	game.user_score = 0;
	game.comp_score = 0;

	for (i = 0; i < MAP_SIZE; i++) {
		game.user_map[i].contents = ' '; /* nothing seen yet */
		game.user_map[i].seen = 0;
		game.comp_map[i].contents = ' ';
		game.comp_map[i].seen = 0;
	}
	for (i = 0; i < NUM_OBJECTS; i++) {
		game.user_obj[i] = NULL;
		game.comp_obj[i] = NULL;
	}
	game.free_list = NULL;            /* nothing free yet */
	for (i = 0; i < LIST_SIZE; i++) { /* for each object */
		piece_info_t *obj = &(game.object[i]);
		obj->hits = 0; /* mark object as dead */
		obj->owner = UNOWNED;
		LINK(game.free_list, obj, piece_link);
	}

	make_map(); /* make land and water */

	do {
		for (i = 0; i < MAP_SIZE; i++) { /* remove cities */
			if (game.real_map[i].contents == MAP_CITY) {
				game.real_map[i].contents = MAP_LAND; /* land */
			}
		}
		place_cities();     /* place cities on game.real_map */
	} while (!select_cities()); /* choose a city for each player */
}

/*
Create a map.  To do this, we first randomly assign heights to each
map location.  Then we smooth these heights.  The more we smooth,
the better the land and water will clump together.  Then we decide
how high the land will be.  We attempt to choose enough land to meet
some required percentage.

There are two parameters to this algorithm:  the amount we will smooth,
and the ratio of land to water.  The user can provide these numbers
at program start up.
*/

#define MAX_HEIGHT 999 /* highest height */

static int height_count[MAX_HEIGHT + 1];

static void make_map(void) {
	int from, to, k, water_line;
	count_t i, j, sum, playable, target_water, water_at_line;
	loc_t loc;

	for (i = 0; i < MAP_SIZE;
	     i++) { /* fill game.real_map with random sand */
		height[0][i] = irand(MAX_HEIGHT);
	}

	from = 0;
	to = 1;
	for (i = 0; i < game.SMOOTH; i++) { /* smooth the game.real_map */
		for (j = 0; j < MAP_SIZE; j++) {
			sum = height[from][j];
			for (k = 0; k < 8; k++) {
				loc = j + dir_offset[k];
				/* edges get smoothed in a wierd fashion */
				if (loc < 0 || loc >= MAP_SIZE) {
					loc = j;
				}
				sum += height[from][loc];
			}
			height[to][j] = sum / 9;
		}
		k = to; /* swap to and from */
		to = from;
		from = k;
	}

	/* Count playable cells at each height. */
	for (i = 0; i <= MAX_HEIGHT; i++) {
		height_count[i] = 0;
	}

	playable = 0;
	for (i = 0; i < MAP_SIZE; i++) {
		j = loc_col(i);
		k = loc_row(i);
		game.real_map[i].on_board = !(j == 0 || j == MAP_WIDTH - 1 ||
		                              k == 0 || k == MAP_HEIGHT - 1);
		if (game.real_map[i].on_board) {
			height_count[height[from][i]]++;
			playable++;
		}
	}

	/* Find the water line, leaving enough playable land for every city. */
	target_water = playable * game.WATER_RATIO / 100;
	if (target_water > playable - NUM_CITY) {
		target_water = playable - NUM_CITY;
	}
	water_line = MAX_HEIGHT;
	sum = 0;
	for (i = 0; i <= MAX_HEIGHT; i++) {
		if (sum + height_count[i] >= target_water) {
			water_line = (int)i;
			break;
		}
		sum += height_count[i];
	}
	water_at_line = target_water - sum;

	/* Mark cells below the water line and collect cells tied at the line. */
	sum = 0;
	for (i = 0; i < MAP_SIZE; i++) {
		if (!game.real_map[i].on_board || height[from][i] < water_line) {
			game.real_map[i].contents = MAP_SEA;
		} else {
			game.real_map[i].contents = MAP_LAND;
			if (height[from][i] == water_line) {
				land[sum++] = i;
			}
		}

		game.real_map[i].objp = NULL; /* nothing in cell yet */
		game.real_map[i].cityp = NULL;
	}

	/* Randomly split equal-height cells to hit the requested ratio exactly. */
	for (i = 0; i < water_at_line; i++) {
		count_t choice = i + irand(sum - i);
		loc_t selected = land[choice];

		land[choice] = land[i];
		land[i] = selected;
		game.real_map[selected].contents = MAP_SEA;
	}
}

/*
Randomly place cities on the land.  There is a minimum distance that
should exist between cities.  We maintain a list of acceptable land cells
on which a city may be placed.  We randomly choose elements from this
list until all the cities are placed.  After each choice of a land cell
for a city, we remove land cells which are too close to the city.
*/

static void place_cities(void) {
	count_t placed;
	count_t num_land;

	num_land = 0; /* nothing in land array yet */
	placed = 0;   /* nothing placed yet */
	while (placed < NUM_CITY) {
		count_t i;
		loc_t loc;

		while (num_land == 0) {
			num_land = regen_land(placed);
		}
		i = irand(num_land); /* select random piece of land */
		loc = land[i];

		game.city[placed].loc = loc;
		game.city[placed].owner = UNOWNED;
		game.city[placed].work = 0;
		game.city[placed].prod = NOPIECE;

		for (i = 0; i < NUM_OBJECTS; i++) {
			game.city[placed].func[i] = NOFUNC; /* no function */
		}
		game.real_map[loc].contents = MAP_CITY;
		game.real_map[loc].cityp = &(game.city[placed]);
		placed++;

		/* Now remove any land too close to selected land. */
		num_land = remove_land(loc, num_land);
	}
}

/*
When we run out of available land, we recreate our land list.  We
put all land in the list, decrement the min_city_dist, and then
remove any land which is too close to a city.
*/

static count_t regen_land(count_t placed) {
	count_t num_land;
	count_t i;

	num_land = 0;
	for (i = 0; i < MAP_SIZE; i++) {
		if (game.real_map[i].on_board &&
		    game.real_map[i].contents == MAP_LAND) {
			land[num_land] = i; /* remember piece of land */
			num_land++;         /* remember number of pieces */
		}
	}
	if (placed > 0) { /* don't decrement 1st time */
		game.MIN_CITY_DIST -= 1;
		ASSERT(game.MIN_CITY_DIST >= 0);
	}
	for (i = 0; i < placed; i++) { /* for each placed city */
		num_land = remove_land(game.city[i].loc, num_land);
	}
	return (num_land);
}

/*
Remove land that is too close to a city.
*/

static count_t remove_land(loc_t loc, count_t num_land) {
	count_t new, i;

	new = 0; /* nothing kept yet */
	for (i = 0; i < num_land; i++) {
		if (dist(loc, land[i]) >= game.MIN_CITY_DIST) {
			land[new] = land[i];
			new ++;
		}
	}
	return (new);
}

/*
Here we select the cities for the user and the computer.  Our choice of
cities will be heavily dependent on the difficulty level the user desires.

Our algorithm will not guarantee that either player will eventually be
able to move armies to any continent on the map.  There may be continents
which are unreachable by sea.  Consider the case of an island in a lake.
If the lake has no shore cities, then there is no way for a boat to reach
the island.  Our hope is that there will be enough water on the map, or enough
land, and that there will be enough cities, to make this case extremely rare.

First we make a list of continents which contain at least two cities, one
or more of which is on the coast.  If there are no such continents, we return
false, and our caller should decide again where cities should be placed
on the map.  While making this list, we will rank the continents.  Our ranking
is based on the thought that shore cities are better than inland cities,
that any city is very important, and that the land area of a continent
is mildly important.  Usually, we expect every continent to have a different
ranking.  It will be unusual to have two continents with the same land area,
the same number of shore cities, and the same number of inland cities.  When
this is not the case, the first city encountered will be given the highest
rank.

We then rank pairs of continents.  We tell the user the number of different
ranks, and ask the user what rank they want to use.  This is how the
user specifies the difficulty level.  Using that pair, we have now decided
on a continent for each player.  We now choose a random city on each continent,
making sure the cities are not the same.
*/

#define MAX_CONT 10 /* most continents we will allow */

typedef struct cont {                 /* a continent */
	long value;                   /* value of continent */
	int ncity;                    /* number of cities */
	city_info_t *cityp[MAX_NUM_CITY]; /* pointer to city */
} cont_t;

typedef struct pair {
	long value;    /* value of pair for user */
	int user_cont; /* index to user continent */
	int comp_cont; /* index to computer continent */
} pair_t;

static int ncont;                 /* number of continents */
static cont_t cont_tab[MAX_CONT]; /* list of good continenets */
static int rank_tab[MAX_CONT];    /* indices to cont_tab in order of rank */
static pair_t pair_tab[MAX_CONT * MAX_CONT]; /* ranked pairs of continents */

static bool select_cities(void) {
	loc_t compi;
	city_info_t *compp, *userp;
	int comp_cont, user_cont;
	int pair;

	find_cont(); /* find and rank the continents */
	if (ncont == 0) {
		return (false); /* there are no good continents */
	}
	make_pair(); /* create list of ranked pairs */

	{
		char *cur = game.jnkbuf;
		size_t rem = sizeof(game.jnkbuf);

		(void)buf_append(
		    &cur, &rem,
		    "Choose a difficulty level where 0 is easy and %d is hard: ",
		    ncont * ncont - 1);
	}

	pair = get_range(game.jnkbuf, 0, ncont * ncont - 1);
	comp_cont = pair_tab[pair].comp_cont;
	user_cont = pair_tab[pair].user_cont;

	compi = irand((long)cont_tab[comp_cont].ncity);
	compp = cont_tab[comp_cont].cityp[compi];

	do { /* select different user city */
		loc_t useri = irand((long)cont_tab[user_cont].ncity);
		userp = cont_tab[user_cont].cityp[useri];
	} while (userp == compp);

	topmsg(1, "Your city is at %d.", loc_disp(userp->loc));
	delay(); /* let user see output before we set_prod */

	/* update city and map */
	compp->owner = COMP;
	compp->prod = ARMY;
	compp->work = 0;
	scan(game.comp_map, compp->loc);

	userp->owner = USER;
	userp->work = 0;
	scan(game.user_map, userp->loc);
	set_prod(userp);
	return (true);
}

/*
Find all continents with 2 cities or more, one of which must be a shore
city.  We rank the continents.
*/

static void find_cont(void) {
	loc_t i;
	loc_t mapi;

	for (i = 0; i < MAP_SIZE; i++) {
		marked[i] = 0; /* nothing marked yet */
	}
	ncont = 0; /* no continents found yet */
	mapi = 0;

	while (ncont < MAX_CONT) {
		if (!find_next(&mapi)) {
			return; /* all found */
		}
	}
}

/*
Find the next continent and insert it in the rank table.
If there are no more continents, we return false.
*/

static bool find_next(loc_t *mapi) {
	count_t i;
	long val;

	for (;;) {
		if (*mapi >= MAP_SIZE) {
			return (false);
		}

		if (!game.real_map[*mapi].on_board || marked[*mapi] ||
		    game.real_map[*mapi].contents == MAP_SEA) {
			*mapi += 1;
		} else if (good_cont(*mapi)) {
			rank_tab[ncont] = ncont; /* insert cont in rank tab */
			val = cont_tab[ncont].value;

			for (i = ncont; i > 0; i--) { /* bubble up new rank */
				if (val > cont_tab[rank_tab[i - 1]].value) {
					rank_tab[i] = rank_tab[i - 1];
					rank_tab[i - 1] = ncont;
				} else {
					break;
				}
			}
			ncont++; /* count continents */
			return (true);
		}
	}
}

/*
Map out the current continent.  We mark every piece of land on the continent,
count the cities, shore cities, and land area of the continent.  If the
continent contains 2 cities and a shore city, we set the value of the
continent and return true.  Otherwise we return false.
*/

static count_t ncity, nland, nshore;
static void mark_cont(loc_t);

static bool good_cont(loc_t mapi) {
	long val;

	ncity = 0; /* nothing seen yet */
	nland = 0;
	nshore = 0;

	mark_cont(mapi);

	if (nshore < 1 || ncity < 2) {
		return (false);
	}

	/* The first two cities, one of which must be a shore city,
	   don't contribute to the value.  Otherwise shore cities are
	   worth 3/2 an inland city.  A city is worth 1000 times as much
	   as land area. */

	if (ncity == nshore) {
		val = (nshore - 2) * 3;
	} else {
		val = (nshore - 1) * 3 + (ncity - nshore - 1) * 2;
	}

	val *= 1000; /* cities are worth a lot */
	val += nland;
	cont_tab[ncont].value = val;
	cont_tab[ncont].ncity = ncity;
	return (true);
}

/*
Mark a continent.  This algorithm marks the current square and counts it
if it is land or city.  If it is city, we also check
to see if it is a shore city, and we install it in the list of
cities for the continent.  We then examine each surrounding cell.
*/

static void mark_cont(loc_t mapi) {
	count_t pending;

	if (marked[mapi] || game.real_map[mapi].contents == MAP_SEA ||
	    !game.real_map[mapi].on_board) {
		return;
	}

	pending = 1;
	land[0] = mapi;
	marked[mapi] = 1;

	while (pending > 0) {
		int i;

		mapi = land[--pending];
		nland++;

		if (game.real_map[mapi].contents == MAP_CITY) {
			cont_tab[ncont].cityp[ncity++] = game.real_map[mapi].cityp;
			if (rmap_shore(mapi)) {
				nshore++;
			}
		}

		for (i = 0; i < 8; i++) {
			loc_t adjacent = wrap_map_loc(mapi + dir_offset[i]);
			if (!marked[adjacent] && game.real_map[adjacent].on_board &&
			    game.real_map[adjacent].contents != MAP_SEA) {
				marked[adjacent] = 1;
				land[pending++] = adjacent;
			}
		}
	}
}

/*
Create a list of pairs of continents in a ranked order.  The first
element in the list is the pair which is easiest for the user to
win with.  Our ranking is simply based on the difference in value
between the user's continent and the computer's continent.
*/

static void make_pair(void) {
	int i, j, k, npair;
	long val;

	npair = 0; /* none yet */

	for (i = 0; i < ncont; i++) {
		for (j = 0; j < ncont; j++) { /* loop through all continents */
			val = cont_tab[i].value - cont_tab[j].value;
			pair_tab[npair].value = val;
			pair_tab[npair].user_cont = i;
			pair_tab[npair].comp_cont = j;

			for (k = npair; k > 0; k--) { /* bubble up new rank */
				if (val > pair_tab[k - 1].value) {
					pair_tab[k] = pair_tab[k - 1];
					pair_tab[k - 1].user_cont = i;
					pair_tab[k - 1].comp_cont = j;
					// Sort fix from Sarvottamananda
					pair_tab[k - 1].value = val;
				} else {
					break;
				}
			}
			npair++; /* count pairs */
		}
	}
}

/*
Save a game.  We save the game in empire.sav.  Someday we may want
to ask the user for a file name.  If we cannot save the game, we will
tell the user why.
*/

/* Save-file format identifiers. */
#define SAVE_MAGIC "EMPIRE-SAVE"
#define SAVE_MAGIC_LEN 11
#define SAVE_VERSION 3
#define SAVE_FLAG_TORUS 0x00000001u
#define SAVE_KNOWN_FLAGS SAVE_FLAG_TORUS

/*
Save format (little-endian, field-wise, versioned):
Header:
  magic[11] = "EMPIRE-SAVE"
  u32 version
  u32 map_width
  u32 map_height
  u32 map_size
  u32 num_city
  u32 list_size
  u32 num_objects
  u32 feature_flags (bit 0 = toroidal topology)
State:
  i64 date
  u8  automove
  u8  resigned
  u8  debug
  i32 win
  u8  save_movie
  i32 user_score
  i32 comp_score
Maps:
  real_map: for each cell (map_size)
    u8 contents
    u8 on_board
  comp_map: for each cell
    u8 contents
    i64 seen
  user_map: for each cell
    u8 contents
    i64 seen
Cities: num_city records
  i32 loc
  u8 owner
  i64 work
  u8 prod
  i64 func[num_objects]
Objects: list_size records
  i32 owner
  i32 type
  i32 loc
  i64 func
  i32 hits
  i32 moved
  i32 count
  i32 range
Pointers and lists are reconstructed on load.
*/

static bool write_u8(FILE *f, uint8_t v) {
	return xwrite(f, (char *)&v, (int)sizeof(v));
}

static bool write_u32(FILE *f, uint32_t v) {
	uint8_t b[4];

	b[0] = (uint8_t)(v & 0xff);
	b[1] = (uint8_t)((v >> 8) & 0xff);
	b[2] = (uint8_t)((v >> 16) & 0xff);
	b[3] = (uint8_t)((v >> 24) & 0xff);
	return xwrite(f, (char *)b, (int)sizeof(b));
}

static bool write_i32(FILE *f, int32_t v) {
	return write_u32(f, (uint32_t)v);
}

static bool write_i64(FILE *f, int64_t v) {
	uint8_t b[8];
	uint64_t uv = (uint64_t)v;

	b[0] = (uint8_t)(uv & 0xff);
	b[1] = (uint8_t)((uv >> 8) & 0xff);
	b[2] = (uint8_t)((uv >> 16) & 0xff);
	b[3] = (uint8_t)((uv >> 24) & 0xff);
	b[4] = (uint8_t)((uv >> 32) & 0xff);
	b[5] = (uint8_t)((uv >> 40) & 0xff);
	b[6] = (uint8_t)((uv >> 48) & 0xff);
	b[7] = (uint8_t)((uv >> 56) & 0xff);
	return xwrite(f, (char *)b, (int)sizeof(b));
}

static bool read_u8(FILE *f, uint8_t *v) {
	return xread(f, (char *)v, sizeof(*v));
}

static bool read_u32(FILE *f, uint32_t *v) {
	uint8_t b[4];

	if (!xread(f, (char *)b, (int)sizeof(b))) {
		return false;
	}
	*v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	     ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	return true;
}

static bool read_i32(FILE *f, int32_t *v) {
	uint32_t uv;

	if (!read_u32(f, &uv)) {
		return false;
	}
	*v = (int32_t)uv;
	return true;
}

static bool read_i64(FILE *f, int64_t *v) {
	uint8_t b[8];
	uint64_t uv;

	if (!xread(f, (char *)b, (int)sizeof(b))) {
		return false;
	}
	uv = (uint64_t)b[0] | ((uint64_t)b[1] << 8) |
	     ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
	     ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) |
	     ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
	*v = (int64_t)uv;
	return true;
}

void save_game(void) {
	static const char temp_suffix[] = ".tmp.XXXXXX";
	FILE *f; /* file to save game in */
	char *temp_path;
	size_t path_len;
	int fd;
	bool ok = true;
	int i, j;

#define S_WBYTES(buf, size)                                                    \
	if (!xwrite(f, (char *)buf, (int)(size))) {                             \
		ok = false;                                                    \
		goto save_cleanup;                                            \
	}
#define S_WU8(val)                                                             \
	if (!write_u8(f, (uint8_t)(val))) {                                     \
		ok = false;                                                    \
		goto save_cleanup;                                            \
	}
#define S_WU32(val)                                                            \
	if (!write_u32(f, (uint32_t)(val))) {                                   \
		ok = false;                                                    \
		goto save_cleanup;                                            \
	}
#define S_WI32(val)                                                            \
	if (!write_i32(f, (int32_t)(val))) {                                    \
		ok = false;                                                    \
		goto save_cleanup;                                            \
	}
#define S_WI64(val)                                                            \
	if (!write_i64(f, (int64_t)(val))) {                                    \
		ok = false;                                                    \
		goto save_cleanup;                                            \
	}

	path_len = strlen(game.savefile);
	if (path_len > SIZE_MAX - sizeof(temp_suffix)) {
		fprintf(stderr, "Save file name is too long.\n");
		return;
	}
	temp_path = malloc(path_len + sizeof(temp_suffix));
	if (temp_path == NULL) {
		perror("Cannot allocate temporary save file name");
		return;
	}
	(void)snprintf(temp_path, path_len + sizeof(temp_suffix), "%s%s",
		       game.savefile, temp_suffix);

	fd = mkstemp(temp_path);
	if (fd < 0) {
		perror("Cannot create temporary save file");
		free(temp_path);
		return;
	}
	f = fdopen(fd, "wb");
	if (f == NULL) {
		int saved_errno = errno;

		(void)close(fd);
		(void)unlink(temp_path);
		errno = saved_errno;
		perror("Cannot open temporary save file");
		free(temp_path);
		return;
	}

	S_WBYTES(SAVE_MAGIC, SAVE_MAGIC_LEN);
	S_WU32(SAVE_VERSION);
	S_WU32(MAP_WIDTH);
	S_WU32(MAP_HEIGHT);
	S_WU32(MAP_SIZE);
	S_WU32(NUM_CITY);
	S_WU32(LIST_SIZE);
	S_WU32(NUM_OBJECTS);
	S_WU32(game.tflag ? SAVE_FLAG_TORUS : 0);

	S_WI64(game.date);
	S_WU8(game.automove);
	S_WU8(game.resigned);
	S_WU8(game.debug);
	S_WI32(game.win);
	S_WU8(game.save_movie);
	S_WI32(game.user_score);
	S_WI32(game.comp_score);

	for (i = 0; i < MAP_SIZE; i++) {
		S_WU8(game.real_map[i].contents);
		S_WU8(game.real_map[i].on_board);
	}

	for (i = 0; i < MAP_SIZE; i++) {
		S_WU8(game.comp_map[i].contents);
		S_WI64(game.comp_map[i].seen);
	}
	for (i = 0; i < MAP_SIZE; i++) {
		S_WU8(game.user_map[i].contents);
		S_WI64(game.user_map[i].seen);
	}

	for (i = 0; i < NUM_CITY; i++) {
		S_WI32(game.city[i].loc);
		S_WU8(game.city[i].owner);
		S_WI64(game.city[i].work);
		S_WU8((uint8_t)game.city[i].prod);
		for (j = 0; j < NUM_OBJECTS; j++) {
			S_WI64(game.city[i].func[j]);
		}
	}

	for (i = 0; i < LIST_SIZE; i++) {
		const piece_info_t *obj = &game.object[i];
		S_WI32(obj->owner);
		S_WI32(obj->type);
		S_WI32(obj->loc);
		S_WI64(obj->func);
		S_WI32(obj->hits);
		S_WI32(obj->moved);
		S_WI32(obj->count);
		S_WI32(obj->range);
	}

save_cleanup:
	if (ok && fflush(f) != 0) {
		perror("Cannot flush temporary save file");
		ok = false;
	}
	if (ok && fsync(fileno(f)) != 0) {
		perror("Cannot sync temporary save file");
		ok = false;
	}
	if (fclose(f) != 0) {
		perror("Cannot close temporary save file");
		ok = false;
	}
	if (ok && rename(temp_path, game.savefile) != 0) {
		perror("Cannot replace saved game");
		ok = false;
	}
	if (ok) {
		topmsg(3, "Game saved.");
	} else {
		if (unlink(temp_path) != 0 && errno != ENOENT) {
			perror("Cannot remove temporary save file");
		}
		topmsg(3, "Save failed.");
	}
	free(temp_path);

#undef S_WBYTES
#undef S_WU8
#undef S_WU32
#undef S_WI32
#undef S_WI64
}

static bool read_embark(piece_info_t *list, int piece_type);

static gamestate_t *new_restore_state(void) {
	gamestate_t *state = calloc(1, sizeof(*state));

	if (state == NULL) {
		perror("Cannot allocate restore staging state");
		return NULL;
	}
	state->real_map = calloc((size_t)MAP_SIZE, sizeof(*state->real_map));
	state->comp_map = calloc((size_t)MAP_SIZE, sizeof(*state->comp_map));
	state->user_map = calloc((size_t)MAP_SIZE, sizeof(*state->user_map));
	state->city = calloc((size_t)NUM_CITY, sizeof(*state->city));
	if (state->real_map == NULL || state->comp_map == NULL ||
	    state->user_map == NULL || state->city == NULL) {
		perror("Cannot allocate restore staging state");
		free(state->city);
		free(state->user_map);
		free(state->comp_map);
		free(state->real_map);
		free(state);
		return NULL;
	}
	state->map_width = MAP_WIDTH;
	state->map_height = MAP_HEIGHT;
	state->map_size = MAP_SIZE;
	state->num_city = NUM_CITY;
	return state;
}

static void free_restore_state(gamestate_t *state) {
	if (state != NULL) {
		free(state->city);
		free(state->user_map);
		free(state->comp_map);
		free(state->real_map);
		free(state);
	}
}

static bool validate_restore_state(const gamestate_t *state) {
	unsigned char *city_seen;
	int *cargo_balance;
	count_t i;
	bool valid = false;

	city_seen = calloc((size_t)MAP_SIZE, sizeof(*city_seen));
	cargo_balance = calloc((size_t)MAP_SIZE * 4, sizeof(*cargo_balance));
	if (city_seen == NULL || cargo_balance == NULL) {
		perror("Cannot allocate restore validation state");
		goto validate_cleanup;
	}

	for (i = 0; i < MAP_SIZE; i++) {
		int row = loc_row(i);
		int col = loc_col(i);
		bool expected_on_board = row > 0 && row < MAP_HEIGHT - 1 && col > 0 &&
		                         col < MAP_WIDTH - 1;

		if (state->real_map[i].on_board != expected_on_board ||
		    (state->real_map[i].contents != MAP_LAND &&
		     state->real_map[i].contents != MAP_SEA &&
		     state->real_map[i].contents != MAP_CITY)) {
			fprintf(stderr, "Saved file has invalid map data.\n");
			goto validate_cleanup;
		}
	}

	for (i = 0; i < NUM_CITY; i++) {
		const city_info_t *city = &state->city[i];
		unsigned int prod = (unsigned char)city->prod;

		if (city->loc < 0 || city->loc >= MAP_SIZE ||
		    !state->real_map[city->loc].on_board || city_seen[city->loc] ||
		    state->real_map[city->loc].contents != MAP_CITY ||
		    (city->owner != UNOWNED && city->owner != USER &&
		     city->owner != COMP) ||
		    (prod != (unsigned char)NOPIECE && prod >= NUM_OBJECTS)) {
			fprintf(stderr, "Saved file has invalid city data.\n");
			goto validate_cleanup;
		}
		city_seen[city->loc] = 1;
	}

	for (i = 0; i < MAP_SIZE; i++) {
		if ((state->real_map[i].contents == MAP_CITY) != !!city_seen[i]) {
			fprintf(stderr, "Saved file has inconsistent city data.\n");
			goto validate_cleanup;
		}
	}

	for (i = 0; i < LIST_SIZE; i++) {
		const piece_info_t *obj = &state->object[i];
		int cargo_kind = -1;
		int slot;

		if (obj->owner != UNOWNED && obj->owner != USER && obj->owner != COMP) {
			fprintf(stderr, "Saved file has invalid object owner.\n");
			goto validate_cleanup;
		}
		if (obj->hits < 0 || obj->count < 0 ||
		    (obj->owner == UNOWNED && obj->hits != 0)) {
			fprintf(stderr, "Saved file has invalid object data.\n");
			goto validate_cleanup;
		}
		if (obj->hits == 0) {
			continue;
		}
		if (obj->type < 0 || obj->type >= NUM_OBJECTS || obj->loc < 0 ||
		    obj->loc >= MAP_SIZE || !state->real_map[obj->loc].on_board) {
			fprintf(stderr, "Saved file has invalid object data.\n");
			goto validate_cleanup;
		}
		if (obj->type == SATELLITE && !valid_satellite_func(obj->func)) {
			fprintf(stderr, "Saved file has invalid satellite movement.\n");
			goto validate_cleanup;
		}
		if (obj->type == TRANSPORT) {
			cargo_kind = 0;
		} else if (obj->type == CARRIER) {
			cargo_kind = 1;
		} else if (obj->count != 0) {
			fprintf(stderr, "Saved file has invalid cargo data.\n");
			goto validate_cleanup;
		}
		if (cargo_kind >= 0) {
			if (obj->count > obj_capacity(obj)) {
				fprintf(stderr, "Saved file has invalid cargo data.\n");
				goto validate_cleanup;
			}
			slot = (obj->owner - USER) * 2 + cargo_kind;
			cargo_balance[(size_t)slot * MAP_SIZE + obj->loc] -= obj->count;
		} else if (obj->type == ARMY || obj->type == FIGHTER) {
			cargo_kind = obj->type == ARMY ? 0 : 1;
			slot = (obj->owner - USER) * 2 + cargo_kind;
			cargo_balance[(size_t)slot * MAP_SIZE + obj->loc]++;
		}
	}

	for (i = 0; i < (count_t)MAP_SIZE * 4; i++) {
		if (cargo_balance[i] < 0) {
			fprintf(stderr, "Saved file has inconsistent cargo data.\n");
			goto validate_cleanup;
		}
	}
	valid = true;

validate_cleanup:
	free(cargo_balance);
	free(city_seen);
	return valid;
}

static void install_restore_state(const gamestate_t *state) {
	count_t i;
	int j;
	piece_info_t **list;

	game.date = state->date;
	game.automove = state->automove;
	game.resigned = state->resigned;
	game.debug = state->debug;
	game.win = state->win;
	game.save_movie = state->save_movie;
	game.user_score = state->user_score;
	game.comp_score = state->comp_score;
	game.tflag = state->tflag;

	for (i = 0; i < MAP_SIZE; i++) {
		game.real_map[i].contents = state->real_map[i].contents;
		game.real_map[i].on_board = state->real_map[i].on_board;
		game.real_map[i].cityp = NULL;
		game.real_map[i].objp = NULL;
	}
	(void)memcpy(game.comp_map, state->comp_map,
	             (size_t)MAP_SIZE * sizeof(*game.comp_map));
	(void)memcpy(game.user_map, state->user_map,
	             (size_t)MAP_SIZE * sizeof(*game.user_map));
	(void)memcpy(game.city, state->city,
	             (size_t)NUM_CITY * sizeof(*game.city));

	game.free_list = NULL;
	for (j = 0; j < NUM_OBJECTS; j++) {
		game.comp_obj[j] = NULL;
		game.user_obj[j] = NULL;
	}
	for (i = 0; i < LIST_SIZE; i++) {
		piece_info_t *dst = &game.object[i];
		const piece_info_t *src = &state->object[i];

		dst->owner = src->owner;
		dst->type = src->type;
		dst->loc = src->loc;
		dst->func = src->func;
		dst->hits = src->hits;
		dst->moved = src->moved;
		dst->count = src->count;
		dst->range = src->range;
		dst->loc_link.next = NULL;
		dst->loc_link.prev = NULL;
		dst->cargo_link.next = NULL;
		dst->cargo_link.prev = NULL;
		dst->piece_link.next = NULL;
		dst->piece_link.prev = NULL;
		dst->ship = NULL;
		dst->cargo = NULL;
	}

	for (i = 0; i < NUM_CITY; i++) {
		game.real_map[game.city[i].loc].cityp = &game.city[i];
	}
	for (i = 0; i < LIST_SIZE; i++) {
		piece_info_t *obj = &game.object[i];

		if (obj->owner == UNOWNED || obj->hits == 0) {
			LINK(game.free_list, obj, piece_link);
		} else {
			list = LIST(obj->owner);
			LINK(list[obj->type], obj, piece_link);
			LINK(game.real_map[obj->loc].objp, obj, loc_link);
		}
	}
	ASSERT(read_embark(game.user_obj[TRANSPORT], ARMY));
	ASSERT(read_embark(game.user_obj[CARRIER], FIGHTER));
	ASSERT(read_embark(game.comp_obj[TRANSPORT], ARMY));
	ASSERT(read_embark(game.comp_obj[CARRIER], FIGHTER));
}

/*
Recover a saved game from empire.sav.
The live game is not modified unless the entire save parses and validates.
*/

bool restore_game(void) {
	FILE *f = NULL;
	gamestate_t *state = NULL;
	bool restored = false;
	long i;
	int j;
	piece_info_t *obj;
	uint8_t v_u8;
	uint32_t v_u32;
	int32_t v_i32;
	int64_t v_i64;
	uint32_t map_width, map_height, map_size;
	uint32_t num_city, list_size, num_objects, feature_flags;
	char magic[SAVE_MAGIC_LEN];

	f = fopen(game.savefile, "rb");
	if (f == NULL) {
		perror("Cannot open saved game");
		return false;
	}
#define R_RU8(dst)                                                             \
	do {                                                                   \
		if (!read_u8(f, &v_u8))                                        \
			goto restore_cleanup;                                 \
		dst = v_u8;                                                    \
	} while (0)
#define R_RU32(dst)                                                            \
	do {                                                                   \
		if (!read_u32(f, &v_u32))                                      \
			goto restore_cleanup;                                 \
		dst = v_u32;                                                   \
	} while (0)
#define R_RI32(dst)                                                            \
	do {                                                                   \
		if (!read_i32(f, &v_i32))                                      \
			goto restore_cleanup;                                 \
		dst = v_i32;                                                   \
	} while (0)
#define R_RI64(dst)                                                            \
	do {                                                                   \
		if (!read_i64(f, &v_i64))                                      \
			goto restore_cleanup;                                 \
		dst = v_i64;                                                   \
	} while (0)

	if (!xread(f, magic, (int)sizeof(magic))) {
		goto restore_cleanup;
	}
	if (memcmp(magic, SAVE_MAGIC, sizeof(magic)) != 0) {
		fprintf(stderr, "Saved file has unknown format.\n");
		goto restore_cleanup;
	}
	R_RU32(v_u32);
	if (v_u32 != SAVE_VERSION) {
		fprintf(stderr, "Saved file version %u is not supported.\n",
		        v_u32);
		goto restore_cleanup;
	}

	R_RU32(map_width);
	R_RU32(map_height);
	R_RU32(map_size);
	R_RU32(num_city);
	R_RU32(list_size);
	R_RU32(num_objects);
	R_RU32(feature_flags);

	if (feature_flags & ~SAVE_KNOWN_FLAGS) {
		fprintf(stderr, "Saved file uses unsupported feature flags.\n");
		goto restore_cleanup;
	}

	if (map_width != (uint32_t)MAP_WIDTH ||
	    map_height != (uint32_t)MAP_HEIGHT ||
	    map_size != (uint32_t)MAP_SIZE) {
		fprintf(stderr,
		        "Saved file map is %ux%u (size %u); current geometry is %dx%d (size %d).\n",
		        map_width, map_height, map_size, MAP_WIDTH, MAP_HEIGHT,
		        MAP_SIZE);
		goto restore_cleanup;
	}
	if (num_city != (uint32_t)NUM_CITY || list_size != LIST_SIZE ||
	    num_objects != NUM_OBJECTS) {
		fprintf(stderr,
		        "Saved file uses different limits (cities %u, objects %u, list %u).\n",
		        num_city, num_objects, list_size);
		goto restore_cleanup;
	}

	state = new_restore_state();
	if (state == NULL) {
		goto restore_cleanup;
	}
	state->tflag = (feature_flags & SAVE_FLAG_TORUS) != 0;

	R_RI64(state->date);
	R_RU8(state->automove);
	R_RU8(state->resigned);
	R_RU8(state->debug);
	R_RI32(state->win);
	R_RU8(state->save_movie);
	R_RI32(state->user_score);
	R_RI32(state->comp_score);

	for (i = 0; i < MAP_SIZE; i++) {
		R_RU8(state->real_map[i].contents);
		R_RU8(state->real_map[i].on_board);
	}
	for (i = 0; i < MAP_SIZE; i++) {
		R_RU8(state->comp_map[i].contents);
		R_RI64(state->comp_map[i].seen);
	}
	for (i = 0; i < MAP_SIZE; i++) {
		R_RU8(state->user_map[i].contents);
		R_RI64(state->user_map[i].seen);
	}

	for (i = 0; i < NUM_CITY; i++) {
		R_RI32(state->city[i].loc);
		R_RU8(state->city[i].owner);
		R_RI64(state->city[i].work);
		R_RU8(state->city[i].prod);
		for (j = 0; j < NUM_OBJECTS; j++) {
			R_RI64(state->city[i].func[j]);
		}
	}

	for (i = 0; i < LIST_SIZE; i++) {
		obj = &state->object[i];
		R_RI32(obj->owner);
		R_RI32(obj->type);
		R_RI32(obj->loc);
		R_RI64(obj->func);
		R_RI32(obj->hits);
		R_RI32(obj->moved);
		R_RI32(obj->count);
		R_RI32(obj->range);
	}

	if (!validate_restore_state(state)) {
		goto restore_cleanup;
	}
	if (fclose(f) != 0) {
		perror("Cannot close saved game");
		f = NULL;
		goto restore_cleanup;
	}
	f = NULL;
	install_restore_state(state);
	update_display_size();
	kill_display(); /* what we had is no longer good */
	topmsg(3, "Game restored from save file.");
	restored = true;

restore_cleanup:
	if (f != NULL) {
		(void)fclose(f);
	}
	free_restore_state(state);
	return restored;

#undef R_RU8
#undef R_RU32
#undef R_RI32
#undef R_RI64
}

/*
Embark cargo on a ship.  We loop through the list of ships.
We then loop through the pieces at the ship's location until
the ship has the same amount of cargo it previously had.
*/

static bool read_embark(piece_info_t *list, int piece_type) {
	piece_info_t *ship;
	piece_info_t *obj;

	for (ship = list; ship != NULL; ship = ship->piece_link.next) {
		int count = ship->count; /* get # of pieces we need */
		if (count < 0) {
			return false;
		}
		ship->count = 0; /* nothing on board yet */
		for (obj = game.real_map[ship->loc].objp; obj && count;
		     obj = obj->loc_link.next) {
			if (obj->ship == NULL && obj->type == piece_type &&
			    obj->owner == ship->owner) {
				embark(ship, obj);
				count -= 1;
			}
		}
		if (count) {
			return false;
		}
	}
	return true;
}

/*
Write a buffer to a file.  If we cannot write everything, return false.
Also, tell the user why the write did not work if it didn't.
*/

static bool xwrite(FILE *f, const char *buf, unsigned int size) {
	size_t bytes;

	bytes = fwrite(buf, 1, size, f);
	if (bytes != size) {
		if (ferror(f)) {
			perror("Write to save file failed");
		} else {
			fprintf(stderr, "Cannot complete write to save file.\n");
		}
		return (false);
	}
	return (true);
}

/*
Read a buffer from a file.  If the read fails, we tell the user why
and return false.
*/

static bool xread(FILE *f, char *buf, unsigned int size) {
	size_t bytes;

	bytes = fread(buf, 1, size, f);
	if (bytes != size) {
		if (ferror(f)) {
			perror("Read from save file failed");
		} else if (feof(f)) {
			fprintf(stderr, "Saved file is too short.\n");
		} else {
			fprintf(stderr, "Read from save file incomplete.\n");
		}
		return (false);
	}
	return (true);
}

/*
Save a movie screen.  For each cell on the board, we write out
the character that would appear on either the user's or the
computer's screen.  This information is appended to 'empmovie.dat'.
*/

extern char city_char[];

void save_movie_screen(void) {
	FILE *f; /* file to save game in */
	count_t i;
	const piece_info_t *p;

	f = fopen("empmovie.dat", "ab"); /* open for append */
	if (f == NULL) {
		perror("Cannot open empmovie.dat");
		return;
	}

	for (i = 0; i < MAP_SIZE; i++) {
		if (game.real_map[i].cityp) {
			mapbuf[i] = city_char[game.real_map[i].cityp->owner];
		} else {
			p = find_obj_at_loc(i);

			if (!p) {
				mapbuf[i] = game.real_map[i].contents;
			} else if (p->owner == USER) {
				mapbuf[i] = piece_attr[p->type].sname;
			} else {
				mapbuf[i] = tolower((unsigned char)piece_attr[p->type].sname);
			}
		}
	}
	if (!xwrite(f, mapbuf, (unsigned int)MAP_SIZE)) {
		(void)fclose(f);
		return;
	}
	if (fclose(f) != 0)
		perror("Cannot close empmovie.dat");
}

/*
Replay a movie.  We read each buffer from the file and
print it using a zoomed display.
*/

void replay_movie(void) {
	void print_movie_cell(const char *, int, int, int, int);

	FILE *f; /* file to save game in */
	int r, c;
	int round;

	f = fopen("empmovie.dat", "rb"); /* open for input */
	if (f == NULL) {
		perror("Cannot open empmovie.dat");
		return;
	}
	round = 0;
	clear_screen();
	for (;;) {
		int row_inc, col_inc;
		size_t bytes = fread(mapbuf, 1, (size_t)MAP_SIZE, f);

		if (bytes != (size_t)MAP_SIZE) {
			if (ferror(f)) {
				perror("Cannot read empmovie.dat");
			} else if (bytes != 0) {
				fprintf(stderr,
				        "Ignoring incomplete final movie frame.\n");
			}
			break;
		}
		round += 1;

		stat_display(mapbuf, round);

		row_inc = (MAP_HEIGHT + game.lines - NUMTOPS - 1) /
		          (game.lines - NUMTOPS);
		col_inc = (MAP_WIDTH + game.cols - 1) / (game.cols - 1);

		for (r = 0; r < MAP_HEIGHT; r += row_inc) {
			for (c = 0; c < MAP_WIDTH; c += col_inc) {
				print_movie_cell(mapbuf, r, c, row_inc,
				                 col_inc);
			}
		}

		(void)redisplay();
		delay();
	}
	(void)fclose(f);
}

/*
Display statistics about the game.  At the top of the screen we
print:

nn O  nn A  nn F  nn P  nn D  nn S  nn T  nn C  nn B  nn Z  xxxxx
nn X  nn a  nn f  nn p  nn d  nn s  nn t  nn c  nn b  nn z  xxxxx

There may be objects in cities and boats that aren't displayed.
The "xxxxx" field is the cumulative cost of building the hardware.
*/

/* in declared order, with city first */
static char *pieces = "OAFPDSTCBZXafpdstcbz";

static void stat_display(const char *mbuf, int round) {
	count_t i;
	int counts[2 * NUM_OBJECTS + 2];
	int user_cost, comp_cost;

	(void)memset((char *)counts, '\0', sizeof(counts));

	for (i = 0; i < MAP_SIZE; i++) {
		const char *p = strchr(pieces, mbuf[i]);
		if (p) {
			counts[p - pieces] += 1;
		}
	}
	user_cost = 0;
	for (i = 1; i <= NUM_OBJECTS; i++) {
		user_cost += counts[i] * piece_attr[i - 1].build_time;
	}

	comp_cost = 0;
	for (i = NUM_OBJECTS + 2; i <= 2 * NUM_OBJECTS + 1; i++) {
		comp_cost +=
		    counts[i] * piece_attr[i - NUM_OBJECTS - 2].build_time;
	}

	for (i = 0; i < NUM_OBJECTS + 1; i++) {
		pos_str(1, (int)i * 6, "%2d %c  ", counts[i], pieces[i]);
		pos_str(2, (int)i * 6, "%2d %c  ", counts[i + NUM_OBJECTS + 1],
		        pieces[i + NUM_OBJECTS + 1]);
	}

	pos_str(1, (int)i * 6, "%5d", user_cost);
	pos_str(2, (int)i * 6, "%5d", comp_cost);
	pos_str(0, 0, "Round %3d", (round + 1) / 2);
}

/* end */
