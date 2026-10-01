/* lab_ghost.c -- the ghost ball: your best run of a level, replayed as a
 * faded ball you do not collide with; in local play, the other player's
 * ball on your board.
 *
 * The engine has the iPad's ghost code (GhostBallRecorder / GhostBallPlayer,
 * temp/ and ghosts/<pack>.<nn> files), but nothing in the Android library
 * drives it: nothing makes the ghost manager (+0x211fe), sets it up for a
 * level (+0x21b14) or starts a recording (+0x21344). What is left is its
 * bookkeeping -- a run that beats the ghost's time is written down as
 * TIMEG_<pack>_<n> (+0x3f150) -- and its drawing: the level's balls
 * renderer draws GhostBalls, when a level has any (lab_game_eng_ghost). So
 * the port does the rest:
 *
 *   - a run's clock is the engine's own (lab_game_eng_run: it runs from
 *     the end of the 3-2-1, not while paused; a hole does not stop it), the
 *     time its best times and TIMEG_ are of; it goes back to 0 as the level
 *     starts again;
 *   - each frame it moves on with the ball in play, the ball's position is
 *     kept at that time (lab_game_eng_ball); the moment it goes out of play
 *     (into a hole, the goal) too, so the ghost vanishes there as the ball
 *     did, until it is back;
 *   - the engine's TIMEG_ of this pack and level (a new ghost time; the
 *     setting on) saves the run: <root>/data/ghosts/<pack>.<nn>.lgh;
 *   - with a ghost for the level, the engine's ghost ball is put where it
 *     was at the run's time (it waits at the start through the 3-2-1).
 *
 * Local play: each board shows the other player's ball, live. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "lab.h"
#include "util.h"

const char *dcr_game_root(void); /* the runtime (dcr_path.c) */

/* ---- the runs (single player) */
/* a point of a run: where the ball was at its time t; x NAN from where it
 * went out of play (into a hole, the goal) until it was back ("LGH2";
 * "LGH1", the first files, have no such points: they stop, and go on
 * where it came back) */
typedef struct {
  float t, x, y;
} Sample;

#define MAX_SAMPLES 36000 /* ten minutes at 60 a second */

static struct {
  char id[48];
  int level;
  float t, dt;         /* the run's time (the engine's), s; its last step */
  Sample *rec;
  int nrec;
  Sample *ghost;       /* the level's ghost, if it has one */
  int nghost;
} R = {.level = -1};

static void ghost_path(char *out, size_t cap, const char *id, int level) {
  snprintf(out, cap, "%s/data/ghosts/%s.%02d.lgh", dcr_game_root(), id, level);
}

static void load_ghost(void) {
  free(R.ghost);
  R.ghost = NULL, R.nghost = 0;
  if (lab_reg_get_int("setting-ghostball", 1) != 1)
    return;
  char path[512];
  ghost_path(path, sizeof path, R.id, R.level);
  FILE *f = fopen(path, "rb");
  if (!f)
    return;
  char magic[4];
  int n = 0;
  if (fread(magic, 1, 4, f) == 4 && (!memcmp(magic, "LGH1", 4) || !memcmp(magic, "LGH2", 4)) &&
      fread(&n, 4, 1, f) == 1 && n > 1 &&
      n <= MAX_SAMPLES && (R.ghost = malloc(sizeof(Sample) * (size_t)n)) &&
      fread(R.ghost, sizeof(Sample), (size_t)n, f) == (size_t)n) {
    R.nghost = n;
    debugPrintf("[ghost] %s level %d: its ghost, %d points, %.2f s\n", R.id, R.level + 1, n,
                (double)R.ghost[n - 1].t);
  } else {
    free(R.ghost);
    R.ghost = NULL;
  }
  fclose(f);
}

/* the engine's TIMEG_<pack>_<n>: a new ghost time -- this run, if it is its */
void lab_ghost_on_best(const char *key) {
  if (!key || strncmp(key, "TIMEG_", 6) || !R.nrec)
    return;
  char want[80];
  snprintf(want, sizeof want, "TIMEG_%s_%d", R.id, R.level);
  if (strcmp(key, want))
    return;
  char dir[512], path[512];
  snprintf(dir, sizeof dir, "%s/data/ghosts", dcr_game_root());
  mkdir(dir, 0777);
  ghost_path(path, sizeof path, R.id, R.level);
  FILE *f = fopen(path, "wb");
  if (!f) {
    debugPrintf("[ghost] %s: could not be written\n", path);
    return;
  }
  int n = R.nrec;
  fwrite("LGH2", 1, 4, f);
  fwrite(&n, 4, 1, f);
  fwrite(R.rec, sizeof(Sample), (size_t)n, f);
  fclose(f);
  debugPrintf("[ghost] %s level %d: your run kept as its ghost (%d points, %.2f s)\n", R.id, R.level + 1, n,
              (double)R.t);
}

/* the ghost at time t (between its points) */
static int ghost_at(float t, float *x, float *y) {
  if (R.nghost < 2 || t > R.ghost[R.nghost - 1].t)
    return -1; /* not recorded that far: it is in the goal */
  int lo = 0, hi = R.nghost - 1;
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if (R.ghost[mid].t <= t)
      lo = mid;
    else
      hi = mid;
  }
  const Sample *a = &R.ghost[lo], *b = &R.ghost[hi];
  if (isnan(a->x) || b->t - a->t > 0.25f)
    return -1; /* out of play (fallen into a hole): not seen until it is back */
  if (isnan(b->x)) { /* its last frame in play */
    *x = a->x, *y = a->y;
    return 0;
  }
  float k = b->t > a->t ? (t - a->t) / (b->t - a->t) : 0;
  k = k < 0 ? 0 : k > 1 ? 1 : k;
  *x = a->x + (b->x - a->x) * k, *y = a->y + (b->y - a->y) * k;
  return 0;
}

void lab_ghost_frame(int eng) {
  float secs;
  int level;
  char id[48];
  if (lab_game_eng_run(eng, &secs, &level, id, sizeof id) != 0)
    return;
  float x = 0, y = 0;
  int live = 0, have = lab_game_eng_ball(eng, &x, &y, &live) == 0;
  if (level != R.level || strcmp(id, R.id)) {
    R.level = level, R.t = secs, R.dt = 0, R.nrec = 0;
    snprintf(R.id, sizeof R.id, "%s", id);
    load_ghost(); /* shown at the start through the 3-2-1 */
  }
  if (secs + 0.001f < R.t) { /* the engine's clock back to 0: the level again */
    R.nrec = 0, R.dt = 0;
    debugPrintf("[ghost] %s level %d: a run begins\n", R.id, R.level + 1);
    load_ghost(); /* a new best since the last run */
  }
  if (secs > R.t) { /* played: the ball where it is at this time */
    R.dt = secs - R.t;
    if (!R.rec)
      R.rec = malloc(sizeof(Sample) * MAX_SAMPLES);
    /* only in play: out of it (in the goal, falling in a hole) the ball
     * reads as where it starts */
    if (have && live && R.rec && R.nrec < MAX_SAMPLES)
      R.rec[R.nrec++] = (Sample){secs, x, y};
    else if (!live && R.nrec && !isnan(R.rec[R.nrec - 1].x) && R.nrec < MAX_SAMPLES) {
      R.rec[R.nrec++] = (Sample){secs, NAN, NAN}; /* it went out of play here */
      debugPrintf("[ghost] %s level %d: the ball out of play at %.2f s (a hole, the goal)\n", R.id, R.level + 1,
                  (double)secs);
    }
  } else {
    R.dt = 0; /* the 3-2-1, paused, its end */
  }
  R.t = secs;
  /* the engine draws it next frame: where it is then */
  float gx = 0, gy = 0;
  int shown = R.nghost && lab_game_eng_popup(eng) != 2 && ghost_at(R.t + R.dt, &gx, &gy) == 0;
  lab_game_eng_ghost(eng, shown, gx, gy);
}

/* ---- local play: the other player's ball on this board */
void lab_ghost_mirror(int board_eng, int other_eng, int shown) {
  float x = 0, y = 0;
  int live = 0;
  if (lab_game_eng_ball(other_eng, &x, &y, &live) != 0 || !live || lab_game_eng_popup(other_eng) == 2)
    shown = 0;
  lab_game_eng_ghost(board_eng, shown, x, y);
}
