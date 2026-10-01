/* lab_audio.c -- the game's SoundManager (a SoundPool), played through audout.
 *
 * The engine plays sounds by id through JNI: playSound(source, id, gain,
 * pitch), stopSound(source, id), updateGainAndPitch(source, id, gain, pitch)
 * (the rolling ball, the fan, the alarm: loops whose volume and pitch follow
 * the game), stopAllSounds, disable/enableSounds. SoundManager.java turns
 * the ids into res/raw/<name>.ogg samples of a SoundPool (24 streams) with these
 * rules, kept exactly:
 *   - an id is ignored if it played less than 150 ms ago;
 *   - sound effects off (registry setting-sound-effects = 0), or disabled
 *     (disableSounds, except the menu click): nothing plays, but a paused
 *     loop of that sample takes the new gain/pitch;
 *   - only LOOPING sounds are tracked (source + sample): playing one that is
 *     already on just sets its gain and pitch; one-shots are fire-and-forget
 *     (the Java's "24 tracked: drop a non-looping one" never finds one);
 *   - disableSounds stops everything, keeping the loops to restart on
 *     enableSounds; updateGainAndPitch clamps gain 0..1 and pitch 0.5..2.
 * The SoundPool itself is a 24-voice mixer here: priority 100 for loops, 0
 * for one-shots (a full pool drops the oldest lowest-priority voice), pitch
 * as a playback rate (linear interpolation), gain on both channels.
 *
 * The samples are decoded from the APK once at start-up (stb_vorbis, on a
 * thread: the splash shows meanwhile). The mixer renders 1024-frame buffers
 * of 48 kHz stereo on the runtime's audout pump (rt_audout.c), which queues
 * three ahead. MIT.
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "lab.h"
#include "rt_audout.h"
#include "util.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.inc"

/* ============================================================== samples */
/* res/raw/<name>.ogg, as SoundManager.e() loads them */
enum {
  S_NONE = 0, S_HOLE_WOOD, S_COLL_METAL, S_BALL_BALL, S_CANNONBALL_COLL, S_CLOWNBALL_COLL, S_BUMPER,
  S_BUMPER_TRI, S_AWARD, S_BUTTON_MENU, S_BUTTON_INGAME, S_CHECKPOINT, S_POPUP, S_DOOR_STOP,
  S_DOOR_MOVING, S_GOAL, S_CANNON_FIRE, S_EXPLOSION, S_COLL_WOOD, S_CLONING, S_RELAY_FLICKER_SHORT,
  S_RELAY_FLICKER, S_RELAY, S_LASER_BUZZ, S_COLL_FAN, S_FAN_SPIN, S_WARNINGLIGHT_ALARM,
  S_MERRY_GO_ROUND, S_ROLL_WOOD, S_COUNT
};
static const char *const k_sample_file[S_COUNT] = {
    NULL, "hole_wood", "ball_collision_metal", "ball_ball_collision", "cannonball_collision",
    "clownball_collision", "bumper", "bumper_tri", "award", "button_menu", "button_ingame",
    "checkpoint", "popup", "door_stop", "door_moving", "goal", "cannon_fire", "explosion",
    "ball_collision_wood", "cloning", "relay_flicker_short", "relay_flicker", "relay", "laser_buzz",
    "ball_collision_fan", "fan_spin", "warninglight_alarm", "merry_go_round_squeak", "ball_roll_wood",
};

typedef struct {
  int16_t *pcm; /* interleaved */
  int frames, ch, rate;
} Sample;
static Sample g_samples[S_COUNT];
static volatile int g_ready;

/* id -> sample (SoundManager.f[]), and which ids loop (g[]) */
static uint8_t g_sample_of[SND_COUNT];
static uint8_t g_loops[SND_COUNT];

static void build_tables(void) {
#define M(id, s) g_sample_of[id] = s
  M(SND_HOLE, S_HOLE_WOOD);
  M(SND_BALLENLARGER_BOUNCE, S_COLL_METAL);
  M(SND_BALL_BALL_COLLISION, S_BALL_BALL);
  M(SND_BALL_CANNONBALL_COLLISION, S_CANNONBALL_COLL);
  M(SND_BALL_CLOWNBALL_COLLISION, S_CLOWNBALL_COLL);
  M(SND_BUMPER_ROUND, S_BUMPER);
  M(SND_BUMPER_TRI, S_BUMPER_TRI);
  M(SND_AWARD, S_AWARD);
  M(SND_MENU_BUTTON_CLICK, S_BUTTON_MENU);
  M(SND_BUTTON, S_BUTTON_INGAME);
  M(SND_CHECKPOINT, S_CHECKPOINT);
  M(SND_POPUP_HIDE, S_POPUP);
  M(SND_POPUP_SHOW, S_POPUP);
  M(SND_DOOR_HIT, S_COLL_METAL);
  M(SND_DOOR_STOP, S_DOOR_STOP);
  M(SND_DOOR_MOVING, S_DOOR_MOVING);
  M(SND_GOAL, S_GOAL);
  M(SND_CANNON_FIRE, S_CANNON_FIRE);
  M(SND_CANNON_BALL_EXPLOSION, S_EXPLOSION);
  M(SND_CANNON_BOUNCE, S_COLL_WOOD);
  M(SND_CLONING_STARTED, S_CLONING);
  M(SND_CLONING_ENABLED, S_RELAY_FLICKER_SHORT);
  M(SND_LASER_BEAM_ON, S_RELAY_FLICKER);
  M(SND_LASER_BEAM_OFF, S_RELAY);
  M(SND_LASER_BEAM_BUZZ, S_LASER_BUZZ);
  M(SND_LASER_TOWER_BOUNCE, S_COLL_METAL);
  M(SND_MAGNET_BOUNCE, S_COLL_METAL);
  M(SND_MOVINGWALL_BOUNCE, S_COLL_WOOD);
  M(SND_FAN_HIT_BASE, S_COLL_METAL);
  M(SND_FAN_HIT_BLADE, S_COLL_FAN);
  M(SND_FAN_SPIN, S_FAN_SPIN);
  M(SND_WARNINGLIGHT_ALARM, S_WARNINGLIGHT_ALARM);
  M(SND_WARNINGLIGHT_BOUNCE, S_COLL_WOOD);
  M(SND_MERRYGOROUND_SPIN, S_MERRY_GO_ROUND);
  M(SND_MERRYGOROUND_BOUNCE, S_COLL_METAL);
  M(SND_BALL_ROLL, S_ROLL_WOOD);
  M(SND_WALL, S_COLL_WOOD);
  M(SND_BUMPER_ROUND_CANNONBALL, S_BUMPER);
  M(SND_CANNON_BOUNCE_CANNONBALL, S_COLL_WOOD);
  M(SND_WALL_CANNONBALL, S_COLL_WOOD);
  M(SND_MOVINGWALL_BOUNCE_CANNONBALL, S_COLL_WOOD);
  M(SND_BUMPER_TRI_CANNONBALL, S_BUMPER_TRI);
  M(SND_MERRYGOROUND_BOUNCE_CANNONBALL, S_COLL_METAL);
  M(SND_MAGNET_BOUNCE_CANNONBALL, S_COLL_METAL);
  M(SND_WARNINGLIGHT_BOUNCE_CANNONBALL, S_COLL_WOOD);
  M(SND_LASER_TOWER_BOUNCE_CANNONBALL, S_COLL_METAL);
  M(SND_FAN_HIT_BASE_CANNONBALL, S_COLL_METAL);
  M(SND_DOOR_HIT_CANNONBALL, S_COLL_METAL);
  M(SND_BALLENLARGER_BOUNCE_CANNONBALL, S_COLL_METAL);
#undef M
  g_loops[SND_DOOR_MOVING] = g_loops[SND_LASER_BEAM_BUZZ] = g_loops[SND_FAN_SPIN] = 1;
  g_loops[SND_WARNINGLIGHT_ALARM] = g_loops[SND_MERRYGOROUND_SPIN] = g_loops[SND_BALL_ROLL] = 1;
}

static void decode_all(void *arg) {
  u64 t0 = armGetSystemTick();
  int ok = 0;
  size_t bytes = 0;
  for (int s = 1; s < S_COUNT; s++) {
    char name[96];
    snprintf(name, sizeof name, "res/raw/%s.ogg", k_sample_file[s]);
    size_t len = 0;
    uint8_t *ogg = lab_apk_read(name, &len);
    if (!ogg) {
      debugPrintf("[audio] %s: not in the APK\n", name);
      continue;
    }
    int ch = 0, rate = 0;
    short *pcm = NULL;
    int frames = stb_vorbis_decode_memory(ogg, (int)len, &ch, &rate, &pcm);
    free(ogg);
    if (frames <= 0 || !pcm || (ch != 1 && ch != 2) || rate < 4000) {
      debugPrintf("[audio] %s: not a sound this can decode\n", name);
      free(pcm);
      continue;
    }
    g_samples[s] = (Sample){pcm, frames, ch, rate};
    bytes += (size_t)frames * (size_t)ch * 2;
    ok++;
  }
  __atomic_store_n(&g_ready, 1, __ATOMIC_RELEASE); /* the samples above, seen whole */
  debugPrintf("[audio] %d of %d sounds decoded (%u KB) in %llu ms\n", ok, S_COUNT - 1,
              (unsigned)(bytes >> 10), (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

int lab_audio_ready(void) { return __atomic_load_n(&g_ready, __ATOMIC_ACQUIRE); }

/* ============================================================ the pool */
#define NVOICE 24
typedef struct {
  int on;
  int sample;
  int loop;
  int prio;
  float gain, rate;
  double pos;       /* frames into the sample */
  uint32_t serial;  /* start order: the oldest is dropped first */
} Voice;
static Voice g_v[NVOICE];
static uint32_t g_serial;

/* SoundManager's own bookkeeping */
typedef struct {
  int sample, source, voice; /* voice: index + 1 in g_v, 0 = not playing */
  uint32_t serial;           /* ... while g_v[voice - 1] still has this serial */
  float gain, rate;
} Track;
#define NTRACK 64
static Track g_active[NTRACK]; /* the Java's list j: loops that play */
static int g_nactive;
static Track g_pending[NTRACK]; /* list k: loops stopped by disableSounds */
static int g_npending;
static u64 g_last_play[SND_COUNT];
static int g_disabled; /* l */
static int g_muted;    /* m */
static Mutex g_lock;
uint32_t lab_audio_mixes(void) { return (uint32_t)rt_audout_pump_blocks(); }
void lab_audio_pause(int paused) { rt_audout_pause(paused); } /* in the background: the voices wait where they are */

static int effects_on(void) { return lab_reg_get_int("setting-sound-effects", 1) != 0; }

/* SoundPool.play: a voice (the lowest-priority, oldest one is dropped when
 * all 24 play) -> its index + 1 */
static int pool_play(int sample, float gain, int loop, float rate, int prio) {
  if (sample <= 0 || sample >= S_COUNT || !g_samples[sample].pcm)
    return 0;
  int slot = -1;
  for (int i = 0; i < NVOICE; i++)
    if (!g_v[i].on) {
      slot = i;
      break;
    }
  if (slot < 0) {
    for (int i = 0; i < NVOICE; i++)
      if (g_v[i].prio <= prio && (slot < 0 || g_v[i].prio < g_v[slot].prio ||
                                  (g_v[i].prio == g_v[slot].prio && g_v[i].serial < g_v[slot].serial)))
        slot = i;
    if (slot < 0)
      return 0; /* all busier: SoundPool refuses */
  }
  g_v[slot] = (Voice){1, sample, loop, prio, gain, rate, 0.0, ++g_serial};
  return slot + 1;
}

/* the track's voice, if it is still the one it started (a full pool may
 * have given the slot to another sound) */
static int track_voice(const Track *t) {
  if (t->voice > 0 && t->voice <= NVOICE && g_v[t->voice - 1].on && g_v[t->voice - 1].serial == t->serial)
    return t->voice;
  return 0;
}

static uint32_t voice_serial(int voice) { return voice > 0 && voice <= NVOICE ? g_v[voice - 1].serial : 0; }

static void pool_stop(int voice) {
  if (voice > 0 && voice <= NVOICE)
    g_v[voice - 1].on = 0;
}

static void pool_set(int voice, float gain, float rate) {
  if (voice > 0 && voice <= NVOICE && g_v[voice - 1].on) {
    g_v[voice - 1].gain = gain;
    g_v[voice - 1].rate = rate;
  }
}

static int valid_id(int id) { return id >= 0 && id < SND_COUNT; }

/* SoundPool's own limits: volume 0..1, rate 0.5..2 */
static float clamp_gain(float g) { return !(g > 0) ? 0 : g > 1 ? 1 : g; }
static float clamp_rate(float r) { return !(r >= 0.5f) ? 0.5f : r > 2.0f ? 2.0f : r; }

void lab_audio_play(int source, int id, float gain, float pitch) {
  if (!valid_id(id) || !lab_audio_ready())
    return;
  gain = clamp_gain(gain);
  pitch = clamp_rate(pitch);
  u64 now = armTicksToNs(armGetSystemTick());
  mutexLock(&g_lock);
  if (now - g_last_play[id] < 150000000ull) {
    mutexUnlock(&g_lock);
    return;
  }
  int sample = g_sample_of[id];
  if (!effects_on() || g_muted || (g_disabled && id != SND_MENU_BUTTON_CLICK)) {
    for (int i = 0; i < g_npending; i++)
      if (g_pending[i].sample == sample) {
        g_pending[i].gain = gain;
        g_pending[i].rate = pitch;
      }
    mutexUnlock(&g_lock);
    return;
  }
  for (int i = 0; i < g_nactive; i++)
    if (g_active[i].source == source && g_active[i].sample == sample) {
      pool_set(track_voice(&g_active[i]), gain, pitch);
      mutexUnlock(&g_lock);
      return;
    }
  int loop = g_loops[id];
  int v = pool_play(sample, gain, loop, pitch, loop ? 100 : 0);
  g_last_play[id] = now;
  if (loop && g_nactive < NTRACK)
    g_active[g_nactive++] = (Track){sample, source, v, voice_serial(v), gain, pitch};
  mutexUnlock(&g_lock);
}

void lab_audio_stop(int source, int id) {
  if (!valid_id(id))
    return;
  mutexLock(&g_lock);
  int sample = g_sample_of[id];
  for (int i = 0; i < g_nactive; i++)
    if (g_active[i].source == source && g_active[i].sample == sample) {
      pool_stop(track_voice(&g_active[i]));
      memmove(&g_active[i], &g_active[i + 1], sizeof g_active[0] * (size_t)(g_nactive - i - 1));
      g_nactive--;
      break;
    }
  mutexUnlock(&g_lock);
}

void lab_audio_update(int source, int id, float gain, float pitch) {
  if (!valid_id(id))
    return;
  gain = clamp_gain(gain);
  pitch = clamp_rate(pitch);
  mutexLock(&g_lock);
  int sample = g_sample_of[id];
  for (int i = 0; i < g_nactive; i++)
    if (g_active[i].source == source && g_active[i].sample == sample) {
      g_active[i].gain = gain;
      g_active[i].rate = pitch;
      int v = track_voice(&g_active[i]);
      if (v)
        pool_set(v, gain, pitch);
      else {
        g_active[i].voice = pool_play(sample, gain, 1, pitch, 100);
        g_active[i].serial = voice_serial(g_active[i].voice);
      }
      break;
    }
  mutexUnlock(&g_lock);
}

void lab_audio_stop_all(void) {
  mutexLock(&g_lock);
  for (int i = 0; i < g_nactive; i++)
    pool_stop(track_voice(&g_active[i]));
  g_nactive = g_npending = 0;
  mutexUnlock(&g_lock);
}

void lab_audio_enable(int on) {
  mutexLock(&g_lock);
  if (!on) {
    g_disabled = 1;
    for (int i = 0; i < g_nactive; i++) {
      pool_stop(track_voice(&g_active[i]));
      if (g_npending < NTRACK) {
        g_pending[g_npending] = g_active[i];
        g_pending[g_npending++].voice = 0;
      }
    }
    g_nactive = 0;
  } else {
    g_disabled = 0;
    if (effects_on()) {
      for (int i = 0; i < g_npending && g_nactive < NTRACK; i++) {
        Track t = g_pending[i];
        t.voice = pool_play(t.sample, t.gain, 1, t.rate, 100);
        t.serial = voice_serial(t.voice);
        g_active[g_nactive++] = t;
      }
      g_npending = 0;
    }
  }
  mutexUnlock(&g_lock);
}

void lab_audio_mute_game(int on) {
  mutexLock(&g_lock);
  g_muted = on;
  mutexUnlock(&g_lock);
}

void lab_audio_click(void) { lab_audio_play(0, SND_MENU_BUTTON_CLICK, 1.0f, 1.0f); }

/* ============================================================ the mixer */
static int g_thread_up;

static void mix(int16_t *out, int frames, void *ud) {
  (void)ud;
  static int32_t acc[RT_AUDOUT_FRAMES * 2];
  memset(acc, 0, sizeof acc);
  mutexLock(&g_lock);
  for (int k = 0; k < NVOICE; k++) {
    Voice *v = &g_v[k];
    if (!v->on)
      continue;
    const Sample *s = &g_samples[v->sample];
    const double step = (double)v->rate * (double)s->rate / (double)rt_audout_rate(); /* rate is 0.5..2 */
    const int g = (int)(v->gain * 256.0f);
    double pos = v->pos;
    for (int f = 0; f < frames; f++) {
      if (pos >= (double)s->frames) {
        if (!v->loop) {
          v->on = 0;
          break;
        }
        pos = fmod(pos, (double)s->frames);
      }
      int i0 = (int)pos;
      int i1 = i0 + 1 < s->frames ? i0 + 1 : (v->loop ? 0 : i0);
      int t = (int)((pos - (double)i0) * 256.0);
      int l, r;
      if (s->ch == 1) {
        l = r = s->pcm[i0] + (((s->pcm[i1] - s->pcm[i0]) * t) >> 8);
      } else {
        l = s->pcm[i0 * 2] + (((s->pcm[i1 * 2] - s->pcm[i0 * 2]) * t) >> 8);
        r = s->pcm[i0 * 2 + 1] + (((s->pcm[i1 * 2 + 1] - s->pcm[i0 * 2 + 1]) * t) >> 8);
      }
      acc[f * 2] += (l * g) >> 8;
      acc[f * 2 + 1] += (r * g) >> 8;
      pos += step;
    }
    v->pos = pos;
  }
  mutexUnlock(&g_lock);
  for (int i = 0; i < frames * 2; i++) {
    int32_t x = acc[i];
    out[i] = (int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
  }
}

void lab_audio_init(void) {
  build_tables();
  static Thread dec;
  if (R_SUCCEEDED(threadCreate(&dec, decode_all, NULL, NULL, 0x20000, 0x2C, -2)))
    threadStart(&dec);
  else
    decode_all(NULL);
  /* priority 0x28 on core 2: above the game's (main) thread, so the mixer
   * keeps up */
  if (rt_audout_pump_start(mix, NULL, 0x28, 2) != 0)
    return;
  g_thread_up = 1;
}
