#ifndef MRB_AE_H
#define MRB_AE_H

#include <time.h>
#include "ae.h"
#include <mruby.h>
#include <mruby/data.h>
#include <mruby/class.h>
#include <sys/resource.h>
#include <assert.h>
#include <mruby/variable.h>
#include <errno.h>
#include <mruby/error.h>

static void
mrb_aeDeleteEventLoop(mrb_state *mrb, void *p)
{
  aeDeleteEventLoop((aeEventLoop *) p);
}

static const struct mrb_data_type mrb_aeEventLoop_type = {
  "$i_mrb_aeEventLoop_type", mrb_aeDeleteEventLoop
};

typedef struct {
  mrb_value client_data;
  mrb_value block;
} mrb_ae_callback_data;

static const struct mrb_data_type mrb_ae_callback_data_type = {
  "$i_mrb_ae_callback_data_type", mrb_free
};


#endif
