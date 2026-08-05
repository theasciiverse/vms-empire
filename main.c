/*
 * SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
 * SPDX-License-Identifier: GPL-2.0+
 *
 * See the file COPYING, distributed with empire, for restriction
 * and warranty information.
 */

/*
main.c -- parse command line for empire

options:

    -w water: percentage of map that is water.  Must be in the range
              10..90.  Default is 70.

    -s smooth: amount of smoothing performed to generate map.  Must
               be a nonnegative integer.  Default is 5.

    -d delay:  number of milliseconds to delay between output.
               default is 2000 (2 seconds).

    -S saveinterval: sets turn interval between saves.
               default is 10

    -g geometry: map size as WIDTHxHEIGHT. Default is 100x60.

    -t: enable the feature controlled by tflag. Disabled by default.
*/

#include "empire.h"
#include "extern.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define OPTFLAGS "w:s:d:S:f:g:t"

static int parse_geometry(const char *geometry, int *width, int *height) {
	char *end;
	long w, h;

	w = strtol(geometry, &end, 10);
	if ((*end != 'x' && *end != 'X') || end == geometry) {
		return 0;
	}
	h = strtol(end + 1, &end, 10);
	if (*end != '\0') {
		return 0;
	}
	if (w < 10 || w > MAX_MAP_DIMENSION || h < 10 ||
	    h > MAX_MAP_DIMENSION || w * h > MAX_MAP_SIZE ||
	    (100 * (w + h)) / 228 > MAX_NUM_CITY) {
		return 0;
	}
	*width = (int)w;
	*height = (int)h;
	return 1;
}

int main(int argc, char *argv[]) {
	int c;
	extern char *optarg;
	extern int optind;
	int errflg = 0;
	int wflg, sflg, dflg, Sflg;
	int map_width, map_height;
	int land;

	wflg = 70; /* set defaults */
	sflg = 5;
	dflg = 2000;
	Sflg = 10;
	map_width = DEFAULT_MAP_WIDTH;
	map_height = DEFAULT_MAP_HEIGHT;
	game.savefile = "empire.sav";
	game.tflag = false;

	/*
	 * extract command line options
	 */

	while ((c = getopt(argc, argv, OPTFLAGS)) != EOF) {
		switch (c) {
		case 'w':
			wflg = atoi(optarg);
			break;
		case 's':
			sflg = atoi(optarg);
			break;
		case 'd':
			dflg = atoi(optarg);
			break;
		case 'S':
			Sflg = atoi(optarg);
			break;
		case 'f':
			game.savefile = optarg;
			break;
		case 'g':
			if (!parse_geometry(optarg, &map_width, &map_height)) {
				errflg++;
			}
			break;
		case 't':
			game.tflag = true;
			break;
		case '?': /* illegal option? */
			errflg++;
			break;
		}
	}
	if (errflg || (argc - optind) != 0) {
		(void)printf("empire: usage: empire [-w water] [-s smooth] [-d "
		             "delay] [-S saveinterval] [-f savefile] "
		             "[-g WIDTHxHEIGHT] [-t]\n");
		exit(1);
	}
	configure_geometry(map_width, map_height);

	if (wflg < 10 || wflg > 90) {
		(void)printf(
		    "empire: -w argument must be in the range 0..90.\n");
		exit(1);
	}
	if (sflg < 0) {
		(void)printf(
		    "empire: -s argument must be greater or equal to zero.\n");
		exit(1);
	}

	if (dflg < 0 || dflg > 30000) {
		(void)printf(
		    "empire: -d argument must be in the range 0..30000.\n");
		exit(1);
	}

	game.SMOOTH = sflg;
	game.WATER_RATIO = wflg;
	game.delay_time = dflg;
	game.save_interval = Sflg;

	/* compute min distance between cities */
	land = MAP_SIZE * (100 - game.WATER_RATIO) / 100; /* available land */
	land /= NUM_CITY;                                 /* land per city */
	game.MIN_CITY_DIST = isqrt(land); /* distance between cities */

	empire(); /* call main routine */
	return (0);
}
