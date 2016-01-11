#include "mrb_ae.h"

static mrb_value
mrb_aeCreateEventLoop(mrb_state *mrb, mrb_value self)
{
  mrb_int setsize;

  int argc = mrb_get_args(mrb, "|i", &setsize);
  if (argc == 0) {
    struct rlimit limit;
    int rc = getrlimit(RLIMIT_NOFILE, &limit);
    assert (rc == 0);
    setsize = limit.rlim_cur;
  }

  if (setsize < INT_MIN||setsize > INT_MAX) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "setsize doesn't fit into int");
  }

  errno = 0;
  aeEventLoop *loop = aeCreateEventLoop(setsize);
  if (loop) {
    loop->mrb = mrb;
    mrb_data_init(self, loop, &mrb_aeEventLoop_type);
  }
  else {
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

static inline void
mrb_aeFileProc(aeEventLoop *eventLoop, int fd, void *clientData, int mask)
{
  mrb_state *mrb = eventLoop->mrb;
  int arena_index = mrb_gc_arena_save(mrb);
  mrb_ae_callback_data *callback_data = (mrb_ae_callback_data *) clientData;

  mrb_value args[2];
  args[0] = callback_data->client_data;
  args[1] = mrb_fixnum_value(mask);
  mrb_yield_argv(mrb, callback_data->block, 2, args);
  mrb_gc_arena_restore(mrb, arena_index);
}

static inline mrb_value
mrb_ae_create_file_callback_data(mrb_state *mrb, mrb_value self, mrb_value client_data, int mask, mrb_value block)
{
  struct RBasic *callback_data_obj = mrb_obj_alloc(mrb, MRB_TT_DATA, mrb_class_get_under(mrb, mrb_class(mrb, self), "CallbackData"));
  mrb_value mrb_ae_callback_data = mrb_obj_value((struct RObject*)callback_data_obj);
  mrb_ae_callback_data = mrb_funcall_with_block(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "initialize"), 1, &client_data, block);
  mrb_iv_set(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "mask"), mrb_fixnum_value(mask));

  return mrb_ae_callback_data;
}

static mrb_value
mrb_aeCreateFileEvent(mrb_state *mrb, mrb_value self)
{
  mrb_value sock = mrb_nil_value();
  mrb_int mask = AE_READABLE;
  mrb_value block = mrb_nil_value();

  mrb_get_args(mrb, "|oi&", &sock, &mask, &block);

  if (mrb_nil_p(block)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "no block given");
  }

  int fd = 0;

  switch(mrb_type(sock)) {
    case MRB_TT_DATA: {
      mrb_value fd_val = mrb_Integer(mrb, sock);
      fd = mrb_fixnum(fd_val);
    } break;
    case MRB_TT_FIXNUM: {
      fd = mrb_fixnum(sock);
    } break;
    case MRB_TT_FALSE:
      break;
    default:
        mrb_raise(mrb, E_ARGUMENT_ERROR, "sock type not supported");
  }

  mrb_value mrb_ae_callback_data = mrb_ae_create_file_callback_data(mrb, self, sock, mask, block);

  errno = 0;
  int rc = aeCreateFileEvent((aeEventLoop *) DATA_PTR(self), fd, mask, mrb_aeFileProc, DATA_PTR(mrb_ae_callback_data));
  if (rc != AE_OK) {
    mrb_sys_fail(mrb, "aeCreateFileEvent");
  }

  return mrb_ae_callback_data;
}

static inline void
mrb_ae_delete_file_callback_data(mrb_state *mrb, mrb_value mrb_ae_callback_data, mrb_value self)
{
  mrb_value client_data = mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@client_data"));
  int fd = 0;
  if (!mrb_undef_p(client_data)) {
    mrb_value fd_val = mrb_Integer(mrb, client_data);
    fd = mrb_fixnum(fd_val);
  }
  int mask = mrb_fixnum(mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "mask")));
  mrb_iv_remove(mrb, mrb_ae_callback_data, mrb_intern_lit(mrb, "@block"));
  aeDeleteFileEvent((aeEventLoop *) DATA_PTR(self), fd, mask);
}

static mrb_value
mrb_aeDeleteFileEvent(mrb_state *mrb, mrb_value self)
{
  mrb_value mrb_ae_callback_data;

  mrb_get_args(mrb, "o", &mrb_ae_callback_data);

  if (mrb_type(mrb_ae_callback_data) == MRB_TT_DATA && DATA_TYPE(mrb_ae_callback_data) == &mrb_ae_callback_data_type) {
    mrb_ae_delete_file_callback_data(mrb, mrb_ae_callback_data, self);
  }
  else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "expected Ae callback data");
  }

  return mrb_nil_value();
}

static mrb_value
mrb_aeProcessEvents(mrb_state *mrb, mrb_value self)
{
  mrb_int flags = AE_ALL_EVENTS;

  mrb_get_args(mrb, "|i", &flags);

  return mrb_fixnum_value(aeProcessEvents((aeEventLoop *) DATA_PTR(self), flags));
}

static mrb_value
mrb_aeMain(mrb_state *mrb, mrb_value self)
{
  aeMain((aeEventLoop *) DATA_PTR(self));

  return mrb_nil_value();
}

static mrb_value
mrb_aeGetSetSize(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(aeGetSetSize((aeEventLoop *) DATA_PTR(self)));
}

static mrb_value
mrb_aeResizeSetSize(mrb_state *mrb, mrb_value self)
{
  mrb_int setsize;

  mrb_get_args(mrb, "i", &setsize);

  if (setsize < INT_MIN||setsize > INT_MAX) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "setsize doesn't fit into int");
  }

  return mrb_fixnum_value(aeResizeSetSize((aeEventLoop *) DATA_PTR(self), setsize));
}

static mrb_value
mrb_ae_callback_data_init(mrb_state *mrb, mrb_value self)
{
  mrb_value client_data, block;

  mrb_get_args(mrb, "o&", &client_data, &block);

  mrb_ae_callback_data *callback_data = (mrb_ae_callback_data *) mrb_malloc(mrb, sizeof(mrb_ae_callback_data));
  callback_data->client_data = client_data;
  callback_data->block = block;
  mrb_data_init(self, callback_data, &mrb_ae_callback_data_type);
  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@client_data"), client_data);
  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@block"), block);

  return self;
}

void
mrb_mruby_redis_ae_gem_init(mrb_state* mrb)
{
  struct RClass *ae_class, *ae_callback_data_class;
  ae_class = mrb_define_class(mrb, "RedisAe", mrb->object_class);
  MRB_SET_INSTANCE_TT(ae_class, MRB_TT_DATA);
  mrb_define_const(mrb, ae_class, "READABLE", mrb_fixnum_value(AE_READABLE));
  mrb_define_const(mrb, ae_class, "WRITABLE", mrb_fixnum_value(AE_WRITABLE));
  mrb_define_const(mrb, ae_class, "FILE_EVENTS", mrb_fixnum_value(AE_FILE_EVENTS));
  mrb_define_const(mrb, ae_class, "TIME_EVENTS", mrb_fixnum_value(AE_TIME_EVENTS));
  mrb_define_const(mrb, ae_class, "ALL_EVENTS", mrb_fixnum_value(AE_ALL_EVENTS));
  mrb_define_const(mrb, ae_class, "DONT_WAIT", mrb_fixnum_value(AE_DONT_WAIT));
  mrb_define_method(mrb, ae_class, "initialize", mrb_aeCreateEventLoop, MRB_ARGS_NONE());
  mrb_define_method(mrb, ae_class, "stop", mrb_aeStop, MRB_ARGS_NONE());
  mrb_define_method(mrb, ae_class, "create_file_event", mrb_aeCreateFileEvent, MRB_ARGS_OPT(2)|MRB_ARGS_BLOCK());
  mrb_define_method(mrb, ae_class, "delete_file_event", mrb_aeDeleteFileEvent, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, ae_class, "process_events", mrb_aeProcessEvents, MRB_ARGS_OPT(1));
  mrb_define_method(mrb, ae_class, "main", mrb_aeMain, MRB_ARGS_NONE());
  mrb_define_method(mrb, ae_class, "set_size", mrb_aeGetSetSize, MRB_ARGS_NONE());
  mrb_define_method(mrb, ae_class, "set_size=", mrb_aeResizeSetSize, MRB_ARGS_REQ(1));

  ae_callback_data_class = mrb_define_class_under(mrb, ae_class, "CallbackData", mrb->object_class);
  MRB_SET_INSTANCE_TT(ae_callback_data_class, MRB_TT_DATA);
  mrb_define_method(mrb, ae_callback_data_class, "initialize", mrb_ae_callback_data_init, MRB_ARGS_REQ(1)|MRB_ARGS_BLOCK());
}

void mrb_mruby_redis_ae_gem_final(mrb_state* mrb) {}
