#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <assert.h>
#include <ncurses.h>
#include <pthread.h>
#include <sys/wait.h>

#include <mpd/client.h>
#include <taglib/tag_c.h>

#define min(a, b) ((a) < (b) ? (a) : (b))

#define return_defer(value)                                                                        \
  do {                                                                                             \
    result = (value);                                                                              \
    goto defer;                                                                                    \
  } while (0)

// List
#define LIST_INIT_CAP 128

#define list_free(l)                                                                               \
  do {                                                                                             \
    free((l)->data);                                                                               \
    memset((l), 0, sizeof(*(l)));                                                                  \
  } while (0)

#define list_append(l, v)                                                                          \
  do {                                                                                             \
    if ((l)->count >= (l)->capacity) {                                                             \
      (l)->capacity = (l)->capacity == 0 ? LIST_INIT_CAP : (l)->capacity * 2;                      \
      (l)->data = realloc((l)->data, (l)->capacity * sizeof(*(l)->data));                          \
      assert((l)->data);                                                                           \
    }                                                                                              \
                                                                                                   \
    (l)->data[(l)->count++] = (v);                                                                 \
  } while (0)

#define list_append_many(l, v, c)                                                                  \
  do {                                                                                             \
    if ((l)->count + (c) > (l)->capacity) {                                                        \
      if ((l)->capacity == 0) {                                                                    \
        (l)->capacity = LIST_INIT_CAP;                                                             \
      }                                                                                            \
                                                                                                   \
      while ((l)->count + (c) > (l)->capacity) {                                                   \
        (l)->capacity *= 2;                                                                        \
      }                                                                                            \
                                                                                                   \
      (l)->data = realloc((l)->data, (l)->capacity * sizeof(*(l)->data));                          \
      assert((l)->data);                                                                           \
    }                                                                                              \
                                                                                                   \
    if ((v) != NULL) {                                                                             \
      memcpy((l)->data + (l)->count, (v), (c) * sizeof(*(l)->data));                               \
      (l)->count += (c);                                                                           \
    }                                                                                              \
  } while (0)

// Str
typedef struct {
  const char *data;
  size_t size;
} Str;

Str str_from_cstr(const char *data) {
  return (Str){.data = data, .size = strlen(data)};
}

#define StrFmt "%.*s"
#define StrArg(s) (int)(s).size, (s).data
#define StrLit(s) ((Str){.data = (s), .size = sizeof(s)})

bool str_eq(Str a, Str b) {
  return a.size == b.size && !memcmp(a.data, b.data, a.size);
}

Str str_trim(Str str, char ch) {
  for (size_t i = 0; i < str.size; ++i) {
    if (str.data[i] != ch) {
      str.data += i;
      str.size -= i;
      break;
    }
  }

  if (str.size) {
    for (size_t i = str.size; i > 0; --i) {
      if (str.data[i - 1] != ch) {
        str.size = i;
        break;
      }
    }
  }

  return str;
}

Str str_split(Str *str, char ch) {
  Str result = *str;
  const char *end = memchr(str->data, ch, str->size);

  if (end == NULL) {
    str->data += str->size;
    str->size = 0;
  } else {
    result.size = end - str->data;
    str->data += result.size + 1;
    str->size -= result.size + 1;
  }

  return result;
}

// Buffer
typedef struct {
  char *data;
  size_t count;
  size_t capacity;
} Buffer;

void buffer_push_str(Buffer *buffer, Str str) {
  list_append_many(buffer, str.data, str.size);
}

bool buffer_read_file(Buffer *buffer, Str *str, const char *path) {
  bool result = true;

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    return_defer(false);
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  if (size == -1) {
    return_defer(false);
  }
  fseek(f, 0, SEEK_SET);

  list_append_many(buffer, NULL, size);
  if (fread(buffer->data + buffer->count, 1, size, f) != size) {
    return_defer(false);
  }

  str->data = buffer->data + buffer->count;
  str->size = size;
  buffer->count += size;
  list_append(buffer, '\0');

defer:
  if (f) {
    fclose(f);
  }

  return result;
}

Str buffer_terminate_str(Buffer *buffer, Str str) {
  buffer->data[(str.data - buffer->data) + str.size] = '\0';
  return str;
}

// Album
typedef struct {
  Str value;
  bool ready;
} Link;

typedef struct {
  Link *data;
  size_t count;
  size_t capacity;
} Links;

typedef struct {
  Str name;
  Str path;
} Song;

typedef struct {
  Song *data;
  size_t count;
  size_t capacity;
} Songs;

typedef struct {
  Str name;
  bool ready;

  Links links;
  Songs songs;
} Album;

void album_free(Album *album) {
  list_free(&album->links);
  list_free(&album->songs);
}

void album_mark_ready(Album *album) {
  if (!album->ready) {
    for (size_t i = 0; i < album->links.count; i++) {
      if (!album->links.data[i].ready) {
        return;
      }
    }

    album->ready = true;
  }
}

// Artist
typedef struct {
  Str name;

  Album *data;
  size_t count;
  size_t capacity;
} Artist;

void artist_free(Artist *artist) {
  for (size_t i = 0; i < artist->count; i++) {
    album_free(&artist->data[i]);
  }
  list_free(artist);
}

bool artist_mark_link(Artist *artist, Str str) {
  for (size_t i = 0; i < artist->count; i++) {
    Album *album = &artist->data[i];
    for (size_t j = 0; j < album->links.count; j++) {
      Link *link = &album->links.data[j];
      if (str_eq(str, link->value)) {
        link->ready = true;
        return true;
      }
    }
  }

  return false;
}

// Library
typedef struct {
  size_t line;
  const char *message;
} Error;

typedef struct {
  Artist *data;
  size_t count;
  size_t capacity;

  size_t pending;
  Buffer buffer;
} Library;

void library_free(Library *library) {
  for (size_t i = 0; i < library->count; i++) {
    artist_free(&library->data[i]);
  }
  list_free(&library->buffer);
  list_free(library);
}

Error library_parse(Library *library, Str contents) {
  Album *album = NULL;
  Artist *artist = NULL;

  for (size_t row = 1; contents.size > 0; row++) {
    Str line = str_trim(str_split(&contents, '\n'), ' ');
    size_t indent = 0;
    while (line.size > 0 && *line.data == '\t') {
      indent++;
      line.data++;
      line.size--;
    }

    if ((line.size > 0 && *line.data == '#') || line.size == 0) {
      continue;
    }

    switch (indent) {
    case 0:
      list_append(library, (Artist){0});
      artist = &library->data[library->count - 1];
      artist->name = buffer_terminate_str(&library->buffer, line);

      album = NULL;
      break;

    case 1:
      if (artist == NULL) {
        return (Error){.line = row, .message = "encountered album without an artist"};
      }

      list_append(artist, (Album){0});
      album = &artist->data[artist->count - 1];
      album->name = buffer_terminate_str(&library->buffer, line);
      break;

    case 2:
      if (album == NULL) {
        return (Error){.line = row, .message = "encountered song/link without an album"};
      } else {
        Str name = str_trim(str_split(&line, '@'), ' ');
        line = buffer_terminate_str(&library->buffer, str_trim(line, ' '));

        if (name.size == 0) {
          Link link = {
            .ready = false,
            .value = line,
          };

          list_append(&album->links, link);
        } else {
          Song song = {
            .name = buffer_terminate_str(&library->buffer, name),
            .path = line,
          };

          list_append(&album->songs, song);
        }
      }
      break;

    default:
      return (Error){.line = row, .message = "invalid indentation level"};
    }
  }

  return (Error){0};
}

void library_mark_links(Library *library, Str contents) {
  while (contents.size > 0) {
    Str link = str_split(&contents, '\n');
    for (size_t i = 0; i < library->count; i++) {
      if (artist_mark_link(&library->data[i], link)) {
        break;
      }
    }
  }

  for (size_t i = 0; i < library->count; i++) {
    Artist *artist = &library->data[i];
    for (size_t j = 0; j < artist->count; j++) {
      album_mark_ready(&artist->data[j]);
    }
  }

  for (size_t i = 0; i < library->count; i++) {
    Artist *artist = &library->data[i];
    for (size_t j = 0; j < artist->count; j++) {
      Album *album = &artist->data[j];
      for (size_t k = 0; k < album->links.count; k++) {
        if (!album->links.data[k].ready) {
          library->pending++;
        }
      }
    }
  }
}

bool library_save_links(Library *library, const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) {
    return false;
  }

  for (size_t i = 0; i < library->count; i++) {
    Artist *artist = &library->data[i];
    for (size_t j = 0; j < artist->count; j++) {
      Album *album = &artist->data[j];
      for (size_t k = 0; k < album->links.count; k++) {
        Link *link = &album->links.data[k];
        if (link->ready) {
          fprintf(f, StrFmt "\n", StrArg(link->value));
        }
      }
    }
  }

  fclose(f);
  return true;
}

// Tui
typedef enum {
  COLOR_ERROR = 1,
  COLOR_NORMAL,
  COLOR_HEADER,
  COLOR_SUCCESS,
  COLOR_CURRENT,
  COLOR_ACTIVE_PANE,
  COLOR_INACTIVE_PANE,
} Color;

void tui_init(void) {
  initscr();
  cbreak();
  noecho();
  curs_set(0);
  keypad(stdscr, true);
  start_color();
  use_default_colors();
  init_pair(COLOR_ERROR, 9, -1);
  init_pair(COLOR_NORMAL, -1, -1);
  init_pair(COLOR_HEADER, 3, -1);
  init_pair(COLOR_SUCCESS, 2, -1);
  init_pair(COLOR_CURRENT, 4, -1);
  init_pair(COLOR_ACTIVE_PANE, 6, -1);
  init_pair(COLOR_INACTIVE_PANE, 5, -1);

  timeout(1000);
}

void tui_exit(void) {
  curs_set(1);
  endwin();
}

void tui_print_name(int y, int x, int width, Str name, chtype attr) {
  name.size = min(name.size, width);
  attron(attr);
  mvprintw(y, x, StrFmt "%*c", StrArg(name), (int)(width - name.size), ' ');
  attroff(attr);
}

void tui_get_bounds(int height, size_t current, size_t count, size_t *first, size_t *last) {
  *first = current - current % (height - 1);
  *last = min(count, height - 1 + *first);
}

// Tag
bool tag_file(const char *path, Str artist, Str album, Str name) {
  bool result = true;

  TagLib_File *file = taglib_file_new(path);
  if (!file) {
    return_defer(false);
  }

  TagLib_Tag *tag = taglib_file_tag(file);
  if (!tag) {
    return_defer(false);
  }

  taglib_tag_set_artist(tag, artist.data);
  taglib_tag_set_album(tag, album.data);
  taglib_tag_set_title(tag, name.data);
  taglib_file_save(file);

defer:
  if (file) {
    taglib_file_free(file);
  }

  return result;
}

// App
typedef enum {
  STATUS_ERROR,
  STATUS_SUCCESS
} StatusType;

typedef struct {
  Library library;

  char status[256];
  StatusType status_type;

  size_t current_pane;
  size_t current_song;
  size_t current_album;
  size_t current_artist;

  bool download_started;
  pid_t download_process;
  Buffer download_buffer;
  pthread_t download_thread;

  Buffer misc_buffer;
  struct mpd_connection *mpd;
} App;

bool app_execute(App *app, pid_t *process, char *const *args) {
  pid_t backup = 0;
  if (!process) {
    process = &backup;
  }

  *process = fork();
  if (*process == -1) {
    return false;
  }

  if (*process == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    execvp(*args, args);
    return false;
  }

  int status = 0;
  if (waitpid(*process, &status, 0) == -1) {
    return false;
  }

  return WEXITSTATUS(status) == 0;
}

#define app_set_status(app, style, ...)                                                            \
  do {                                                                                             \
    (app)->status_type = (style);                                                                  \
    snprintf((app)->status, sizeof((app)->status), __VA_ARGS__);                                   \
  } while (0)

void *app_downloader(void *arg) {
  App *app = arg;

  app_set_status(app, STATUS_SUCCESS, "%zu Download%s started", app->library.pending,
                 app->library.pending == 1 ? "" : "s");

  size_t count = 0;
  for (size_t i = 0; i < app->library.count; i++) {
    Artist *artist = &app->library.data[i];
    for (size_t j = 0; j < artist->count; j++) {
      Album *album = &artist->data[j];
      for (size_t k = 0; k < album->links.count; k++) {
        Link *link = &album->links.data[k];
        if (link->ready) {
          continue;
        }

        app->download_buffer.count = 0;
        buffer_push_str(&app->download_buffer, artist->name);
        list_append(&app->download_buffer, '/');
        buffer_push_str(&app->download_buffer, album->name);
        buffer_push_str(&app->download_buffer, StrLit("/%(title)s.%(ext)s"));
        list_append(&app->download_buffer, '\0');

        char *const args[] = {
          "yt-dlp", "-x", "-o", app->download_buffer.data, (char *const)link->value.data, NULL,
        };
        bool status = app_execute(app, &app->download_process, args);

        count++;
        app->download_process = 0;
        if (status) {
          link->ready = true;
          album_mark_ready(album);

          app_set_status(app, STATUS_SUCCESS, "[%zu/%zu] Downloaded " StrFmt, count,
                         app->library.pending, StrArg(link->value));
        } else {
          app_set_status(app, STATUS_ERROR, "[%zu/%zu] Error: could not download " StrFmt, count,
                         app->library.pending, StrArg(link->value));
        }
      }
    }
  }

  return NULL;
}

void app_connect_mpd(App *app) {
  if (app->mpd) {
    mpd_connection_free(app->mpd);
  }

  app->mpd = mpd_connection_new(NULL, 0, 0);
  if (!app->mpd || mpd_connection_get_error(app->mpd) != MPD_ERROR_SUCCESS) {
    app->mpd = NULL;
    app_set_status(app, STATUS_ERROR, "Error: could not connect to mpd");
  }
}

bool app_check_mpd_state(App *app) {
  if (mpd_connection_get_error(app->mpd) != MPD_ERROR_SUCCESS) {
    app_set_status(app, STATUS_ERROR, "Error: %s", mpd_connection_get_error_message(app->mpd));

    if (!mpd_connection_clear_error(app->mpd)) {
      app_connect_mpd(app);
    }

    return false;
  }

  return true;
}

void app_init(App *app) {
  tui_init();

  Str links = {0};
  buffer_read_file(&app->library.buffer, &links, ".links");

  Str config = {0};
  if (!buffer_read_file(&app->library.buffer, &config, ".config")) {
    app_set_status(app, STATUS_ERROR, "Error: could not load config");
    return;
  }

  Error error = library_parse(&app->library, config);
  if (error.message) {
    app_set_status(app, STATUS_ERROR, "Error: %s in line %zu", error.message, error.line);
    return;
  }
  library_mark_links(&app->library, links);

  if (app->library.pending) {
    if (pthread_create(&app->download_thread, NULL, app_downloader, app)) {
      app_set_status(app, STATUS_ERROR, "Error: could not start downloader thread");
    } else {
      app->download_started = true;
    }
  }

  app->mpd = mpd_connection_new(NULL, 0, 0);
  if (!app->mpd || mpd_connection_get_error(app->mpd) != MPD_ERROR_SUCCESS) {
    app_set_status(app, STATUS_ERROR, "Error: could not connect to mpd");
  }
}

void app_exit(App *app) {
  tui_exit();

  if (app->mpd) {
    mpd_connection_free(app->mpd);
  }

  if (app->download_started) {
    pthread_cancel(app->download_thread);
    pthread_join(app->download_thread, NULL);

    if (app->download_process != 0) {
      kill(app->download_process, SIGKILL);
    }

    library_save_links(&app->library, ".links");
  }

  list_free(&app->misc_buffer);
  list_free(&app->download_buffer);
  library_free(&app->library);
}

void app_draw(App *app) {
  erase();

  int width = getmaxx(stdscr) - 1;
  int height = getmaxy(stdscr) - 1;

  // Artists
  {
    size_t first, last;
    tui_get_bounds(height - 4, app->current_artist, app->library.count, &first, &last);

    size_t row = 3;
    for (size_t i = first; i < last; i++) {
      chtype attr = 0;
      if (i == app->current_artist) {
        attr = A_REVERSE;
        attr |= COLOR_PAIR(app->current_pane == 0 ? COLOR_ACTIVE_PANE : COLOR_INACTIVE_PANE);
      }
      tui_print_name(row++, 1, width / 3, app->library.data[i].name, attr);
    }
  }

  if (app->current_artist < app->library.count) {
    Artist *artist = &app->library.data[app->current_artist];

    // Albums
    {
      size_t first, last;
      tui_get_bounds(height - 4, app->current_album, artist->count, &first, &last);

      size_t row = 3;
      for (size_t i = first; i < last; i++) {
        chtype attr = 0;
        if (i == app->current_album) {
          attr = A_REVERSE;
          attr |= COLOR_PAIR(app->current_pane == 1 ? COLOR_ACTIVE_PANE : COLOR_INACTIVE_PANE);
        }
        tui_print_name(row++, width / 3 + 1, width / 3, artist->data[i].name, attr);
      }
    }

    // Songs
    if (app->current_album < artist->count) {
      Album *album = &artist->data[app->current_album];

      if (album->ready) {
        size_t first, last;
        tui_get_bounds(height - 4, app->current_song, album->songs.count, &first, &last);

        size_t row = 3;
        for (size_t i = first; i < last; i++) {
          chtype attr = 0;
          if (i == app->current_song) {
            attr = A_REVERSE;
            attr |= COLOR_PAIR(app->current_pane == 2 ? COLOR_ACTIVE_PANE : COLOR_INACTIVE_PANE);
          }

          tui_print_name(row++, width * 2 / 3 + 1, width / 3, album->songs.data[i].name, attr);
        }
      } else {
        mvprintw(3, width * 2 / 3 + 1, "Not Ready");
      }
    }
  }

  // Status
  {
    chtype attr = 0;
    switch (app->status_type) {
    case STATUS_ERROR:
      attr = COLOR_PAIR(COLOR_ERROR) | A_BOLD;
      break;

    case STATUS_SUCCESS:
      attr = COLOR_PAIR(COLOR_SUCCESS) | A_BOLD;
      break;
    }

    tui_print_name(height - 1, 1, width - 2, str_from_cstr(app->status), attr);
  }

  // MPD Status
  if (app->mpd) {
    struct mpd_song *song = mpd_run_current_song(app->mpd);
    struct mpd_status *status = mpd_run_status(app->mpd);

    if (song && status) {
      chtype attr = COLOR_PAIR(COLOR_HEADER) | A_BOLD |
                    A_DIM * (mpd_status_get_state(status) != MPD_STATE_PLAY);

      static char header[256];

      const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
      const char *album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
      const char *artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);

      if (artist && album && artist) {
        snprintf(header, sizeof(header), "%s > %s > %s", artist, album, title);
      } else {
        snprintf(header, sizeof(header), "%s", mpd_song_get_uri(song));
      }
      tui_print_name(1, 1, width - 2, str_from_cstr(header), attr);

      unsigned int duration = mpd_song_get_duration(song);
      unsigned int elapsed = mpd_status_get_elapsed_time(status);

      snprintf(header, sizeof(header), "   %u:%02u/%u:%02u", elapsed / 60, elapsed % 60,
               duration / 60, duration % 60);

      Str clock = str_from_cstr(header);
      tui_print_name(1, width - clock.size, clock.size, clock, attr);
    }

    if (song) {
      mpd_song_free(song);
    }

    if (status) {
      mpd_status_free(status);
    }
  }

  // Border
  {
    attron(A_DIM);

    // Corners
    mvaddch(0, 0, ACS_ULCORNER);
    mvaddch(0, width, ACS_URCORNER);
    mvaddch(height, 0, ACS_LLCORNER);
    mvaddch(height, width, ACS_LRCORNER);

    // Sides
    mvhline(0, 1, ACS_HLINE, width - 1);
    mvhline(height, 1, ACS_HLINE, width - 1);
    mvvline(1, 0, ACS_VLINE, height - 1);
    mvvline(1, width * 3 / 3, ACS_VLINE, height - 1);

    // Columns
    mvvline(3, width * 1 / 3, ACS_VLINE, height - 4);
    mvvline(3, width * 2 / 3, ACS_VLINE, height - 4);

    // Status
    mvhline(2, 1, ACS_HLINE, width - 1);
    mvhline(height - 2, 1, ACS_HLINE, width - 1);

    // Junctions
    mvaddch(2, 0, ACS_LTEE);
    mvaddch(2, width, ACS_RTEE);
    mvaddch(height - 2, 0, ACS_LTEE);
    mvaddch(height - 2, width, ACS_RTEE);

    mvaddch(2, width * 1 / 3, ACS_TTEE);
    mvaddch(height - 2, width * 1 / 3, ACS_BTEE);
    mvaddch(2, width * 2 / 3, ACS_TTEE);
    mvaddch(height - 2, width * 2 / 3, ACS_BTEE);

    attroff(A_DIM);
  }

  refresh();
}

void app_loop(App *app) {
  while (true) {
    app_draw(app);

    switch (getch()) {
    case 'q':
      return;

    case 'j':
      switch (app->current_pane) {
      case 0:
        if (app->current_artist + 1 < app->library.count) {
          app->current_artist++;
        }
        break;

      case 1:
        if (app->current_album + 1 < app->library.data[app->current_artist].count) {
          app->current_album++;
        }
        break;

      case 2:
        if (app->current_song + 1 <
            app->library.data[app->current_artist].data[app->current_album].songs.count) {
          app->current_song++;
        }
        break;
      }
      break;

    case 'k':
      switch (app->current_pane) {
      case 0:
        if (app->current_artist > 0) {
          app->current_artist--;
        }
        break;

      case 1:
        if (app->current_album > 0) {
          app->current_album--;
        }
        break;

      case 2:
        if (app->current_song > 0) {
          app->current_song--;
        }
        break;
      }
      break;

    case 'h':
      if (app->current_pane > 0) {
        app->current_pane--;
      }
      break;

    case 'l':
      if (app->current_artist < app->library.count) {
        if (app->current_pane == 0) {
          app->current_pane++;
        } else if (app->current_pane == 1) {
          Artist *artist = &app->library.data[app->current_artist];
          if (app->current_album < artist->count) {
            Album *album = &artist->data[app->current_album];
            if (album->ready && app->current_song < album->songs.count) {
              app->current_pane++;
            }
          }
        }
      }
      break;

    case 't':
      app->misc_buffer.count = 0;

      if (app->current_pane == 1) {
        Artist *artist = &app->library.data[app->current_artist];
        Album *album = &artist->data[app->current_album];
        if (album->ready) {
          bool success = true;

          buffer_push_str(&app->misc_buffer, artist->name);
          list_append(&app->misc_buffer, '/');
          buffer_push_str(&app->misc_buffer, album->name);
          list_append(&app->misc_buffer, '/');

          size_t start = app->misc_buffer.count;
          for (size_t i = 0; i < album->songs.count && success; i++) {
            app->misc_buffer.count = start;

            Song *song = &album->songs.data[i];
            buffer_push_str(&app->misc_buffer, song->path);
            list_append(&app->misc_buffer, '\0');

            success = tag_file(app->misc_buffer.data, artist->name, album->name, song->name);
          }

          if (success) {
            app_set_status(app, STATUS_SUCCESS, "Tagged album '" StrFmt "'", StrArg(album->name));
          } else {
            app_set_status(app, STATUS_ERROR, "Error: could not tag album '" StrFmt "'",
                           StrArg(album->name));
          }
        }
      } else if (app->current_pane == 2) {
        Artist *artist = &app->library.data[app->current_artist];
        Album *album = &artist->data[app->current_album];

        Song *song = &album->songs.data[app->current_song];

        buffer_push_str(&app->misc_buffer, artist->name);
        list_append(&app->misc_buffer, '/');
        buffer_push_str(&app->misc_buffer, album->name);
        list_append(&app->misc_buffer, '/');
        buffer_push_str(&app->misc_buffer, song->path);
        list_append(&app->misc_buffer, '\0');

        if (tag_file(app->misc_buffer.data, artist->name, album->name, song->name)) {
          app_set_status(app, STATUS_SUCCESS, "Tagged song '" StrFmt "'", StrArg(song->name));
        } else {
          app_set_status(app, STATUS_ERROR, "Error: could not tag song '" StrFmt "'",
                         StrArg(song->name));
        }
      }
      break;

    case 10:
      if (app->mpd) {
        app->misc_buffer.count = 0;

        if (app->current_pane == 1) {
          Artist *artist = &app->library.data[app->current_artist];
          Album *album = &artist->data[app->current_album];
          if (album->ready && album->songs.count > 0) {
            buffer_push_str(&app->misc_buffer, artist->name);
            list_append(&app->misc_buffer, '/');
            buffer_push_str(&app->misc_buffer, album->name);
            list_append(&app->misc_buffer, '/');

            mpd_command_list_begin(app->mpd, true);
            mpd_send_clear(app->mpd);

            size_t start = app->misc_buffer.count;
            for (size_t i = 0; i < album->songs.count; i++) {
              app->misc_buffer.count = start;
              buffer_push_str(&app->misc_buffer, album->songs.data[i].path);
              list_append(&app->misc_buffer, '\0');

              mpd_send_add(app->mpd, app->misc_buffer.data);
            }

            mpd_send_play(app->mpd);
            mpd_command_list_end(app->mpd);
            mpd_response_finish(app->mpd);

            if (app_check_mpd_state(app)) {
              app_set_status(app, STATUS_SUCCESS, "Loaded %zu song%s", album->songs.count,
                             album->songs.count == 1 ? "" : "s");
            }
          }
        } else if (app->current_pane == 2) {
          Artist *artist = &app->library.data[app->current_artist];
          Album *album = &artist->data[app->current_album];
          if (album->ready) {
            buffer_push_str(&app->misc_buffer, artist->name);
            list_append(&app->misc_buffer, '/');
            buffer_push_str(&app->misc_buffer, album->name);
            list_append(&app->misc_buffer, '/');
            buffer_push_str(&app->misc_buffer, album->songs.data[app->current_song].path);
            list_append(&app->misc_buffer, '\0');

            mpd_command_list_begin(app->mpd, true);
            mpd_send_clear(app->mpd);
            mpd_send_add(app->mpd, app->misc_buffer.data);
            mpd_send_play(app->mpd);
            mpd_command_list_end(app->mpd);
            mpd_response_finish(app->mpd);

            if (app_check_mpd_state(app)) {
              app_set_status(app, STATUS_SUCCESS, "Loaded 1 song");
            }
          }
        }
      }
      break;

    case ' ':
      if (app->mpd) {
        mpd_run_toggle_pause(app->mpd);
        app_check_mpd_state(app);
      }
      break;

    case 'n':
      if (app->mpd) {
        mpd_run_next(app->mpd);
        app_check_mpd_state(app);
      }
      break;

    case 'p':
      if (app->mpd) {
        mpd_run_previous(app->mpd);
        app_check_mpd_state(app);
      }
      break;

    case ',':
      if (app->mpd) {
        mpd_run_seek_current(app->mpd, -5.0, true);
        app_check_mpd_state(app);
      }
      break;

    case '.':
      if (app->mpd) {
        mpd_run_seek_current(app->mpd, 5.0, true);
        app_check_mpd_state(app);
      }
      break;
    }
  }
}

// Main
int main(void) {
  static App app = {0};
  app_init(&app);
  app_loop(&app);
  app_exit(&app);
}
