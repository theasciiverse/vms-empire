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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

count_t remove_land(loc_t loc, count_t num_land);
bool select_cities(void);
bool find_next(loc_t *mapi);
bool good_cont(loc_t mapi);
bool xread(FILE *f, char *buf, unsigned int size);
bool xwrite(FILE *f, const char *buf, unsigned int size);
void stat_display(const char *mbuf, int round);

/*
Initialize a new game.  Here we generate a new random map, put cities
on the map, select cities for each opponent, and zero out the lists of
pieces on the board.
*/

void init_game(void) {
	void make_map(void), place_cities(void);

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

/* these arrays give some compilers problems when they are automatic */
/* MAP_SIZE+1 to eleminate compiler warning. I think the loop is wrong
   and goes 1 too high, but safer to just increase here */
static int height[2][MAP_SIZE + 1];
static int height_count[MAX_HEIGHT + 1];

void make_map(void) {
	int from, to, k;
	count_t i, j, sum;
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

	/* count the number of cells at each height */
	for (i = 0; i <= MAX_HEIGHT; i++) {
		height_count[i] = 0;
	}

	for (i = 0; i < MAP_SIZE; i++) {
		height_count[height[from][i]]++;
	}

	/* find the water line */
	loc = MAX_HEIGHT; /* default to all water */
	sum = 0;
	for (i = 0; i <= MAX_HEIGHT; i++) {
		sum += height_count[i];
		if (sum * 100 / MAP_SIZE > game.WATER_RATIO &&
		    sum >= NUM_CITY) {
			loc = i; /* this is last height that is water */
			break;
		}
	}

	/* mark the land and water */
	for (i = 0; i < MAP_SIZE; i++) {
		if (height[from][i] > loc) {
			game.real_map[i].contents = MAP_LAND;
		} else {
			game.real_map[i].contents = MAP_SEA;
		}

		game.real_map[i].objp = NULL; /* nothing in cell yet */
		game.real_map[i].cityp = NULL;

		j = loc_col(i);
		k = loc_row(i);

		game.real_map[i].on_board = !(j == 0 || j == MAP_WIDTH - 1 ||
		                              k == 0 || k == MAP_HEIGHT - 1);
	}
}

/*
Randomly place cities on the land.  There is a minimum distance that
should exist between cities.  We maintain a list of acceptable land cells
on which a city may be placed.  We randomly choose elements from this
list until all the cities are placed.  After each choice of a land cell
for a city, we remove land cells which are too close to the city.
*/

/* avoid compiler problems with large automatic arrays */
static loc_t land[MAP_SIZE];

void place_cities(void) {
	count_t regen_land(count_t);

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
		i = irand(num_land - 1); /* select random piece of land */
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

count_t regen_land(count_t placed) {
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

count_t remove_land(loc_t loc, count_t num_land) {
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
	city_info_t *cityp[NUM_CITY]; /* pointer to city */
} cont_t;

typedef struct pair {
	long value;    /* value of pair for user */
	int user_cont; /* index to user continent */
	int comp_cont; /* index to computer continent */
} pair_t;

static int marked[MAP_SIZE];      /* list of examine cells */
static int ncont;                 /* number of continents */
static cont_t cont_tab[MAX_CONT]; /* list of good continenets */
static int rank_tab[MAX_CONT];    /* indices to cont_tab in order of rank */
static pair_t pair_tab[MAX_CONT * MAX_CONT]; /* ranked pairs of continents */

bool select_cities(void) {
	void find_cont(void), make_pair(void);

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

void find_cont(void) {
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

bool find_next(loc_t *mapi) {
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

bool good_cont(loc_t mapi) {
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
Mark a continent.  This recursive algorithm marks the current square
and counts it if it is land or city.  If it is city, we also check
to see if it is a shore city, and we install it in the list of
cities for the continent.  We then examine each surrounding cell.
*/

static void mark_cont(loc_t mapi) {
	int i;

	if (marked[mapi] || game.real_map[mapi].contents == MAP_SEA ||
	    !game.real_map[mapi].on_board) {
		return;
	}

	marked[mapi] = 1; /* mark this cell seen */
	nland++;          /* count land on continent */

	if (game.real_map[mapi].contents == MAP_CITY) { /* a city? */
		cont_tab[ncont].cityp[ncity] = game.real_map[mapi].cityp;
		ncity++;
		if (rmap_shore(mapi)) {
			nshore++;
		}
	}

	for (i = 0; i < 8; i++) { /* look at surrounding squares */
		mark_cont(mapi + dir_offset[i]);
	}
}

/*
Create a list of pairs of continents in a ranked order.  The first
element in the list is the pair which is easiest for the user to
win with.  Our ranking is simply based on the difference in value
between the user's continent and the computer's continent.
*/

void make_pair(void) {
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

/* macro to save typing; write an array, return if it fails */
#define wbuf(buf)                                                              \
	if (!xwrite(f, (char *)buf, sizeof(buf)))                              \
		return
#define wval(val)                                                              \
	if (!xwrite(f, (char *)&val, sizeof(val)))                             \
		return

/* Save-file format identifiers. */
#define SAVE_MAGIC "EMPIRE-SAVE"
#define SAVE_MAGIC_LEN 11
#define SAVE_VERSION 2

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
	FILE *f; /* file to save game in */
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

	f = fopen(game.savefile, "wb"); /* open for output */
	if (f == NULL) {
		perror("Cannot save saved game");
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
	(void)fclose(f);
	if (ok) {
		topmsg(3, "Game saved.");
	} else {
		if (remove(game.savefile) != 0) {
			perror("Cannot remove partial save file");
		}
		topmsg(3, "Save failed.");
	}

#undef S_WBYTES
#undef S_WU8
#undef S_WU32
#undef S_WI32
#undef S_WI64
}

/*
Recover a saved game from empire.sav.
We return true if we succeed, otherwise false.
*/

int restore_game(void) {
	void read_embark(piece_info_t *, int);

	FILE *f; /* file to save game in */
	long i;
	int j;
	piece_info_t **list;
	piece_info_t *obj;
	uint8_t v_u8;
	uint32_t v_u32;
	int32_t v_i32;
	int64_t v_i64;
	uint32_t map_width, map_height, map_size;
	uint32_t num_city, list_size, num_objects;
	char magic[SAVE_MAGIC_LEN];

	f = fopen(game.savefile, "rb"); /* open for input */
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

	if (map_width != MAP_WIDTH || map_height != MAP_HEIGHT ||
	    map_size != MAP_SIZE) {
		fprintf(stderr,
		        "Saved file map is %ux%u (size %u); this build uses %dx%d (size %d).\n",
		        map_width, map_height, map_size, MAP_WIDTH, MAP_HEIGHT,
		        MAP_SIZE);
		goto restore_cleanup;
	}
	if (num_city != NUM_CITY || list_size != LIST_SIZE ||
	    num_objects != NUM_OBJECTS) {
		fprintf(stderr,
		        "Saved file uses different limits (cities %u, objects %u, list %u).\n",
		        num_city, num_objects, list_size);
		goto restore_cleanup;
	}

	R_RI64(game.date);
	R_RU8(game.automove);
	R_RU8(game.resigned);
	R_RU8(game.debug);
	R_RI32(game.win);
	R_RU8(game.save_movie);
	R_RI32(game.user_score);
	R_RI32(game.comp_score);
	game.automove = !!game.automove;
	game.resigned = !!game.resigned;
	game.debug = !!game.debug;
	game.save_movie = !!game.save_movie;

	for (i = 0; i < MAP_SIZE; i++) {
		R_RU8(game.real_map[i].contents);
		R_RU8(game.real_map[i].on_board);
		game.real_map[i].on_board = !!game.real_map[i].on_board;
		game.real_map[i].cityp = NULL;
		game.real_map[i].objp = NULL;
	}
	for (i = 0; i < MAP_SIZE; i++) {
		R_RU8(game.comp_map[i].contents);
		R_RI64(game.comp_map[i].seen);
	}
	for (i = 0; i < MAP_SIZE; i++) {
		R_RU8(game.user_map[i].contents);
		R_RI64(game.user_map[i].seen);
	}

	for (i = 0; i < NUM_CITY; i++) {
		R_RI32(game.city[i].loc);
		R_RU8(game.city[i].owner);
		R_RI64(game.city[i].work);
		R_RU8(game.city[i].prod);
		for (j = 0; j < NUM_OBJECTS; j++) {
			R_RI64(game.city[i].func[j]);
		}
		if (game.city[i].owner != UNOWNED && game.city[i].owner != USER &&
		    game.city[i].owner != COMP) {
			fprintf(stderr, "Saved file has invalid city owner.\n");
			goto restore_cleanup;
		}
	}

	for (i = 0; i < LIST_SIZE; i++) {
		obj = &(game.object[i]);
		R_RI32(obj->owner);
		R_RI32(obj->type);
		R_RI32(obj->loc);
		R_RI64(obj->func);
		R_RI32(obj->hits);
		R_RI32(obj->moved);
		R_RI32(obj->count);
		R_RI32(obj->range);
		if (obj->owner != UNOWNED && obj->owner != USER &&
		    obj->owner != COMP) {
			fprintf(stderr, "Saved file has invalid object owner.\n");
			goto restore_cleanup;
		}
		if (obj->hits < 0 || obj->count < 0) {
			fprintf(stderr, "Saved file has invalid object data.\n");
			goto restore_cleanup;
		}
	}

	/* Our pointers may not be valid because of source
	   changes or other things.  We recreate them. */

	game.free_list = NULL; /* zero all ptrs */
	for (i = 0; i < LIST_SIZE; i++) {
		game.object[i].loc_link.next = NULL;
		game.object[i].loc_link.prev = NULL;
		game.object[i].cargo_link.next = NULL;
		game.object[i].cargo_link.prev = NULL;
		game.object[i].piece_link.next = NULL;
		game.object[i].piece_link.prev = NULL;
		game.object[i].ship = NULL;
		game.object[i].cargo = NULL;
	}
	for (i = 0; i < NUM_OBJECTS; i++) {
		game.comp_obj[i] = NULL;
		game.user_obj[i] = NULL;
	}
	/* put cities on game.real_map */
	for (i = 0; i < NUM_CITY; i++) {
		if (game.city[i].loc < 0 || game.city[i].loc >= MAP_SIZE) {
			fprintf(stderr, "Saved file has invalid city location.\n");
			goto restore_cleanup;
		}
		game.real_map[game.city[i].loc].cityp = &(game.city[i]);
	}

	/* put pieces in free list or on map and in object lists */
	for (i = 0; i < LIST_SIZE; i++) {
		obj = &(game.object[i]);
		if (game.object[i].owner == UNOWNED ||
		    game.object[i].hits == 0) {
			LINK(game.free_list, obj, piece_link);
		} else {
			if (obj->loc < 0 || obj->loc >= MAP_SIZE ||
			    obj->type < 0 || obj->type >= NUM_OBJECTS) {
				fprintf(stderr,
				        "Saved file has invalid object data.\n");
				goto restore_cleanup;
			}
			list = LIST(game.object[i].owner);
			LINK(list[game.object[i].type], obj, piece_link);
			LINK(game.real_map[game.object[i].loc].objp, obj,
			     loc_link);
		}
	}

	/* Embark armies and fighters. */
	read_embark(game.user_obj[TRANSPORT], ARMY);
	read_embark(game.user_obj[CARRIER], FIGHTER);
	read_embark(game.comp_obj[TRANSPORT], ARMY);
	read_embark(game.comp_obj[CARRIER], FIGHTER);

	(void)fclose(f);
	kill_display(); /* what we had is no longer good */
	topmsg(3, "Game restored from save file.");
	return (true);

restore_cleanup:
	(void)fclose(f);
	return (false);

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

void read_embark(piece_info_t *list, int piece_type) {
	void inconsistent(void);

	piece_info_t *ship;
	piece_info_t *obj;

	for (ship = list; ship != NULL; ship = ship->piece_link.next) {
		int count = ship->count; /* get # of pieces we need */
		if (count < 0) {
			inconsistent();
		}
		ship->count = 0; /* nothing on board yet */
		for (obj = game.real_map[ship->loc].objp; obj && count;
		     obj = obj->loc_link.next) {
			if (obj->ship == NULL && obj->type == piece_type) {
				embark(ship, obj);
				count -= 1;
			}
		}
		if (count) {
			inconsistent();
		}
	}
}

void inconsistent(void) {
	(void)printf("saved game is inconsistent.  Please remove it.\n");
	exit(1);
}

/*
Write a buffer to a file.  If we cannot write everything, return false.
Also, tell the user why the write did not work if it didn't.
*/

bool xwrite(FILE *f, const char *buf, unsigned int size) {
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

bool xread(FILE *f, char *buf, unsigned int size) {
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
static char mapbuf[MAP_SIZE];

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
				mapbuf[i] = tolower(piece_attr[p->type].sname);
			}
		}
	}
	wbuf(mapbuf);
	(void)fclose(f);
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
		if (fread((char *)mapbuf, 1, sizeof(mapbuf), f) !=
		    sizeof(mapbuf)) {
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

void stat_display(const char *mbuf, int round) {
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
