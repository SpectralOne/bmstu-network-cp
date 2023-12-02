#include "log.h"

static struct {
  void *udata;
  int level;
  bool quiet;
} Logger;

static const char *level_strings[] = {"TRACE", "DEBUG", "INFO",
                                      "WARN",  "ERROR", "FATAL"};

static const char *level_colors[] = {"\x1b[94m", "\x1b[36m", "\x1b[32m",
                                     "\x1b[33m", "\x1b[31m", "\x1b[35m"};

static void stdout_callback(log_event_t *ev) {
  char buf[16];
  buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
  fprintf(ev->udata, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ", buf,
          level_colors[ev->level], level_strings[ev->level], ev->file,
          ev->line);
  vfprintf(ev->udata, ev->fmt, ev->ap);
  fprintf(ev->udata, "\n");
  fflush(ev->udata);
}

void log_set_level(const int level) { Logger.level = level; }

void log_set_quiet(const bool enable) { Logger.quiet = enable; }

static void init_event(log_event_t *ev, void *udata) {
  if (!ev->time) {
    time_t t = time(NULL);
    ev->time = localtime(&t);
  }
  ev->udata = udata;
}

void log_log(int level, const char *file, int line, const char *fmt, ...) {
  log_event_t ev = {
      .fmt = fmt,
      .file = file,
      .line = line,
      .level = level,
  };

  if (!Logger.quiet && level >= Logger.level) {
    init_event(&ev, stderr);
    va_start(ev.ap, fmt);
    stdout_callback(&ev);
    va_end(ev.ap);
  }
}