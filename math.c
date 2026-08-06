/*
 * SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
 * SPDX-License-Identifier: GPL-2.0+
 *
 * See the file COPYING, distributed with empire, for restriction
 * and warranty information.
 */

/*
math.c -- various mathematical routines.

This file contains routines used to create random integers.  The
initialization routine 'rndini' should be called at program startup.
The flavors of random integers that can be generated are:

    irand(n) -- returns a random integer in the range 0..n-1
    rndint(a,b) -- returns a random integer in the range a..b

Other routines include:

    dist (a, b) -- returns the straight-line distance between two locations.
*/

#include "empire.h"
#include "extern.h"
#include <stdlib.h>
#include <time.h>

void rndini(void) { srand((unsigned)(time(0) & 0xFFFF)); }

long irand(long high) {
	if (high < 2) {
		return (0);
	}
	return (rand() % high);
}

#ifdef __UNUSED__
int rndint(int minp, int maxp) {
	int size;

	size = maxp - minp + 1;
	return ((rand() % size) + minp);
}
#endif

/*
Return the distance between two locations.  This is the maximum of the
absolute differences between their coordinates.  On a toroidal map, use
the shorter distance across each seam.
*/

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))

int dist(loc_t a, loc_t b) {
	int ax, ay, bx, by;
	int row_dist, col_dist;

	ax = loc_row(a);
	ay = loc_col(a);
	bx = loc_row(b);
	by = loc_col(b);

	row_dist = ABS(ax - bx);
	col_dist = ABS(ay - by);
	if (game.tflag) {
		row_dist = MIN(row_dist, MAP_HEIGHT - 2 - row_dist);
		col_dist = MIN(col_dist, MAP_WIDTH - 2 - col_dist);
	}

	return (MAX(row_dist, col_dist));
}

/*
Find the square root of an integer.  We actually return the floor
of the square root using Newton's method.
*/

int isqrt(int n) {
	int guess;

	ASSERT(n >= 0); /* can't take sqrt of negative number */

	if (n <= 1)
		return (n); /* do easy cases and avoid div by zero */

	guess = 2; /* gotta start somewhere */
	guess = (guess + n / guess) / 2;
	guess = (guess + n / guess) / 2;
	guess = (guess + n / guess) / 2;
	guess = (guess + n / guess) / 2;
	guess = (guess + n / guess) / 2;

	if (guess * guess > n) {
		guess -= 1; /* take floor */
	}
	return (guess);
}
