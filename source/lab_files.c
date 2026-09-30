/* lab_files.c -- the engine's files dir, as the Java filled it.
 *
 * FileSystemUtil.setResourcePath(getFilesDir()) is where the engine reads
 * EVERYTHING: textures/, meshes/, awards/awards.xml, and the level packs as
 * zipfiles/<levelid> (minizip). On a phone, the first LevelsDB.e() (dk.java)
 * put them there:
 *   - every asset (but levels/..., and the top-level images/, webkit/,
 *     sounds/ folders the framework adds) copied to files/<path>, unless it
 *     is there already;
 *   - each *.zip asset (officiallevels/1.zip, 2.zip: zips of level-pack
 *     zips) unpacked into files/zipfiles/, once (imported_zips); 2.zip
 *     replaces two of 1.zip's packs with newer copies;
 *   - each unpacked pack's info.xml made a row of the level table
 *     (preloaded: an official pack).
 * The same here, on the first launch and whenever game.apk's assets change
 * (a stamp of their CRCs). The engine's own writes (ghosts/, temp/,
 * zipfiles/tmp/) go to the same dir.
 *
 * Level packs from the server (lab_online.c, the Download and Create
 * screens) are stored the same way, as zipfiles/<levelid>. Level packs on the
 * SD card: <root>/levelpacks/<name>.zip, each a pack as the server sent it
 * (info.xml + level<N>.xml), or a zip of such packs, is copied to
 * zipfiles/<levelid> and listed as downloaded.
 *
 * iPad packs (576 x 768 boards, <labyrinth size="1">, which the iPad engine
 * copy plays: lab_loader.c) live in data/files-ipad/ instead, the iPad
 * engine's resource dir, which falls back to files/ for everything it does
 * not have (dcr_path.c). From the player's .ipa (Labyrinth 2 HD) come the
 * official iPad packs (officiallevelsipad/1..3, zips of pack zips, as the
 * APK's) and the iPad's bigger floors (textures-ipad/theme-*\/floor-
 * 1024x1024.jpg, turned into the PNG the engine asks for). MIT.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "lab.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */
void dcr_dircache_forget(void);   /* dcr_dircache.c */

static char g_files[300], g_files_ipad[300];
static char g_sdpacks[300];

const char *lab_files_ipad_dir(void) {
  if (!g_files_ipad[0])
    snprintf(g_files_ipad, sizeof g_files_ipad, "%s/data/files-ipad", dcr_game_root());
  return g_files_ipad;
}

const char *lab_files_dir(void) {
  if (!g_files[0])
    snprintf(g_files, sizeof g_files, "%s/data/files", dcr_game_root());
  return g_files;
}

const char *lab_sd_packs_dir(void) {
  if (!g_sdpacks[0])
    snprintf(g_sdpacks, sizeof g_sdpacks, "%s/levelpacks", dcr_game_root());
  return g_sdpacks;
}

static void mkdirs_for(const char *path) {
  char p[1024];
  snprintf(p, sizeof p, "%s", path);
  for (char *s = p + 6; *s; s++) /* past "sdmc:/" */
    if (*s == '/') {
      *s = 0;
      mkdir(p, 0777);
      *s = '/';
    }
}

/* A file made or removed here, not through the bionic shims: the engine's
 * "is it there?" answers come from directory listings (dcr_dircache.c),
 * which only the shims forget. A pack zip written after the engine had
 * listed zipfiles/ was "missing" to it: its level loader opened nothing
 * and read through the null handle (hardware 2026-09-26/27: a pack looked
 * at, or downloaded, after an iPad game crashed the thumbnails at
 * +0x1bbf2). The levels' pictures (data/thumbs, never the engine's) need
 * not. */
static void changed(const char *path) {
  if (!strstr(path, "/data/thumbs/"))
    dcr_dircache_forget();
}

static int write_file(const char *path, const void *data, size_t len) {
  char tmp[1040];
  snprintf(tmp, sizeof tmp, "%s.part", path);
  mkdirs_for(path);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return -1;
  int ok = fwrite(data, 1, len, f) == len;
  if (fclose(f) != 0)
    ok = 0;
  if (ok) {
    unlink(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok)
    unlink(tmp);
  changed(path);
  return ok ? 0 : -1;
}

static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

/* setup work on screen: the progress bar (dcr_setup.c) */
void dcr_setup_progress(const char *what, int permille);

static void show_work(const char *what, int permille) {
  debugPrintf("[setup] %s\n", what);
  dcr_setup_progress(what, permille);
}

/* A level id as a file name: the server's ("E9W6TU56.06"), the official
 * ones ("Z0000000.01", "ZTUT..."); nothing that leaves zipfiles/ */
static int valid_id(const char *id) {
  size_t n = strlen(id);
  if (n == 0 || n > 60 || id[0] == '.' || strstr(id, ".."))
    return 0;
  for (size_t i = 0; i < n; i++) {
    char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' ||
          c == '-'))
      return 0;
  }
  return 1;
}

static void zip_path_for(char *out, size_t cap, const char *id, int ipad) {
  snprintf(out, cap, "%s/zipfiles/%s", ipad ? lab_files_ipad_dir() : lab_files_dir(), id);
}

/* a pack in the table: where its zip is */
static void zip_path(char *out, size_t cap, const char *id) {
  const LabPack *k = lab_levels_find(id);
  zip_path_for(out, cap, id, k && k->ipad);
}

/* a pack's zip in memory: an iPad board (level0.xml's <labyrinth size="1">)? */
static int zip_is_ipad(const void *zip, size_t len) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_mem(&z, zip, len, 0))
    return 0;
  int idx = mz_zip_reader_locate_file(&z, "level0.xml", NULL, 0);
  size_t xlen = 0;
  char *x = idx >= 0 ? mz_zip_reader_extract_to_heap(&z, (mz_uint)idx, &xlen, 0) : NULL;
  mz_zip_reader_end(&z);
  int ipad = 0;
  if (x) {
    size_t n = xlen < 400 ? xlen : 400;
    char head[401];
    memcpy(head, x, n);
    head[n] = 0;
    const char *t = strstr(head, "<labyrinth");
    const char *e = t ? strchr(t, '>') : NULL;
    const char *sz = t ? strstr(t, "size=\"") : NULL;
    ipad = sz && (!e || sz < e) && sz[6] == '1';
    mz_free(x);
  }
  return ipad;
}

/* info.xml of a pack's zip in memory, as a string (malloc), NULL if none */
static char *info_text(const void *zip, size_t len) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_mem(&z, zip, len, 0))
    return NULL;
  int idx = mz_zip_reader_locate_file(&z, "info.xml", NULL, 0);
  size_t xlen = 0;
  char *xml = idx >= 0 ? mz_zip_reader_extract_to_heap(&z, (mz_uint)idx, &xlen, 0) : NULL;
  mz_zip_reader_end(&z);
  if (!xml)
    return NULL;
  char *x = malloc(xlen + 1);
  if (x) {
    memcpy(x, xml, xlen);
    x[xlen] = 0;
  }
  mz_free(xml);
  return x;
}

/* info.xml of a pack's zip in memory, parsed (0) */
static int read_info(const void *zip, size_t len, LabPack *p, int *times, int cap, int *nt) {
  char *x = info_text(zip, len);
  if (!x)
    return -1;
  int rc = lab_levels_parse_info(x, p, times, cap, nt);
  free(x);
  return rc;
}

/* ---------------------------------------------- info.xml for the engine
 * The server writes info.xml's text as the player typed it: a pack called
 * "My First Pack<3" has a raw '<' in <levelname>, and the engine's TinyXML
 * cannot read the file. The phone's game hid every pack whose name had < >
 * or " (de.java) -- most of the best rated. Here the text of each
 * <tag>text</tag> line (what dh.java's own clean-up took: the text between
 * a line's first '>' and its last '</') gets its '<', '>' and bare '&'
 * escaped, which TinyXML turns back into the characters; the zip is
 * written again (entries stored) with that info.xml. */
static int bare_amp(const char *s) {
  static const char *const ent[] = {"&lt;", "&gt;", "&amp;", "&quot;", "&apos;"};
  for (unsigned i = 0; i < sizeof ent / sizeof ent[0]; i++)
    if (!strncmp(s, ent[i], strlen(ent[i])))
      return 0;
  if (s[1] == '#') {
    const char *q = s + 2;
    int hex = *q == 'x' || *q == 'X';
    if (hex)
      q++;
    const char *d = q;
    while (hex ? isxdigit((unsigned char)*q) : isdigit((unsigned char)*q))
      q++;
    if (q > d && *q == ';')
      return 0;
  }
  return 1;
}

/* a new string (malloc) with the lines' text escaped; *changed */
static char *xml_escape_text(const char *xml, int *changed) {
  size_t n = strlen(xml);
  char *out = malloc(n * 5 + 1), *o = out;
  *changed = 0;
  if (!out)
    return NULL;
  const char *line = xml;
  while (*line) {
    const char *eol = strchr(line, '\n');
    if (!eol)
      eol = line + strlen(line);
    /* <name>text</name> on the line: the text, from the first '>' to the
     * last "</" */
    const char *a = line;
    while (a < eol && (*a == ' ' || *a == '\t'))
      a++;
    const char *gt = a < eol && *a == '<' && a[1] != '/' && a[1] != '?' && a[1] != '!' ? memchr(a, '>', (size_t)(eol - a)) : NULL;
    const char *close = NULL;
    for (const char *s = gt ? gt + 1 : eol; gt && s + 1 < eol; s++)
      if (s[0] == '<' && s[1] == '/')
        close = s;
    int fix = 0;
    if (gt && close && gt[-1] != '/') {
      for (const char *s = gt + 1; s < close && !fix; s++)
        fix = *s == '<' || (*s == '&' && bare_amp(s));
    }
    if (!fix) {
      memcpy(o, line, (size_t)(eol - line));
      o += eol - line;
    } else {
      *changed = 1;
      memcpy(o, line, (size_t)(gt + 1 - line));
      o += gt + 1 - line;
      for (const char *s = gt + 1; s < close; s++) {
        if (*s == '<')
          memcpy(o, "&lt;", 4), o += 4;
        else if (*s == '>')
          memcpy(o, "&gt;", 4), o += 4;
        else if (*s == '&' && bare_amp(s))
          memcpy(o, "&amp;", 5), o += 5;
        else
          *o++ = *s;
      }
      memcpy(o, close, (size_t)(eol - close));
      o += eol - close;
    }
    if (*eol == '\n')
      *o++ = '\n';
    line = *eol ? eol + 1 : eol;
  }
  *o = 0;
  return out;
}

/* Would the engine take it: TinyXML's rules (tags nested, no '<' in text,
 * one root), and info.xml's shape -- <levelpack> and its children, each
 * only text (a name "<b>x</b>" would be an element where the engine wants
 * the text). */
static int xml_readable(const char *s) {
  char stack[32][48];
  int depth = 0, roots = 0;
  while (*s) {
    if (*s != '<') {
      s++;
      continue;
    }
    if (!strncmp(s, "<?", 2)) {
      const char *e = strstr(s, "?>");
      if (!e)
        return 0;
      s = e + 2;
    } else if (!strncmp(s, "<!--", 4)) {
      const char *e = strstr(s, "-->");
      if (!e)
        return 0;
      s = e + 3;
    } else if (!strncmp(s, "<![CDATA[", 9)) {
      const char *e = strstr(s, "]]>");
      if (!e)
        return 0;
      s = e + 3;
    } else if (!strncmp(s, "<!", 2)) {
      const char *e = strchr(s, '>');
      if (!e)
        return 0;
      s = e + 1;
    } else {
      int end = s[1] == '/';
      const char *nm = s + 1 + end, *q = nm;
      while (isalnum((unsigned char)*q) || *q == '_' || *q == '-' || *q == '.' || *q == ':')
        q++;
      size_t nl = (size_t)(q - nm);
      if (!nl || nl >= sizeof stack[0] || !(isalpha((unsigned char)*nm) || *nm == '_' || *nm == ':'))
        return 0;
      /* to the tag's '>', past quoted attribute values */
      char quote = 0;
      while (*q && (quote || *q != '>')) {
        if (quote && *q == quote)
          quote = 0;
        else if (!quote && (*q == '"' || *q == '\''))
          quote = *q;
        else if (!quote && *q == '<')
          return 0;
        q++;
      }
      if (!*q)
        return 0;
      int empty = q[-1] == '/';
      if (end) {
        if (!depth || strlen(stack[depth - 1]) != nl || strncmp(stack[depth - 1], nm, nl))
          return 0;
        depth--;
      } else {
        if (!depth)
          roots++;
        if (depth >= 2)
          return 0;
        if (!empty) {
          if (depth >= 32)
            return 0;
          memcpy(stack[depth], nm, nl);
          stack[depth][nl] = 0;
          depth++;
        }
      }
      s = q + 1;
    }
  }
  return depth == 0 && roots == 1;
}

/* ---- a zip, written again: every entry stored, info.xml replaced */
static uint32_t crc32_of(const uint8_t *p, size_t n) {
  static uint32_t tab[256];
  if (!tab[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      tab[i] = c;
    }
  uint32_t c = 0xffffffffu;
  while (n--)
    c = tab[(c ^ *p++) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

typedef struct {
  uint8_t *b;
  size_t n, cap;
  int bad;
} Buf;

static void put(Buf *o, const void *d, size_t n) {
  if (o->bad)
    return;
  if (o->n + n > o->cap) {
    size_t cap = o->cap ? o->cap : 4096;
    while (cap < o->n + n)
      cap *= 2;
    uint8_t *g = realloc(o->b, cap);
    if (!g) {
      o->bad = 1;
      return;
    }
    o->b = g;
    o->cap = cap;
  }
  memcpy(o->b + o->n, d, n);
  o->n += n;
}
static void put16(Buf *o, uint32_t v) {
  uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
  put(o, b, 2);
}
static void put32(Buf *o, uint32_t v) {
  uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  put(o, b, 4);
}

/* one entry, stored: its local header + data, and its central record */
static void add_entry(Buf *o, Buf *cd, const char *name, const uint8_t *data, size_t dlen) {
  uint32_t crc = crc32_of(data, dlen), off = (uint32_t)o->n;
  size_t nl = strlen(name);
  put32(o, 0x04034b50), put16(o, 20), put16(o, 0), put16(o, 0), put16(o, 0), put16(o, 0x21);
  put32(o, crc), put32(o, (uint32_t)dlen), put32(o, (uint32_t)dlen), put16(o, (uint32_t)nl), put16(o, 0);
  put(o, name, nl);
  put(o, data, dlen);
  put32(cd, 0x02014b50), put16(cd, 20), put16(cd, 20), put16(cd, 0), put16(cd, 0), put16(cd, 0);
  put16(cd, 0x21), put32(cd, crc), put32(cd, (uint32_t)dlen), put32(cd, (uint32_t)dlen);
  put16(cd, (uint32_t)nl), put16(cd, 0), put16(cd, 0), put16(cd, 0), put16(cd, 0), put32(cd, 0);
  put32(cd, off);
  put(cd, name, nl);
}

/* the zip again, every entry stored, info.xml replaced by `info` (or added
 * when it has none) */
static void *rezip(const void *zip, size_t len, const char *info, size_t *out_len) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_mem(&z, zip, len, 0))
    return NULL;
  mz_uint count = mz_zip_reader_get_num_files(&z);
  Buf o = {0}, cd = {0};
  uint32_t entries = 0;
  int had_info = 0;
  for (mz_uint i = 0; i < count && !o.bad; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory)
      continue;
    size_t dlen = 0;
    void *heap = NULL;
    const uint8_t *data;
    if (!strcmp(st.m_filename, "info.xml")) {
      data = (const uint8_t *)info;
      dlen = strlen(info);
      had_info = 1;
    } else {
      heap = mz_zip_reader_extract_to_heap(&z, i, &dlen, 0);
      if (!heap) {
        o.bad = 1;
        break;
      }
      data = heap;
    }
    add_entry(&o, &cd, st.m_filename, data, dlen);
    entries++;
    if (heap)
      mz_free(heap);
  }
  mz_zip_reader_end(&z);
  if (!had_info && info && !o.bad) {
    add_entry(&o, &cd, "info.xml", (const uint8_t *)info, strlen(info));
    entries++;
  }
  uint32_t cd_off = (uint32_t)o.n;
  put(&o, cd.b, cd.n);
  put32(&o, 0x06054b50), put16(&o, 0), put16(&o, 0), put16(&o, entries), put16(&o, entries);
  put32(&o, (uint32_t)cd.n), put32(&o, cd_off), put16(&o, 0);
  free(cd.b);
  if (o.bad || cd.bad) {
    free(o.b);
    return NULL;
  }
  *out_len = o.n;
  return o.b;
}

/* A pack's zip as the engine can read it: *out is the zip (zip itself, or a
 * new one to free), 0; -1 if its info.xml cannot be made readable. A zip
 * without info.xml is left as it is (1). */
static int readable_zip(const void *zip, size_t len, const char *what, const void **out, size_t *out_len,
                        void **to_free) {
  *out = zip;
  *out_len = len;
  *to_free = NULL;
  char *x = info_text(zip, len);
  if (!x)
    return 1;
  if (xml_readable(x)) {
    free(x); /* as it is: the usual case */
    return 0;
  }
  int changed = 0;
  char *fixed = xml_escape_text(x, &changed);
  free(x);
  if (!fixed)
    return -1;
  if (!changed || !xml_readable(fixed)) {
    debugPrintf("[files] %s: info.xml cannot be read, even with its text escaped\n", what);
    free(fixed);
    return -1;
  }
  {
    size_t nl = 0;
    void *nz = rezip(zip, len, fixed, &nl);
    if (!nz) {
      free(fixed);
      return -1;
    }
    debugPrintf("[files] %s: info.xml's text escaped (a name with < > or &)\n", what);
    *out = nz;
    *out_len = nl;
    *to_free = nz;
  }
  free(fixed);
  return 0;
}

/* ------------------------------------------------------- a pack's zip */
/* One level pack's zip in memory: its info.xml -> the table (and the file
 * into zipfiles/<levelid> when `copy`). 0 = added, 1 = an official pack's
 * copy (left as it is), -1 = not a level pack. */
static int add_pack_zip(const void *zip0, size_t len0, int preloaded, int copy, char *id_out, size_t id_cap) {
  const void *zip = zip0;
  size_t len = len0;
  void *fixed = NULL;
  if (copy && readable_zip(zip0, len0, "a level pack from the SD card", &zip, &len, &fixed) != 0)
    return -1;
  LabPack p;
  int times[256], nt = 0;
  if (read_info(zip, len, &p, times, 256, &nt) || (copy && !valid_id(p.id))) {
    free(fixed);
    return -1;
  }
  p.preloaded = preloaded;
  p.ipad = zip_is_ipad(zip, len);
  const LabPack *have = lab_levels_find(p.id);
  if (!preloaded && have && have->preloaded) {
    /* an official pack's own copy: the APK's stays, and stays official */
    debugPrintf("[files] %s is one of the official packs: kept as it is\n", p.id);
    if (id_out)
      snprintf(id_out, id_cap, "%s", p.id);
    free(fixed);
    return 1;
  }
  if (copy) {
    char dst[512];
    zip_path_for(dst, sizeof dst, p.id, p.ipad);
    int bad = write_file(dst, zip, len);
    free(fixed);
    if (bad)
      return -1;
  }
  if (!lab_levels_add(&p, times, nt))
    return -1;
  if (id_out)
    snprintf(id_out, id_cap, "%s", p.id);
  return 0;
}

/* A zip holding pack zips (officiallevels/N.zip): each entry to
 * zipfiles/<entry name>, then into the table. Returns the packs added. */
static int unpack_pack_collection(const void *zip, size_t len, int preloaded) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_mem(&z, zip, len, 0))
    return 0;
  int n = 0;
  mz_uint count = mz_zip_reader_get_num_files(&z);
  for (mz_uint i = 0; i < count; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory)
      continue;
    size_t ilen = 0;
    void *inner = mz_zip_reader_extract_to_heap(&z, i, &ilen, 0);
    if (!inner)
      continue;
    if (preloaded) {
      /* the APK's: written under the entry's own name (the Java: files/
       * zipfiles/<name>), which is the pack's levelid */
      char dst[1024];
      snprintf(dst, sizeof dst, "%s/zipfiles/%s", zip_is_ipad(inner, ilen) ? lab_files_ipad_dir() : lab_files_dir(),
               st.m_filename);
      if (!strstr(st.m_filename, "..") && write_file(dst, inner, ilen) == 0 &&
          add_pack_zip(inner, ilen, 1, 0, NULL, 0) == 0)
        n++;
      else
        debugPrintf("[setup] %s: not a level pack\n", st.m_filename);
    } else {
      /* the SD card's: checked first, then written as zipfiles/<levelid>
       * (what the engine opens), whatever the entry is called */
      int r = add_pack_zip(inner, ilen, 0, 1, NULL, 0);
      if (r == 0)
        n++;
      else if (r < 0)
        debugPrintf("[files] %s: not a level pack\n", st.m_filename);
    }
    mz_free(inner);
  }
  mz_zip_reader_end(&z);
  return n;
}

/* ------------------------------------------------------- the assets */
typedef struct {
  uint32_t hash;
  int count;
  int zips;
} Scan;

static int skipped(const char *rel) {
  /* dk.java: not levels/...; FileSystemUtil.a: not the framework's folders */
  return !strncmp(rel, "levels/", 7) || !strncmp(rel, "images/", 7) || !strncmp(rel, "webkit/", 7) ||
         !strncmp(rel, "sounds/", 7);
}

static void scan_one(const char *name, uint32_t crc, void *arg) {
  Scan *s = arg;
  const char *rel = name + 7;
  if (skipped(rel))
    return;
  for (const char *p = name; *p; p++)
    s->hash = (s->hash ^ (uint8_t)*p) * 16777619u;
  s->hash = (s->hash ^ crc) * 16777619u;
  s->count++;
}

typedef struct {
  int copied, failed, packs;
  int force; /* the APK changed: overwrite */
  int total, done; /* for the progress bar */
  const char *what;
} Copy;

static void copy_one(const char *name, uint32_t crc, void *arg) {
  Copy *c = arg;
  if (c->total > 0)
    dcr_setup_progress(c->what, 400 + 450 * c->done++ / c->total);
  const char *rel = name + 7; /* past "assets/" */
  if (skipped(rel))
    return;
  size_t n = strlen(rel);
  int is_zip = n > 4 && !strcasecmp(rel + n - 4, ".zip");
  char dst[512];
  snprintf(dst, sizeof dst, "%s/%s", lab_files_dir(), rel);
  if (is_zip && lab_levels_imported(rel) && !c->force)
    return;
  if (!is_zip && !c->force && file_exists(dst))
    return;
  size_t len = 0;
  void *d = lab_apk_read(name, &len);
  if (!d || write_file(dst, d, len)) {
    c->failed++;
    free(d);
    return;
  }
  c->copied++;
  if (is_zip) {
    int k = unpack_pack_collection(d, len, 1);
    debugPrintf("[setup] %s: %d level packs\n", rel, k);
    c->packs += k;
    lab_levels_mark_imported(rel);
  }
  free(d);
}

static void stamp_path(char *out, size_t cap) { snprintf(out, cap, "%s/.assets", lab_files_dir()); }

/* ------------------------------------------------------- the .ipa's */
/* an iPad floor (JPEG, 1024 x 1024) as the PNG the engine opens */
static int floor_png(const char *from, const char *to) {
  size_t len = 0;
  uint8_t *jpg = lab_ipa_read(from, &len);
  if (!jpg)
    return -1;
  int w = 0, h = 0;
  uint8_t *rgba = lab_image_decode(jpg, len, &w, &h);
  free(jpg);
  if (!rgba)
    return -1;
  /* RGB is enough (a floor): three bytes a pixel, in place */
  for (int i = 0; i < w * h; i++)
    memmove(rgba + i * 3, rgba + i * 4, 3);
  size_t plen = 0;
  void *png = tdefl_write_image_to_png_file_in_memory_ex(rgba, w, h, 3, &plen, 1, 0);
  free(rgba);
  if (!png)
    return -1;
  int rc = write_file(to, png, plen);
  mz_free(png);
  return rc;
}

typedef struct {
  uint32_t hash;
} IpaScan;

static void ipa_scan(const char *name, uint32_t crc, void *arg) {
  IpaScan *s = arg;
  for (const char *p = name; *p; p++)
    s->hash = (s->hash ^ (uint8_t)*p) * 16777619u;
  s->hash = (s->hash ^ crc) * 16777619u;
}

static void ipad_setup(void) {
  mkdir(lab_files_ipad_dir(), 0777);
  char zf[320];
  snprintf(zf, sizeof zf, "%s/zipfiles", lab_files_ipad_dir());
  mkdir(zf, 0777);
  IpaScan sc = {2166136261u ^ 2u}; /* 2: the walls too */
  lab_ipa_list("officiallevelsipad/", ipa_scan, &sc);
  lab_ipa_list("textures-ipad/", ipa_scan, &sc);
  char sp[320];
  snprintf(sp, sizeof sp, "%s/.ipa", lab_files_ipad_dir());
  unsigned long have = 0;
  FILE *f = fopen(sp, "r");
  if (f) {
    if (fscanf(f, "%lx", &have) != 1)
      have = 0;
    fclose(f);
  }
  int missing = 0;
  for (int i = 0; i < lab_levels_count(); i++)
    missing |= lab_levels_at(i)->ipad && lab_levels_at(i)->preloaded;
  missing = !missing;
  if (have == sc.hash && !missing)
    return;
  show_work("Unpacking the iPad level packs", 850);
  int packs = 0, floors = 0;
  /* 1, then 2 and 3 (newer copies of a pack replace the older) */
  for (int n = 1; n <= 9; n++) {
    char nm[48];
    snprintf(nm, sizeof nm, "officiallevelsipad/%d", n);
    size_t len = 0;
    void *d = lab_ipa_read(nm, &len);
    if (!d)
      continue;
    packs += unpack_pack_collection(d, len, 1);
    free(d);
  }
  dcr_setup_progress("Preparing the iPad floors and walls", 940);
  static const char *const themes[] = {"theme-classic", "theme-metal", "theme-plastic"};
  for (unsigned t = 0; t < 3; t++) {
    char from[128], to[512];
    snprintf(from, sizeof from, "textures-ipad/%s/floor-1024x1024.jpg", themes[t]);
    snprintf(to, sizeof to, "%s/textures/%s/floor-512x512.png", lab_files_ipad_dir(), themes[t]);
    if (lab_ipa_exists(from) && floor_png(from, to) == 0)
      floors++;
    /* the iPad's walls (PVRTC, 1024 x 1024): as the engine's wall-512x512 */
    snprintf(from, sizeof from, "textures-ipad/%s/wall-1024x1024.pvr", themes[t]);
    snprintf(to, sizeof to, "%s/textures/%s/wall-512x512.png", lab_files_ipad_dir(), themes[t]);
    if (lab_ipa_exists(from) && floor_png(from, to) == 0)
      floors++;
  }
  debugPrintf("[setup] the .ipa: %d iPad level packs, %d floors and walls\n", packs, floors);
  if (packs && (f = fopen(sp, "w"))) {
    fprintf(f, "%08lx\n", (unsigned long)sc.hash);
    fclose(f);
  }
  lab_levels_save();
  lab_reg_save();
}

void lab_files_setup(void) {
  mkdir(lab_files_dir(), 0777);
  char zf[320];
  snprintf(zf, sizeof zf, "%s/zipfiles", lab_files_dir());
  mkdir(zf, 0777);
  /* the engine's own folders: ghosts/ (the ghost ball: your best run of
   * each level, ghosts/<pack>.<level>) and temp/. Nothing made them -- the
   * Java did not, and the engine's mkdir runs before it has its resource
   * path -- so no ghost was ever saved. (The iPad engine's resource dir
   * falls back to these.) */
  static const char *const own[2] = {"ghosts", "temp"};
  for (int i = 0; i < 2; i++) {
    snprintf(zf, sizeof zf, "%s/%s", lab_files_dir(), own[i]);
    mkdir(zf, 0777);
  }
  mkdir(lab_sd_packs_dir(), 0777);

  Scan s = {2166136261u, 0, 0};
  lab_apk_list("assets/", scan_one, &s);
  char sp[320];
  stamp_path(sp, sizeof sp);
  unsigned long have = 0;
  FILE *f = fopen(sp, "r");
  if (f) {
    if (fscanf(f, "%lx", &have) != 1)
      have = 0;
    fclose(f);
  }
  int packs_missing = lab_levels_count() == 0;
  if (have != s.hash || packs_missing) {
    const char *what = have ? "The APK changed: unpacking the game's files again"
                            : "Unpacking the game's files and levels";
    show_work(what, 400);
    Copy c = {0, 0, 0, have != 0 || packs_missing, s.count, 0, what};
    lab_apk_list("assets/", copy_one, &c);
    debugPrintf("[setup] %d files written (%d failed), %d level packs\n", c.copied, c.failed, c.packs);
    if (!c.failed && (f = fopen(sp, "w"))) {
      fprintf(f, "%08lx\n", (unsigned long)s.hash);
      fclose(f);
    }
    lab_levels_save();
    lab_reg_save();
  }
  if (lab_ipa_present())
    ipad_setup();
  int added = 0, failed = 0;
  lab_files_import_sd(&added, &failed);
  debugPrintf("[files] %s: %d assets; %d level packs (%d new from the SD card)\n", lab_files_dir(),
              s.count, lab_levels_count(), added);
  dcr_dircache_forget(); /* the folders made above */
}

/* ---------------------------------------------------- the SD card's packs */
int lab_files_import_sd(int *added, int *failed) {
  *added = *failed = 0;
  DIR *d = opendir(lab_sd_packs_dir());
  if (!d)
    return 0;
  struct dirent *e;
  int seen = 0;
  while ((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (n < 5 || strcasecmp(e->d_name + n - 4, ".zip"))
      continue;
    seen++;
    char path[600];
    snprintf(path, sizeof path, "%s/%s", lab_sd_packs_dir(), e->d_name);
    struct stat st;
    if (stat(path, &st) != 0)
      continue;
    char key[64 + 32];
    snprintf(key, sizeof key, "sd:%.40s:%ld", e->d_name, (long)st.st_size);
    key[63] = 0;
    if (lab_levels_imported(key))
      continue;
    FILE *f = fopen(path, "rb");
    void *buf = f && st.st_size > 0 ? malloc((size_t)st.st_size) : NULL;
    size_t len = buf ? fread(buf, 1, (size_t)st.st_size, f) : 0;
    if (f)
      fclose(f);
    char id[64];
    int ok = 0;
    if (buf && len == (size_t)st.st_size) {
      int r = add_pack_zip(buf, len, 0, 1, id, sizeof id);
      if (r == 1) {
        ok = 2; /* nothing new, and not a failure */
      } else if (r == 0) {
        ok = 1;
        debugPrintf("[files] level pack %s from the SD card (%s)\n", id, e->d_name);
      } else {
        int k = unpack_pack_collection(buf, len, 0);
        ok = k > 0;
        debugPrintf("[files] %s: %d level packs from the SD card\n", e->d_name, k);
      }
    }
    free(buf);
    if (ok) {
      if (ok == 1)
        (*added)++;
      lab_levels_mark_imported(key);
    } else {
      (*failed)++;
      debugPrintf("[files] %s: not a Labyrinth 2 level pack (no info.xml)\n", e->d_name);
    }
  }
  closedir(d);
  if (*added)
    lab_levels_save();
  return seen;
}

/* ------------------------------------------------------ the server's packs */
/* How many of a pack's levels are in its zip, level0.xml up, at most want:
 * the engine reads level<n>.xml without looking whether it is there (a
 * server pack whose list said more levels than its zip had crashed it,
 * hardware 2026-09-26). Case as the engine's (minizip, any case). */
static int zip_levels(const void *zip, size_t len, int want) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_mem(&z, zip, len, 0))
    return 0;
  int n = 0;
  for (; n < want; n++) {
    char name[32];
    snprintf(name, sizeof name, "level%d.xml", n);
    if (mz_zip_reader_locate_file(&z, name, NULL, 0) < 0)
      break;
  }
  mz_zip_reader_end(&z);
  return n;
}

int lab_files_store_pack(const char *id, const void *zip0, size_t len0, LabPack *p, int *times, int cap,
                         int *ntimes) {
  *ntimes = 0;
  memset(p, 0, sizeof *p);
  if (!valid_id(id) || len0 < 4 || memcmp(zip0, "PK\3\4", 4))
    return -1;
  const LabPack *have = lab_levels_find(id);
  if (have && have->preloaded) {
    debugPrintf("[files] %s is one of the official packs: not replaced\n", id);
    return -1;
  }
  const void *zip;
  size_t len;
  void *fixed;
  if (readable_zip(zip0, len0, id, &zip, &len, &fixed) < 0)
    return -1;
  char dst[512];
  int ipad = zip_is_ipad(zip, len);
  zip_path_for(dst, sizeof dst, id, ipad);
  if (write_file(dst, zip, len)) {
    debugPrintf("[files] %s: could not be written to the SD card\n", id);
    free(fixed);
    return -1;
  }
  int bad = read_info(zip, len, p, times, cap, ntimes);
  p->ipad = ipad;
  if (!bad) {
    int have = zip_levels(zip, len, p->nlevels);
    if (have < p->nlevels) {
      debugPrintf("[files] %s: its info says %d levels, its zip has %d: those are kept\n", id, p->nlevels, have);
      p->nlevels = have;
      if (*ntimes > have)
        *ntimes = have;
      bad = have == 0;
    }
  }
  free(fixed);
  if (bad) {
    memset(p, 0, sizeof *p);
    *ntimes = 0;
    return 1;
  }
  return 0;
}

int lab_files_delete_pack(const char *id) {
  const LabPack *k = lab_levels_find(id);
  if (!k || k->preloaded || !valid_id(id))
    return -1;
  char p[512];
  zip_path(p, sizeof p, id);
  unlink(p);
  changed(p);
  lab_levels_remove(id);
  lab_levels_save();
  debugPrintf("[files] level pack %s deleted\n", id);
  return 0;
}

int lab_files_have_pack(const char *id) {
  if (!valid_id(id))
    return 0;
  char p[512];
  zip_path(p, sizeof p, id);
  return file_exists(p);
}

int lab_files_write_png(const char *path, const uint8_t *rgba, int w, int h) {
  size_t len = 0;
  void *png = tdefl_write_image_to_png_file_in_memory_ex(rgba, w, h, 4, &len, 1, 0);
  if (!png)
    return -1;
  int rc = write_file(path, png, len);
  mz_free(png);
  return rc;
}

/* a pack that may not be in the table (one being looked at before it is
 * downloaded): its zip where its board's packs are */
int lab_files_have_pack_dev(const char *id, int ipad) {
  if (!valid_id(id))
    return 0;
  char p[512];
  zip_path_for(p, sizeof p, id, ipad);
  return file_exists(p);
}

/* ------------------------------------------- a pack about to be played */
static void *read_whole(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  void *b = NULL;
  long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
  if (n > 0 && n < (32L << 20) && fseek(f, 0, SEEK_SET) == 0 && (b = malloc((size_t)n))) {
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
      free(b);
      b = NULL;
    }
  }
  fclose(f);
  *len = b ? (size_t)n : 0;
  return b;
}

/* text for info.xml: & < > escaped */
static void put_xml(char *o, size_t cap, size_t *n, const char *s) {
  for (; *s && *n + 6 < cap; s++) {
    const char *e = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : NULL;
    if (e) {
      memcpy(o + *n, e, strlen(e));
      *n += strlen(e);
    } else {
      o[(*n)++] = *s;
    }
  }
  o[*n] = 0;
}

/* info.xml as the server writes it for a published pack (the official
 * ones' layout), from the pack's row and its designer times */
static char *make_info(const LabPack *p) {
  size_t cap = 2048 + (size_t)(p->nlevels > 0 ? p->nlevels : 0) * 12, n = 0;
  char *o = malloc(cap);
  if (!o)
    return NULL;
  n += (size_t)snprintf(o + n, cap - n, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<levelpack>\n  <levelid>%s</levelid>\n  <levelname>", p->id);
  put_xml(o, cap, &n, p->name[0] ? p->name : p->id);
  n += (size_t)snprintf(o + n, cap - n, "</levelname>\n  <authorid>");
  put_xml(o, cap, &n, p->author_id);
  n += (size_t)snprintf(o + n, cap - n, "</authorid>\n  <authorname>");
  put_xml(o, cap, &n, p->author);
  n += (size_t)snprintf(o + n, cap - n,
                        "</authorname>\n  <nbrunits>%d</nbrunits>\n  <difficulty>%d</difficulty>\n"
                        "  <theme>%d</theme>\n  <reqver>%d</reqver>\n  <times>",
                        p->nlevels, p->difficulty >= 0 ? p->difficulty : 0, p->theme, p->reqver);
  for (int i = 0; i < p->nlevels && n + 16 < cap; i++)
    n += (size_t)snprintf(o + n, cap - n, "%d,", lab_levels_designer_time(p->id, i));
  snprintf(o + n, cap - n, "</times>\n</levelpack>\n");
  return o;
}

int lab_files_complete_pack(const LabPack *p) {
  if (!p || p->preloaded || !valid_id(p->id) || p->nlevels <= 0)
    return 0;
  char path[512];
  zip_path(path, sizeof path, p->id);
  size_t len = 0;
  void *zip = read_whole(path, &len);
  if (!zip)
    return -1;
  char *have = info_text(zip, len);
  if (have) {
    free(have);
    free(zip);
    return 0;
  }
  char *xml = make_info(p);
  size_t nl = 0;
  void *nz = xml ? rezip(zip, len, xml, &nl) : NULL;
  int rc = nz && write_file(path, nz, nl) == 0 ? 0 : -1;
  debugPrintf("[files] %s: no info.xml in its zip (a pack not published yet): one made from its row%s\n", p->id,
              rc ? " -- could not be written" : "");
  free(nz);
  free(xml);
  free(zip);
  return rc;
}

int lab_files_pack_levels(const char *id, int ipad, int want) {
  if (!valid_id(id) || want <= 0)
    return 0;
  char path[512];
  zip_path_for(path, sizeof path, id, ipad);
  size_t len = 0;
  void *zip = read_whole(path, &len);
  if (!zip && ipad) { /* the iPad engine falls back to data/files too */
    zip_path_for(path, sizeof path, id, 0);
    zip = read_whole(path, &len);
  }
  if (!zip)
    return 0;
  int n = zip_levels(zip, len, want);
  free(zip);
  return n;
}

int lab_files_check_pack(const LabPack *p, char *why, size_t cap) {
  why[0] = 0;
  if (!valid_id(p->id)) {
    snprintf(why, cap, "Its ID (%s) is not one a level pack can have.", p->id);
    return -1;
  }
  char path[512];
  zip_path(path, sizeof path, p->id);
  size_t len = 0;
  void *zip = read_whole(path, &len);
  if (!zip) {
    snprintf(why, cap, "Its file is missing (data/files/zipfiles/%s).", p->id);
    debugPrintf("[files] %s: %s\n", p->id, why);
    return -1;
  }
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_mem(&z, zip, len, 0)) {
    free(zip);
    snprintf(why, cap, "Its file (%u bytes) is not a zip that can be read.", (unsigned)len);
    debugPrintf("[files] %s: %s\n", p->id, why);
    return -1;
  }
  /* what is in it, for debug.log */
  char list[512];
  size_t n = 0;
  mz_uint count = mz_zip_reader_get_num_files(&z);
  list[0] = 0;
  for (mz_uint i = 0; i < count && n + 40 < sizeof list; i++) {
    mz_zip_archive_file_stat st;
    if (mz_zip_reader_file_stat(&z, i, &st))
      n += (size_t)snprintf(list + n, sizeof list - n, "%s%s (%u)", i ? ", " : "", st.m_filename,
                            (unsigned)st.m_uncomp_size);
  }
  debugPrintf("[files] %s: %u bytes, %u entries: %s\n", p->id, (unsigned)len, (unsigned)count, list);
  int rc = 0;
  if (p->nlevels <= 0) {
    snprintf(why, cap, "It has no levels.");
    rc = -1;
  }
  for (int i = 0; i < p->nlevels && !rc; i++) {
    char name[32];
    snprintf(name, sizeof name, "level%d.xml", i);
    if (mz_zip_reader_locate_file(&z, name, NULL, 0) < 0) {
      snprintf(why, cap, "Level %d of %d (%s) is not in it.", i + 1, p->nlevels, name);
      rc = -1;
    }
  }
  mz_zip_reader_end(&z);
  free(zip);
  if (rc)
    debugPrintf("[files] %s: not played: %s\n", p->id, why);
  return rc;
}
