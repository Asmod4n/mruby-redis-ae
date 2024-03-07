#include "mrb_ae.h"

static mrb_value
mrb_aeCreateEventLoop(mrb_state *mrb, mrb_value self)
{
  mrb_int setsize = 1024;

  mrb_get_args(mrb, "|i", &setsize);

  if (setsize < INT_MIN||setsize > INT_MAX) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "setsize doesn't fit into int");
  }

  errno = 0;
  aeEventLoop *loop = aeCreateEventLoop((int) setsize);
  if (loop) {
    mrb_data_init(self, loop, &mrb_aeEventLoop_type);
    loop->mrb = mrb;
  } else {
    mrb_sys_fail(mrb, "aeCreateEventLoop");
  }

  return self;
}

static mrb_value
mrb_aeStop(mrb_state *mrb, mrb_value self)
{
  aeStop((aeEventLoop *) DATA_PTR(self));

  return mrb_nil_value();
}

MRB_INLINE void
mrb_aeFileProc(aeEventLoop *eventLoop, int fd, void *clientData, int mask)
{
  mrb_ae_file_callback_data *file_callback_data = (mrb_ae_file_callback_data *) clientData;

  mrb_value argv[] = { file_callback_data->sock, mrb_int_value(eventLoop->mrb, mask) };
  mrb_assert(mrb_type(file_callback_data->block) == MRB_TT_PROC);
  int ai = mrb_gc_arena_save(eventLoop->mrb);
  mrb_yield_argv(eventLoop->mrb, file_callback_data->block, 2, argv);
  mrb_gc_arena_restore(eventLoop->mrb, ai);
}

MRB_INLINE mrb_value
mrb_ae_create_file_callback_data(mrb_state *mrb, mrb_value self, mrb_value sock, int fd, int mask, mrb_value block)
{
  mrb_value mrb_ae_callback_data = mrb_obj_value((struct RObject*)mrb_obj_alloc(mrb, MRB_TT_DATA, mrb_class_get_under(mrb, mrb_class(mrb, self), "FileCallbackData")));

  mrb_value args[] = { self, sock, mrb_int_value(mrb, fd), mrb_int_value(mrb, mask) };

  return mrb_funcall_with_block(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "initialize"), 4, args, block);
}

static mrb_value
mrb_aeCreateFileEvent(mrb_state *mrb, mrb_value self)
{
  mrb_value sock = mrb_nil_value();
  mrb_int mask = AE_READABLE;
  mrb_value block = mrb_nil_value();

  mrb_get_args(mrb, "|oi&", &sock, &mask, &block);

  if (mask < INT_MIN||mask > INT_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "mask doesn't fit into int");
  }

  if (mrb_type(block) != MRB_TT_PROC) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "no block given");
  }

  int fd = 0;

  if (!mrb_nil_p(sock)) {
    mrb_int fd_mrb = mrb_as_int(mrb, sock);
    if (fd_mrb < INT_MIN||fd_mrb > INT_MAX) {
      mrb_raise(mrb, E_ARGUMENT_ERROR, "fd doesn't fit into int");
    }
    fd = fd_mrb;
  }

  mrb_value mrb_ae_callback_data = mrb_ae_create_file_callback_data(mrb, self, sock, fd, mask, block);

  errno = 0;
  int rc = aeCreateFileEvent((aeEventLoop *) DATA_PTR(self), fd, mask, mrb_aeFileProc, DATA_PTR(mrb_ae_callback_data));
  if (rc != AE_OK) {
    mrb_sys_fail(mrb, "aeCreateFileEvent");
  }

  return mrb_ae_callback_data;
}

MRB_INLINE void
mrb_ae_delete_file_callback_data(mrb_state *mrb, mrb_value mrb_ae_callback_data, mrb_value self)
{
  mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@sock"));
  mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@mask"));
  mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@block"));
  mrb_ae_file_callback_data *file_callback_data = (mrb_ae_file_callback_data *) DATA_PTR(mrb_ae_callback_data);
  aeDeleteFileEvent((aeEventLoop *) DATA_PTR(self), file_callback_data->fd, file_callback_data->mask);
  mrb_free(mrb, DATA_PTR(mrb_ae_callback_data));
  mrb_data_init(mrb_ae_callback_data, NULL, NULL);
}

static mrb_value
mrb_aeDeleteFileEvent(mrb_state *mrb, mrb_value self)
{
  mrb_value mrb_ae_file_callback_data;

  mrb_get_args(mrb, "o", &mrb_ae_file_callback_data);

  if (mrb_type(mrb_ae_file_callback_data) == MRB_TT_DATA && DATA_TYPE(mrb_ae_file_callback_data) == &mrb_ae_file_callback_data_type) {
    mrb_ae_delete_file_callback_data(mrb, mrb_ae_file_callback_data, self);
  } else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "expected Ae File callback data");
  }

  return mrb_nil_value();
}

MRB_INLINE int
mrb_aeTimeProc(aeEventLoop *eventLoop, long long id, void *clientData)
{
  mrb_state *mrb = eventLoop->mrb;
  mrb_ae_time_callback_data *time_callback_data = (mrb_ae_time_callback_data *) clientData;
  int ai = mrb_gc_arena_save(mrb);
  mrb_assert(mrb_type(time_callback_data->block) == MRB_TT_PROC);
  mrb_value ret = mrb_yield(mrb, time_callback_data->block, mrb_int_value(mrb, id));
  mrb_int milliseconds = mrb_as_int(mrb, ret);
  mrb_gc_arena_restore(mrb, ai);

  if (milliseconds < INT_MIN||milliseconds > INT_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "milliseconds doesn't fit into int");
  }

  return (int) milliseconds;
}

MRB_INLINE void
mrb_aeEventFinalizerProc(aeEventLoop *eventLoop, void *clientData)
{
  mrb_ae_time_callback_data *time_callback_data = (mrb_ae_time_callback_data *) clientData;

  mrb_assert (mrb_type(time_callback_data->finalizer) == MRB_TT_PROC);
  int ai = mrb_gc_arena_save(eventLoop->mrb);
  mrb_yield_argv(eventLoop->mrb, time_callback_data->finalizer, 0, NULL);
  mrb_gc_arena_restore(eventLoop->mrb, ai);
}

MRB_INLINE mrb_value
mrb_ae_create_time_callback_data(mrb_state *mrb, mrb_value self, mrb_value finalizer, mrb_value block)
{
  mrb_value args[] = { self, finalizer };
  return mrb_funcall_with_block(mrb, mrb_obj_value((struct RObject*)mrb_obj_alloc(mrb, MRB_TT_DATA, mrb_class_get_under(mrb, mrb_class(mrb, self), "TimeCallbackData"))), mrb_intern_lit(mrb, "initialize"), 2, args, block);
}

MRB_INLINE void
mrb_ae_push_time_callback_data(mrb_state *mrb, mrb_value mrb_ae_callback_data, long long id, mrb_value self)
{
  mrb_iv_set(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@id"), mrb_int_value(mrb, id));
  ((mrb_ae_time_callback_data *) DATA_PTR(mrb_ae_callback_data))->id = id;
}

static mrb_value
mrb_aeCreateTimeEvent(mrb_state *mrb, mrb_value self)
{
  mrb_int milliseconds;
  mrb_value finalizer = mrb_nil_value(), block = mrb_nil_value();

  mrb_get_args(mrb, "i|o&", &milliseconds, &finalizer, &block);

  if (milliseconds < LLONG_MIN||milliseconds > LLONG_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "milliseconds doesn't fit into long long");
  }

  if (mrb_type(block) != MRB_TT_PROC) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "no block given");
  }

  aeEventFinalizerProc *finalizerProc = NULL;
  if (mrb_type(finalizer) == MRB_TT_PROC) {
    finalizerProc = mrb_aeEventFinalizerProc;
  }

  mrb_value mrb_ae_callback_data = mrb_ae_create_time_callback_data(mrb, self, finalizer, block);

  errno = 0;
  long long id = aeCreateTimeEvent((aeEventLoop *) DATA_PTR(self), (long long) milliseconds,
          mrb_aeTimeProc, DATA_PTR(mrb_ae_callback_data), finalizerProc);

  if (id != AE_ERR) {
    mrb_ae_push_time_callback_data(mrb, mrb_ae_callback_data, id, self);
  } else {
    mrb_sys_fail(mrb, "aeCreateTimeEvent");
  }

  return mrb_ae_callback_data;
}

MRB_INLINE void
mrb_ae_delete_time_callback_data(mrb_state *mrb, mrb_value mrb_ae_callback_data, mrb_value self)
{
  mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@finalizer"));
  mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@block"));
  mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@id"));
  aeDeleteTimeEvent((aeEventLoop *) DATA_PTR(self), ((mrb_ae_time_callback_data *) DATA_PTR(mrb_ae_callback_data))->id);
  mrb_free(mrb, DATA_PTR(mrb_ae_callback_data));
  mrb_data_init(mrb_ae_callback_data, NULL, NULL);
}

static mrb_value
mrb_aeDeleteTimeEvent(mrb_state *mrb, mrb_value self)
{
  mrb_value mrb_ae_callback_data;

  mrb_get_args(mrb, "o", &mrb_ae_callback_data);

  if (mrb_type(mrb_ae_callback_data) == MRB_TT_DATA && DATA_TYPE(mrb_ae_callback_data) == &mrb_ae_time_callback_data_type) {
    mrb_ae_delete_time_callback_data(mrb, mrb_ae_callback_data, self);
  } else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "expected Ae Time callback data");
  }

  return mrb_int_value(mrb, -1);
}

static mrb_value
mrb_aeProcessEvents(mrb_state *mrb, mrb_value self)
{
  mrb_int flags = AE_ALL_EVENTS;

  mrb_get_args(mrb, "|i", &flags);

  if (flags < INT_MIN||flags > INT_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "flags doesn't fit into int");
  }

  return mrb_int_value(mrb, aeProcessEvents((aeEventLoop *) DATA_PTR(self), (int) flags));
}

static mrb_value
mrb_aeWait(mrb_state *mrb, mrb_value self)
{
  mrb_int fd = 0, mask = AE_READABLE, milliseconds = -1;

  mrb_get_args(mrb, "|iii", &fd, &mask, &milliseconds);

  if (fd < INT_MIN||fd > INT_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "fd doesn't fit into int");
  }

  if (mask < INT_MIN||mask > INT_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "mask doesn't fit into int");
  }

  if (milliseconds < LLONG_MIN||milliseconds > LLONG_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "milliseconds doesn't fit into long long");
  }

  return mrb_int_value(mrb, aeWait((int) fd, (int) mask, (long long) milliseconds));
}

static mrb_value
mrb_aeMain(mrb_state *mrb, mrb_value self)
{
  aeMain((aeEventLoop *) DATA_PTR(self));

  return mrb_nil_value();
}

MRB_INLINE void
mrb_aeBeforeSleepProc(aeEventLoop *eventLoop)
{
  mrb_assert(mrb_type(eventLoop->before_sleep_block) == MRB_TT_PROC);
  int ai = mrb_gc_arena_save(eventLoop->mrb);
  mrb_yield_argv(eventLoop->mrb, eventLoop->before_sleep_block, 0, NULL);
  mrb_gc_arena_restore(eventLoop->mrb, ai);
}

static mrb_value
mrb_aeSetBeforeSleepProc(mrb_state *mrb, mrb_value self)
{
  mrb_value block = mrb_nil_value();

  mrb_get_args(mrb, "&", &block);

  if (mrb_type(block) != MRB_TT_PROC) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "no block given");
  }

  aeEventLoop *loop = (aeEventLoop *) DATA_PTR(self);

  aeSetBeforeSleepProc(loop, mrb_aeBeforeSleepProc);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "before_sleep"), block);

  loop->before_sleep_block = block;

  return self;
}

static mrb_value
mrb_aeGetSetSize(mrb_state *mrb, mrb_value self)
{
  return mrb_int_value(mrb, aeGetSetSize((aeEventLoop *) DATA_PTR(self)));
}

static mrb_value
mrb_aeResizeSetSize(mrb_state *mrb, mrb_value self)
{
  mrb_int setsize;

  mrb_get_args(mrb, "i", &setsize);

  if (setsize < INT_MIN||setsize > INT_MAX) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "setsize doesn't fit into int");
  }

  errno = 0;
  int rc = aeResizeSetSize((aeEventLoop *) DATA_PTR(self), setsize);
  if (rc != AE_OK) {
    mrb_sys_fail(mrb, "aeResizeSetSize");
  }

  return self;
}

static mrb_value
mrb_ae_file_callback_data_init(mrb_state *mrb, mrb_value self)
{
  aeEventLoop *loop;
  mrb_value sock;
  mrb_int fd, mask;
  mrb_value block = mrb_nil_value();

  mrb_get_args(mrb, "doii&", &loop, &mrb_aeEventLoop_type, &sock, &fd, &mask, &block);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@sock"), sock);
  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@mask"), mrb_int_value(mrb, mask));
  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@block"), block);

  mrb_ae_file_callback_data *file_callback_data = (mrb_ae_file_callback_data *) mrb_realloc(mrb, DATA_PTR(self), sizeof(mrb_ae_file_callback_data));
  mrb_data_init(self, file_callback_data, &mrb_ae_file_callback_data_type);
  file_callback_data->loop = loop;
  file_callback_data->sock = sock;
  file_callback_data->fd = fd;
  file_callback_data->mask = mask;
  file_callback_data->block = block;

  return self;
}

static mrb_value
mrb_ae_time_callback_data_init(mrb_state *mrb, mrb_value self)
{
  aeEventLoop *loop;
  mrb_value finalizer = mrb_nil_value();
  mrb_value block = mrb_nil_value();

  mrb_get_args(mrb, "d|o&", &loop, &mrb_aeEventLoop_type, &finalizer, &block);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@finalizer"), finalizer);
  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@block"), block);

  mrb_ae_time_callback_data *time_callback_data = (mrb_ae_time_callback_data *) mrb_realloc(mrb, DATA_PTR(self), sizeof(mrb_ae_time_callback_data));
  mrb_data_init(self, time_callback_data, &mrb_ae_time_callback_data_type);
  time_callback_data->loop = loop;
  time_callback_data->finalizer = finalizer;
  time_callback_data->block = block;

  return self;
}

void
mrb_mruby_redis_ae_gem_init(mrb_state* mrb)
{
  if (sizeof(mrb_int) < sizeof(int)) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "mruby-redis-ae isn't compatible with MRB_INT%S", mrb_int_value(mrb, sizeof(mrb_int) * 8));
  }

  const char *api_name = aeGetApiName();
  mrb_value api_name_str = mrb_str_new_static(mrb, api_name, strlen(api_name));

  struct RClass *ae_class, *ae_file_callback_data_class, *ae_time_callback_data_class;

  ae_class = mrb_define_class(mrb, "RedisAe", mrb->object_class);
  MRB_SET_INSTANCE_TT(ae_class, MRB_TT_DATA);
  mrb_define_const(mrb, ae_class, "OK", mrb_int_value(mrb, AE_OK));
  mrb_define_const(mrb, ae_class, "ERR", mrb_int_value(mrb, AE_ERR));
  mrb_define_const(mrb, ae_class, "NONE", mrb_int_value(mrb, AE_NONE));
  mrb_define_const(mrb, ae_class, "READABLE", mrb_int_value(mrb, AE_READABLE));
  mrb_define_const(mrb, ae_class, "WRITABLE", mrb_int_value(mrb, AE_WRITABLE));
  mrb_define_const(mrb, ae_class, "FILE_EVENTS", mrb_int_value(mrb, AE_FILE_EVENTS));
  mrb_define_const(mrb, ae_class, "TIME_EVENTS", mrb_int_value(mrb, AE_TIME_EVENTS));
  mrb_define_const(mrb, ae_class, "ALL_EVENTS", mrb_int_value(mrb, AE_ALL_EVENTS));
  mrb_define_const(mrb, ae_class, "DONT_WAIT", mrb_int_value(mrb, AE_DONT_WAIT));
  mrb_define_const(mrb, ae_class, "NOMORE", mrb_int_value(mrb, AE_NOMORE));
  mrb_define_const(mrb, ae_class, "API_NAME", api_name_str);
  mrb_define_method(mrb, ae_class, "initialize", mrb_aeCreateEventLoop, MRB_ARGS_NONE());
  mrb_define_method(mrb, ae_class, "stop", mrb_aeStop, MRB_ARGS_NONE());
  mrb_define_method(mrb, ae_class, "create_file_event", mrb_aeCreateFileEvent, MRB_ARGS_OPT(2)|MRB_ARGS_BLOCK());
  mrb_define_method(mrb, ae_class, "delete_file_event", mrb_aeDeleteFileEvent, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, ae_class, "create_time_event", mrb_aeCreateTimeEvent, MRB_ARGS_ARG(1, 1)|MRB_ARGS_BLOCK());
  mrb_define_method(mrb, ae_class, "delete_time_event", mrb_aeDeleteTimeEvent, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, ae_class, "process_events", mrb_aeProcessEvents, MRB_ARGS_OPT(1));
  mrb_define_method(mrb, ae_class, "wait", mrb_aeWait, MRB_ARGS_OPT(3));
  mrb_define_method(mrb, ae_class, "main", mrb_aeMain, MRB_ARGS_NONE());
  mrb_define_alias (mrb, ae_class, "run", "main");
  mrb_define_method(mrb, ae_class, "before_sleep", mrb_aeSetBeforeSleepProc, MRB_ARGS_BLOCK());
  mrb_define_method(mrb, ae_class, "set_size", mrb_aeGetSetSize, MRB_ARGS_NONE());
  mrb_define_method(mrb, ae_class, "set_size=", mrb_aeResizeSetSize, MRB_ARGS_REQ(1));

  ae_file_callback_data_class = mrb_define_class_under(mrb, ae_class, "FileCallbackData", mrb->object_class);
  MRB_SET_INSTANCE_TT(ae_file_callback_data_class, MRB_TT_DATA);
  mrb_define_method(mrb, ae_file_callback_data_class, "initialize", mrb_ae_file_callback_data_init, MRB_ARGS_REQ(3)|MRB_ARGS_BLOCK());

  ae_time_callback_data_class = mrb_define_class_under(mrb, ae_class, "TimeCallbackData", mrb->object_class);
  MRB_SET_INSTANCE_TT(ae_time_callback_data_class, MRB_TT_DATA);
  mrb_define_method(mrb, ae_time_callback_data_class, "initialize", mrb_ae_time_callback_data_init, MRB_ARGS_OPT(1)|MRB_ARGS_BLOCK());
}

void mrb_mruby_redis_ae_gem_final(mrb_state* mrb) {}
