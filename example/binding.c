/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2015  Benjamin Fleischer

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` clock_ll.c -o clock_ll
*/

#define FUSE_USE_VERSION 26

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#include <node_api.h>
#include <napi-macros.h>

static char clock_str[32] = "Hello World!\n";
static const char *clock_name = "clock";

static int hello_stat(fuse_ino_t ino, struct stat *stbuf)
{
  stbuf->st_ino = ino;
  switch (ino) {
  case 1:
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    break;

  case 2:
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(clock_str);
    break;

  default:
    return -1;
  }
  return 0;
}

static void clock_ll_getattr(fuse_req_t req, fuse_ino_t ino,
           struct fuse_file_info *fi)
{
  struct stat stbuf;

  (void) fi;

  memset(&stbuf, 0, sizeof(stbuf));
  if (hello_stat(ino, &stbuf) == -1)
    fuse_reply_err(req, ENOENT);
  else
    fuse_reply_attr(req, &stbuf, 1.0);
}

static void clock_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  struct fuse_entry_param e;

  if (parent != 1 || strcmp(name, clock_name) != 0)
    fuse_reply_err(req, ENOENT);
  else {
    memset(&e, 0, sizeof(e));
    e.ino = 2;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    hello_stat(e.ino, &e.attr);

    fuse_reply_entry(req, &e);
  }
}

struct dirbuf {
  char *p;
  size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
           fuse_ino_t ino)
{
  struct stat stbuf;
  size_t oldsize = b->size;
  b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
        char *newp = realloc(b->p, b->size);
        if (!newp) {
            fprintf(stderr, "*** fatal error: cannot allocate memory\n");
            abort();
        }
  b->p = newp;
  memset(&stbuf, 0, sizeof(stbuf));
  stbuf.st_ino = ino;
  fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
        b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
           off_t off, size_t maxsize)
{
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off,
              min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

static void clock_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
           off_t off, struct fuse_file_info *fi)
{
  (void) fi;

  if (ino != 1)
    fuse_reply_err(req, ENOTDIR);
  else {
    struct dirbuf b;

    memset(&b, 0, sizeof(b));
    dirbuf_add(req, &b, ".", 1);
    dirbuf_add(req, &b, "..", 1);
    dirbuf_add(req, &b, clock_name, 2);
    reply_buf_limited(req, b.p, b.size, off, size);
    free(b.p);
  }
}

static void clock_ll_open(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi)
{
  if (ino != 2)
    fuse_reply_err(req, EISDIR);
  else if ((fi->flags & 3) != O_RDONLY)
    fuse_reply_err(req, EACCES);
  else
    fuse_reply_open(req, fi);
}

static void clock_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
        off_t off, struct fuse_file_info *fi)
{
  (void) fi;

  assert(ino == 2);
  reply_buf_limited(req, clock_str, strlen(clock_str), off, size);
}

static struct fuse_lowlevel_ops clock_ll_oper = {
  .lookup   = clock_ll_lookup,
  .getattr  = clock_ll_getattr,
  .readdir  = clock_ll_readdir,
  .open   = clock_ll_open,
  .read   = clock_ll_read,
};

static void *clock_update(void *arg)
{
  struct fuse_session *se = (struct fuse_session *)arg;
  struct fuse_chan *ch = fuse_session_next_chan(se, NULL);

  struct timeval tp;
  struct tm *t;

  while (!fuse_session_exited(se)) {
    gettimeofday(&tp, 0);
    t = localtime(&tp.tv_sec);
    snprintf(clock_str, sizeof(clock_str), "%02d:%02d:%02d:%d\n", t->tm_hour, t->tm_min, t->tm_sec, tp.tv_usec);

    fuse_lowlevel_notify_inval_inode(ch, 2, 0, 0);
    usleep(250000);
  }

  return NULL;
}

NAPI_METHOD(run_clock_fs) {
  NAPI_ARGV(0)

  char* fake_argv[3] = { "/bin", "mnt", NULL };
  int fake_argc = 2;

  struct fuse_args args = FUSE_ARGS_INIT(fake_argc, fake_argv);
  struct fuse_chan *ch;
  char *mountpoint;
  int err = 0;

  if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
      (ch = fuse_mount(mountpoint, &args)) != NULL) {
    struct fuse_session *se;

    se = fuse_lowlevel_new(&args, &clock_ll_oper,
               sizeof(clock_ll_oper), NULL);
    if (se != NULL) {
      if (fuse_set_signal_handlers(se) != -1) {
        fuse_session_add_chan(se, ch);

        if (!err) {
          pthread_t clock_thread;
          err = pthread_create(&clock_thread, NULL, &clock_update, se);
        }
        if (!err) {
          err = fuse_session_loop(se);
        }

        fuse_remove_signal_handlers(se);
        fuse_session_remove_chan(ch);
      }
      fuse_session_destroy(se);
    }
    fuse_unmount(mountpoint, ch);
  }
  fuse_opt_free_args(&args);
}

NAPI_INIT() {
  NAPI_EXPORT_FUNCTION(run_clock_fs)
}
