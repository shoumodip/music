#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <assert.h>
#include <pthread.h>
#include <sys/wait.h>

#include <mpd/client.h>
#include <raylib.h>

#include "fonts/Roboto-Regular.c"

// GUI
#define FONT_SIZE 18
#define FONT_PAD (FONT_SIZE / 2.0)
#define ROW_SIZE (FONT_SIZE * 2)

#define ERROR_COLOR GetColor(0xEA6962FF)
#define SUCCESS_COLOR GetColor(0xA9B665FF)

#define HOVER_COLOR GetColor(0x45403DFF)
#define BORDER_COLOR GetColor(0x5A524CFF)
#define DISABLED_COLOR GetColor(0x928374FF)
#define BACKGROUND_COLOR GetColor(0x282828FF)
#define FOREGROUND_COLOR GetColor(0xD4BE98FF)
#define STATUSLINE_COLOR GetColor(0x202020FF)

// Comparisons
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

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
  char *data;
  size_t size;
} Str;

Str str_from_cstr(char *data) {
  if (!data) {
    return (Str){0};
  }
  return (Str){.data = data, .size = strlen(data)};
}

char *str_to_cstr(Str str) {
  str.data[str.size] = '\0';
  return str.data;
}

bool str_match(Str str, const char *pred) {
  return str.size == strlen(pred) && !memcmp(pred, str.data, str.size);
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

void buffer_push_string(Buffer *buffer, const char *cstr) {
  list_append_many(buffer, cstr, strlen(cstr));
}

void buffer_push_number(Buffer *buffer, size_t number) {
  char temp[32];
  snprintf(temp, sizeof(temp), "%zu", number);
  buffer_push_string(buffer, temp);
}

// Album
typedef struct {
  char *value;
  bool ready;
} Link;

typedef struct {
  Link *data;
  size_t count;
  size_t capacity;
} Links;

typedef struct {
  char *name;
  char *path;
} Song;

typedef struct {
  Song *data;
  size_t count;
  size_t capacity;
} Songs;

typedef struct {
  char *name;
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
  char *name;

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
      if (str_match(str, link->value)) {
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

  char *config;
  size_t pending;
} Library;

void library_free(Library *library) {
  for (size_t i = 0; i < library->count; i++) {
    artist_free(&library->data[i]);
  }
  UnloadFileText(library->config);
  list_free(library);
}

Error library_parse(Library *library) {
  Album *album = NULL;
  Artist *artist = NULL;

  Str contents = str_from_cstr(library->config);
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
      artist->name = str_to_cstr(line);

      album = NULL;
      break;

    case 1:
      if (artist == NULL) {
        return (Error){.line = row, .message = "encountered album without an artist"};
      }

      list_append(artist, (Album){0});
      album = &artist->data[artist->count - 1];
      album->name = str_to_cstr(line);
      break;

    case 2:
      if (album == NULL) {
        return (Error){.line = row, .message = "encountered song/link without an album"};
      } else {
        Str name = str_trim(str_split(&line, '@'), ' ');
        char *value = str_to_cstr(str_trim(line, ' '));

        if (name.size == 0) {
          Link link = {
            .ready = false,
            .value = value,
          };

          list_append(&album->links, link);
        } else {
          Song song = {
            .name = str_to_cstr(name),
            .path = value,
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
  char *start = contents.data;
  if (start) {
    while (contents.size > 0) {
      Str link = str_split(&contents, '\n');
      for (size_t i = 0; i < library->count; i++) {
        if (artist_mark_link(&library->data[i], link)) {
          break;
        }
      }
    }
    UnloadFileText(start);

    for (size_t i = 0; i < library->count; i++) {
      Artist *artist = &library->data[i];
      for (size_t j = 0; j < artist->count; j++) {
        album_mark_ready(&artist->data[j]);
      }
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
          fprintf(f, "%s\n", link->value);
        }
      }
    }
  }

  fclose(f);
  return true;
}

// Popups
typedef enum {
  POPUP_STARTED,
  POPUP_DOWNLOAD_OK,
  POPUP_DOWNLOAD_ERROR,
  POPUP_CONFIG_ERROR,
  POPUP_GENERAL_ERROR,
} PopupType;

Color popup_type_color(PopupType type) {
  if (type == POPUP_STARTED || type == POPUP_DOWNLOAD_OK) {
    return SUCCESS_COLOR;
  }

  return ERROR_COLOR;
}

typedef struct {
  PopupType type;
  size_t number;
  const char *string;

  float lifetime;
} Popup;

void popup_render(Popup *popup, Buffer *buffer) {
  buffer->count = 0;
  switch (popup->type) {
  case POPUP_STARTED:
    buffer_push_number(buffer, popup->number);
    list_append(buffer, ' ');
    buffer_push_string(buffer, popup->string);
    if (popup->number != 1) {
      list_append(buffer, 's');
    }
    buffer_push_string(buffer, " started");
    break;

  case POPUP_DOWNLOAD_OK:
    buffer_push_string(buffer, "Downloaded ");
    buffer_push_string(buffer, popup->string);
    break;

  case POPUP_DOWNLOAD_ERROR:
    buffer_push_string(buffer, "Could not download ");
    buffer_push_string(buffer, popup->string);
    break;

  case POPUP_CONFIG_ERROR:
    buffer_push_string(buffer, popup->string);
    buffer_push_string(buffer, " in line ");
    buffer_push_number(buffer, popup->number);
    break;

  case POPUP_GENERAL_ERROR:
    buffer_push_string(buffer, popup->string);
    break;
  }
  list_append(buffer, '\0');
}

#define POPUP_SLIDEIN 0.1
#define POPUP_LIFETIME 4.0

#define POPUPS_CAPACITY 20

#define popups_get(p, index)                                                                       \
  (assert(index < (p)->count), &(p)->items[((p)->begin + index) % POPUPS_CAPACITY])

#define popups_first(p) popups_get((p), 0)
#define popups_last(p) popups_get((p), (p)->count - 1)

typedef struct {
  Popup items[POPUPS_CAPACITY];
  size_t begin;
  size_t count;
  float slide;
} Popups;

void popups_push(Popups *popups, PopupType type, size_t number, const char *string) {
  if (popups->count < POPUPS_CAPACITY) {
    if (popups->begin == 0) {
      popups->begin = POPUPS_CAPACITY - 1;
    } else {
      popups->begin -= 1;
    }
    popups->count += 1;
    popups->slide += POPUP_SLIDEIN;

    Popup *p = popups_first(popups);
    p->type = type;
    p->number = number;
    p->string = string;
    p->lifetime = POPUP_LIFETIME + popups->slide;
  }
}

// App
void scroll_clamp(float *scroll, float wheel, size_t count, size_t height) {
  *scroll = min(max(*scroll - wheel, 0), max((float)count * ROW_SIZE - height, 0));
}

typedef struct {
  Library library;
  Popups popups;

  pid_t download_process;
  pthread_t download_thread;

  struct mpd_connection *mpd;

  Font font;
  int font_sizes[127 - 32];
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
    execvp(*args, args);
    return false;
  }

  int status = 0;
  if (waitpid(*process, &status, 0) == -1) {
    return false;
  }

  return WEXITSTATUS(status) == 0;
}

void *app_downloader(void *arg) {
  App *app = arg;
  popups_push(&app->popups, POPUP_STARTED, app->library.pending, "Download");

  Buffer buffer = {0};
  for (size_t i = 0; i < app->library.count; i++) {
    Artist *artist = &app->library.data[i];

    buffer.count = 0;
    buffer_push_string(&buffer, artist->name);
    list_append(&buffer, '/');

    size_t start = buffer.count;
    for (size_t j = 0; app->library.count && j < artist->count; j++) {
      Album *album = &artist->data[j];

      buffer.count = start;
      buffer_push_string(&buffer, album->name);
      buffer_push_string(&buffer, "/%(title)s.%(ext)s");
      list_append(&buffer, '\0');

      for (size_t k = 0; app->library.count && k < album->links.count; k++) {
        Link *link = &album->links.data[k];
        if (link->ready) {
          continue;
        }

        char *const args[] = {
          "yt-dlp", "-x", "-o", buffer.data, link->value, NULL,
        };
        bool status = app_execute(app, &app->download_process, args);

        if (app->library.count) {
          app->download_process = 0;
          if (status) {
            link->ready = true;
            album_mark_ready(album);
            popups_push(&app->popups, POPUP_DOWNLOAD_OK, 0, link->value);
          } else {
            popups_push(&app->popups, POPUP_DOWNLOAD_ERROR, 0, link->value);
          }
        }
      }
    }
  }

  list_free(&buffer);
  return NULL;
}

bool app_mpd_connect(App *app) {
  if (app->mpd) {
    mpd_connection_free(app->mpd);
  }

  app->mpd = mpd_connection_new(NULL, 0, 0);
  if (!app->mpd || mpd_connection_get_error(app->mpd) != MPD_ERROR_SUCCESS) {
    app->mpd = NULL;
    popups_push(&app->popups, POPUP_GENERAL_ERROR, 0, "Could not connect to MPD");
    return false;
  }

  return true;
}

bool app_mpd_check_error(App *app) {
  if (!app->mpd) {
    return app_mpd_connect(app);
  }

  if (mpd_connection_get_error(app->mpd) != MPD_ERROR_SUCCESS) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", mpd_connection_get_error_message(app->mpd));
    popups_push(&app->popups, POPUP_GENERAL_ERROR, 0, buffer);

    if (!mpd_connection_clear_error(app->mpd)) {
      app_mpd_connect(app);
    }

    return false;
  }

  return true;
}

enum mpd_state app_mpd_get_state(App *app) {
  if (app->mpd) {
    struct mpd_status *status = mpd_run_status(app->mpd);
    if (status) {
      return mpd_status_get_state(status);
    }
  }

  app_mpd_check_error(app);
  return MPD_STATE_UNKNOWN;
}

void app_mpd_load_song(App *app, Artist *artist, Album *album, Song *song, Buffer *buffer) {
  if (!app->mpd) {
    return;
  }

  buffer->count = 0;
  buffer_push_string(buffer, artist->name);
  list_append(buffer, '/');
  buffer_push_string(buffer, album->name);
  list_append(buffer, '/');
  buffer_push_string(buffer, song->path);
  list_append(buffer, '\0');

  mpd_command_list_begin(app->mpd, true);
  mpd_send_clear(app->mpd);
  mpd_send_add(app->mpd, buffer->data);
  mpd_send_play(app->mpd);
  mpd_command_list_end(app->mpd);
  mpd_response_finish(app->mpd);

  if (app_mpd_check_error(app)) {
    popups_push(&app->popups, POPUP_STARTED, 1, "Song");
  }
}

void app_mpd_load_album(App *app, Artist *artist, Album *album, Buffer *buffer) {
  if (!app->mpd || album->songs.count == 0) {
    return;
  }

  buffer->count = 0;
  buffer_push_string(buffer, artist->name);
  list_append(buffer, '/');
  buffer_push_string(buffer, album->name);
  list_append(buffer, '/');

  mpd_command_list_begin(app->mpd, true);
  mpd_send_clear(app->mpd);

  size_t start = buffer->count;
  for (size_t i = 0; i < album->songs.count; i++) {
    buffer->count = start;
    buffer_push_string(buffer, album->songs.data[i].path);
    list_append(buffer, '\0');

    mpd_send_add(app->mpd, buffer->data);
  }

  mpd_send_play(app->mpd);
  mpd_command_list_end(app->mpd);
  mpd_response_finish(app->mpd);

  if (app_mpd_check_error(app)) {
    popups_push(&app->popups, POPUP_STARTED, album->songs.count, "Song");
  }
}

void app_init(App *app) {
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "Music");
  SetExitKey(KEY_Q);
  SetTargetFPS(60);

  app->font = LoadFontFromMemory(".ttf", font, font_len, FONT_SIZE, 0, 0);
  for (char ch = 32; ch < 127; ch++) {
    GlyphInfo info = GetGlyphInfo(app->font, ch);
    app->font_sizes[ch - 32] = info.advanceX;
  }

  app->library.config = LoadFileText(".config");
  Error error = library_parse(&app->library);
  if (error.message) {
    library_free(&app->library);
    popups_push(&app->popups, POPUP_CONFIG_ERROR, error.line, error.message);
    return;
  }

  library_mark_links(&app->library, str_from_cstr(LoadFileText(".links")));
  if (app->library.pending) {
    if (pthread_create(&app->download_thread, NULL, app_downloader, app)) {
      popups_push(&app->popups, POPUP_GENERAL_ERROR, 0, "Error: could not start downloader thread");
    }
  }

  app_mpd_connect(app);
}

void app_exit(App *app) {
  UnloadFont(app->font);
  CloseWindow();

  if (app->mpd) {
    mpd_connection_free(app->mpd);
  }

  library_save_links(&app->library, ".links");
  library_free(&app->library);

  if (app->download_process != 0) {
    kill(app->download_process, SIGKILL);
  }

  if (app->download_thread) {
    pthread_join(app->download_thread, NULL);
  }
}

size_t app_fit_text(App *app, Str str, int bound, Buffer *buffer, size_t *real_size) {
  int size = 0;
  for (size_t i = 0; i < str.size; i++) {
    int final = size + app->font_sizes[str.data[i] - 32];
    if (final >= bound - 2 * FONT_PAD) {
      str.size = i;
      break;
    }
    size = final;
  }

  if (real_size) {
    *real_size = size;
  }

  return str.size;
}

void app_draw_text(App *app, Rectangle rect, Str str, int bound, Buffer *buffer, Color color) {
  Vector2 position = {
    rect.x + FONT_PAD,
    rect.y + (rect.height - FONT_SIZE) / 2.0,
  };
  size_t end = app_fit_text(app, str, bound, buffer, NULL);

  buffer->count = 0;
  list_append_many(buffer, str.data, end);
  list_append(buffer, '\0');

  DrawTextEx(app->font, buffer->data, position, FONT_SIZE, 0, color);
}

bool app_draw_name_button(App *app, Rectangle rect, char *name, Buffer *buffer, Vector2 mouse) {
  bool hover = CheckCollisionPointRec(mouse, rect);
  if (hover) {
    DrawRectangleRec(rect, HOVER_COLOR);
  }

  app_draw_text(app, rect, str_from_cstr(name), rect.width, buffer, FOREGROUND_COLOR);
  return hover;
}

bool app_draw_play_button(Rectangle rect, Vector2 mouse, enum mpd_state state) {
  Color color = state > 1 ? FOREGROUND_COLOR : DISABLED_COLOR;
  if (state == MPD_STATE_PLAY) {
    DrawRectangle(rect.x, rect.y, rect.width / 3, rect.height, color);
    DrawRectangle(rect.x + rect.width * 2 / 3, rect.y, rect.width / 3, rect.height, color);
  } else {
    Vector2 a = {rect.x, rect.y};
    Vector2 b = {rect.x, rect.y + rect.height};
    Vector2 c = {rect.x + rect.width, rect.y + rect.height / 2};
    DrawTriangle(a, b, c, color);
  }

  return CheckCollisionPointRec(mouse, rect) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool app_draw_next_button(Rectangle rect, Vector2 mouse, enum mpd_state state, bool next) {
  int thick = 2;
  int padding = 2;

  Color color = state > 1 ? FOREGROUND_COLOR : DISABLED_COLOR;
  if (next) {
    Vector2 a = {rect.x + padding, rect.y + padding};
    Vector2 b = {rect.x + padding, rect.y + rect.height - padding};
    Vector2 c = {rect.x + rect.width - padding, rect.y + rect.height / 2};
    DrawTriangle(a, b, c, color);

    Vector2 start = {rect.x + rect.width - thick / 2.0 - padding, rect.y + padding};
    Vector2 end = {rect.x + rect.width - thick / 2.0 - padding, rect.y + rect.height - padding};
    DrawLineEx(start, end, thick, color);
  } else {
    Vector2 a = {rect.x + rect.width - padding, rect.y + padding};
    Vector2 b = {rect.x + padding, rect.y + rect.height / 2};
    Vector2 c = {rect.x + rect.width - padding, rect.y + rect.height - padding};
    DrawTriangle(a, b, c, color);

    Vector2 start = {rect.x + padding, rect.y + padding};
    Vector2 end = {rect.x + padding, rect.y + rect.height - padding};
    DrawLineEx(start, end, thick, color);
  }

  return CheckCollisionPointRec(mouse, rect) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool app_draw_seek_button(Rectangle rect, Vector2 mouse, enum mpd_state state, bool forward) {
  int thick = 2;
  int padding = 2;

  Vector2 a, b, c;
  if (forward) {
    a = (Vector2){rect.x + padding, rect.y + padding};
    b = (Vector2){rect.x + rect.width / 2, rect.y + rect.height / 2};
    c = (Vector2){rect.x + padding, rect.y + rect.height - padding};
  } else {
    a = (Vector2){rect.x + rect.width / 2, rect.y + padding};
    b = (Vector2){rect.x + padding, rect.y + rect.height / 2};
    c = (Vector2){rect.x + rect.width / 2, rect.y + rect.height - padding};
  }

  Color color = state > 1 ? FOREGROUND_COLOR : DISABLED_COLOR;
  DrawLineEx(a, b, thick, color);
  DrawLineEx(b, c, thick, color);

  a.x += rect.width / 2 - padding;
  b.x += rect.width / 2 - padding;
  c.x += rect.width / 2 - padding;
  DrawLineEx(a, b, thick, color);
  DrawLineEx(b, c, thick, color);

  return CheckCollisionPointRec(mouse, rect) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

void app_draw_popups(App *app, int width, int height, Buffer *buffer) {
  float dt = GetFrameTime();
  if (app->popups.slide > 0) {
    app->popups.slide -= dt;
  }
  if (app->popups.slide < 0) {
    app->popups.slide = 0;
  }

  for (size_t i = 0; i < app->popups.count; ++i) {
    Popup *popup = popups_get(&app->popups, i);
    popup->lifetime -= dt;

    popup_render(popup, buffer);

    size_t size, end;
    end = app_fit_text(app, str_from_cstr(buffer->data), width / 3.0 - 2 * FONT_PAD, buffer, &size);
    buffer->data[end] = '\0';

    Rectangle boundary = {
      .x = width - size - 3 * FONT_PAD,
      .y = height - (i + 1 - app->popups.slide / POPUP_SLIDEIN) * (FONT_SIZE + 3 * FONT_PAD),
      .width = size + 2 * FONT_PAD,
      .height = FONT_SIZE + 2 * FONT_PAD,
    };

    float t = popup->lifetime / POPUP_LIFETIME;
    float alpha = t >= 0.5f ? 1.0f : t / 0.5f;

    DrawRectangleRounded(boundary, 0.3, 20, ColorAlpha(popup_type_color(popup->type), alpha));
    Vector2 position = {
      .x = boundary.x + boundary.width / 2 - size / 2.0,
      .y = boundary.y + boundary.height / 2 - FONT_SIZE / 2.0,
    };
    DrawTextEx(app->font, buffer->data, position, FONT_SIZE, 0,
               ColorAlpha(BACKGROUND_COLOR, alpha));
  }

  while (app->popups.count > 0 && popups_last(&app->popups)->lifetime <= 0) {
    app->popups.count--;
  }
}

void app_loop(App *app) {
  Buffer buffer = {0};

  Album *current_album = NULL;
  Artist *current_artist = NULL;

  float scroll[3] = {0};

  enum mpd_state state = MPD_STATE_UNKNOWN;
  if (app->mpd) {
    app_mpd_get_state(app);
  }

  float clock = 0.0;
  while (!WindowShouldClose()) {
    int width = GetScreenWidth();
    int height = GetScreenHeight() - ROW_SIZE;
    float wheel = GetMouseWheelMove() * 20;
    Vector2 mouse = GetMousePosition();

    BeginDrawing();
    {
      ClearBackground(BACKGROUND_COLOR);
      DrawLine(width * 0 / 3, 0, width * 0 / 3, height, BORDER_COLOR);
      DrawLine(width * 1 / 3, 0, width * 1 / 3, height, BORDER_COLOR);
      DrawLine(width * 2 / 3, 0, width * 2 / 3, height, BORDER_COLOR);

      // Artists
      {
        if (wheel != 0.0 && mouse.x >= width * 0.0 / 3 && mouse.x < width * 1.0 / 3) {
          scroll_clamp(&scroll[0], wheel, app->library.count, height);
        }

        for (size_t i = 0; i < app->library.count; i++) {
          Artist *artist = &app->library.data[i];
          Rectangle rect = {
            width * 0.0 / 3,
            i * ROW_SIZE - scroll[0],
            width / 3.0,
            ROW_SIZE,
          };

          if (app_draw_name_button(app, rect, artist->name, &buffer, mouse)) {
            current_artist = artist;
          }
        }
      }

      if (current_artist) {
        if (wheel != 0.0 && mouse.x >= width * 1.0 / 3 && mouse.x < width * 2.0 / 3) {
          scroll_clamp(&scroll[1], wheel, current_artist->count, height);
        }

        // Albums
        for (size_t i = 0, row = 0; i < current_artist->count; i++) {
          Album *album = &current_artist->data[i];
          Rectangle rect = {
            width * 1.0 / 3,
            row++ * ROW_SIZE - scroll[1],
            width / 3.0,
            ROW_SIZE,
          };

          if (app_draw_name_button(app, rect, album->name, &buffer, mouse)) {
            current_album = album;
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && album->ready) {
              app_mpd_load_album(app, current_artist, current_album, &buffer);
            }
          }
        }

        // Songs
        if (current_album) {
          if (current_album->ready) {
            if (wheel != 0.0 && mouse.x >= width * 2.0 / 3 && mouse.x < width * 3.0 / 3) {
              scroll_clamp(&scroll[2], wheel, current_album->songs.count, height);
            }

            for (size_t i = 0; i < current_album->songs.count; i++) {
              Song *song = &current_album->songs.data[i];
              Rectangle rect = {
                width * 2.0 / 3,
                i * ROW_SIZE - scroll[2],
                width / 3.0,
                ROW_SIZE,
              };

              if (app_draw_name_button(app, rect, song->name, &buffer, mouse)) {
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                  app_mpd_load_song(app, current_artist, current_album, song, &buffer);
                }
              }
            }
          } else {
            Rectangle rect = {
              width * 2.0 / 3,
              0.0,
              width / 3.0,
              ROW_SIZE,
            };

            app_draw_text(app, rect, str_from_cstr("Not Ready"), width / 3, &buffer,
                          FOREGROUND_COLOR);
          }
        }
      }

      // Popups
      app_draw_popups(app, width, height, &buffer);

      // Status
      DrawRectangle(0, height, width, ROW_SIZE, STATUSLINE_COLOR);
      if (app->mpd) {
        clock += GetFrameTime();
        if (clock >= 1.0) {
          state = app_mpd_get_state(app);
          clock = 0.0;
        }

        Rectangle rect = {
          0,
          height + FONT_PAD,
          ROW_SIZE - 2 * FONT_PAD,
          ROW_SIZE - 2 * FONT_PAD,
        };

        rect.x = (width - rect.width) / 2 + 2 * ROW_SIZE;
        if (app_draw_seek_button(rect, mouse, state, true) || IsKeyReleased(KEY_PERIOD)) {
          mpd_run_seek_current(app->mpd, 5.0, true);
          app_mpd_check_error(app);
          state = app_mpd_get_state(app);
        }

        rect.x -= ROW_SIZE;
        if (app_draw_next_button(rect, mouse, state, true) || IsKeyReleased(KEY_N)) {
          mpd_run_next(app->mpd);
          app_mpd_check_error(app);
          state = app_mpd_get_state(app);
        }

        rect.x -= ROW_SIZE;
        if (app_draw_play_button(rect, mouse, state) || IsKeyReleased(KEY_SPACE)) {
          mpd_run_toggle_pause(app->mpd);
          app_mpd_check_error(app);
          state = app_mpd_get_state(app);
        }

        rect.x -= ROW_SIZE;
        if (app_draw_next_button(rect, mouse, state, false) || IsKeyReleased(KEY_P)) {
          mpd_run_previous(app->mpd);
          app_mpd_check_error(app);
          state = app_mpd_get_state(app);
        }

        rect.x -= ROW_SIZE;
        if (app_draw_seek_button(rect, mouse, state, false) || IsKeyReleased(KEY_COMMA)) {
          mpd_run_seek_current(app->mpd, -5.0, true);
          app_mpd_check_error(app);
          state = app_mpd_get_state(app);
        }
      }
    }
    EndDrawing();
  }

  list_free(&buffer);
}

// Main
int main(int argc, char **argv) {
  if (argc > 1) {
    if (chdir(argv[1]) < 0) {
      fprintf(stderr, "Error: could not change directory to '%s'\n", argv[1]);
      exit(1);
    }
  }

  static App app = {0};
  app_init(&app);
  app_loop(&app);
  app_exit(&app);
}
