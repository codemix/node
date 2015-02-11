// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_ASYNC_WRAP_INL_H_
#define SRC_ASYNC_WRAP_INL_H_

#include "async-wrap.h"
#include "base-object.h"
#include "base-object-inl.h"
#include "env.h"
#include "env-inl.h"
#include "node_internals.h"
#include "util.h"
#include "util-inl.h"
#include "v8.h"

namespace node {

inline AsyncWrap::AsyncWrap(Environment* env,
                            v8::Handle<v8::Object> object,
                            ProviderType provider,
                            AsyncWrap* parent)
    : BaseObject(env, object),
      has_async_queue_(false),
      provider_type_(provider) {
  // Check user controlled flag to see if the init callback should run.
  if (!env->call_async_init_hook())
    return;

  // TODO(trevnorris): Do we really need to TryCatch this call?
  v8::TryCatch try_catch;
  try_catch.SetVerbose(true);

  v8::Local<v8::Value> val = object.As<v8::Value>();
  env->async_listener_run_function()->Call(env->process_object(), 1, &val);

  if (!try_catch.HasCaught())
    async_flags_ |= HAS_ASYNC_LISTENER;
}


inline uint32_t AsyncWrap::provider_type() const {
  return provider_type_;
}


inline bool AsyncWrap::has_async_listener() {
  return async_flags_ & HAS_ASYNC_LISTENER;
}


// I hate you domains.
inline v8::Handle<v8::Value> AsyncWrap::MakeDomainCallback(
    const v8::Handle<v8::Function> cb,
    int argc,
    v8::Handle<v8::Value>* argv) {
  CHECK_EQ(env()->context(), env()->isolate()->GetCurrentContext());

  v8::Local<v8::Object> parent_obj;

  v8::TryCatch try_catch;

  // If a parent value was sent then call its pre/post functions to let it know
  // a conceptual "child" is being instantiated (e.g. that a server has
  // received a connection).
  if (parent != NULL) {
    parent_obj = parent->object();
    env->async_hooks_pre_function()->Call(parent_obj, 0, NULL);
    if (try_catch.HasCaught())
      return v8::Undefined(env()->isolate());
  }

  bool has_domain = domain_v->IsObject();
  if (has_domain) {
    domain = domain_v.As<v8::Object>();

    if (domain->Get(env()->disposed_string())->IsTrue())
      return Undefined(env()->isolate());

    v8::Local<v8::Function> enter =
      domain->Get(env()->enter_string()).As<v8::Function>();
    if (enter->IsFunction()) {
      enter->Call(domain, 0, nullptr);
      if (try_catch.HasCaught())
        return Undefined(env()->isolate());
    }
  }

  env->async_hooks_init_function()->Call(object, 0, NULL);

  if (try_catch.HasCaught()) {
    return Undefined(env()->isolate());
  }

  if (has_domain) {
    v8::Local<v8::Function> exit =
      domain->Get(env()->exit_string()).As<v8::Function>();
    if (exit->IsFunction()) {
      exit->Call(domain, 0, nullptr);
      if (try_catch.HasCaught())
        return Undefined(env()->isolate());
    }
  }

  has_async_queue_ = true;

  if (parent != NULL) {
    env->async_hooks_post_function()->Call(parent_obj, 0, NULL);
    if (try_catch.HasCaught())
      FatalError("node::AsyncWrap::AsyncWrap", "parent post hook threw");
  }

  Environment::TickInfo* tick_info = env()->tick_info();

  if (tick_info->in_tick()) {
    return ret;
  }

  if (tick_info->length() == 0) {
    env()->isolate()->RunMicrotasks();
  }

  if (tick_info->length() == 0) {
    tick_info->set_index(0);
    return ret;
  }

  tick_info->set_in_tick(true);

  env()->tick_callback_function()->Call(process, 0, nullptr);

  tick_info->set_in_tick(false);

  if (try_catch.HasCaught()) {
    tick_info->set_last_threw(true);
    return Undefined(env()->isolate());
  }

  return ret;
}


inline v8::Handle<v8::Value> AsyncWrap::MakeCallback(
    const v8::Handle<v8::Function> cb,
    int argc,
    v8::Handle<v8::Value>* argv) {
  if (env()->using_domains())
    return MakeDomainCallback(cb, argc, argv);

  CHECK_EQ(env()->context(), env()->isolate()->GetCurrentContext());

  v8::Local<v8::Object> context = object();
  v8::Local<v8::Object> process = env()->process_object();

  v8::TryCatch try_catch;
  try_catch.SetVerbose(true);

  if (has_async_listener()) {
    v8::Local<v8::Value> val = context.As<v8::Value>();
    env()->async_listener_load_function()->Call(process, 1, &val);

    if (try_catch.HasCaught())
      return v8::Undefined(env()->isolate());
  }

  v8::Local<v8::Value> ret = cb->Call(context, argc, argv);

  if (try_catch.HasCaught()) {
    return Undefined(env()->isolate());
  }

  if (has_async_listener()) {
    v8::Local<v8::Value> val = context.As<v8::Value>();
    env()->async_listener_unload_function()->Call(process, 1, &val);

    if (try_catch.HasCaught())
      return v8::Undefined(env()->isolate());
  }

  Environment::TickInfo* tick_info = env()->tick_info();

  if (tick_info->in_tick()) {
    return ret;
  }

  if (tick_info->length() == 0) {
    env()->isolate()->RunMicrotasks();
  }

  if (tick_info->length() == 0) {
    tick_info->set_index(0);
    return ret;
  }

  tick_info->set_in_tick(true);

  env()->tick_callback_function()->Call(process, 0, nullptr);

  tick_info->set_in_tick(false);


inline uint32_t AsyncWrap::provider_type() const {
  return provider_type_;
}


inline v8::Handle<v8::Value> AsyncWrap::MakeCallback(
    const v8::Handle<v8::String> symbol,
    int argc,
    v8::Handle<v8::Value>* argv) {
  v8::Local<v8::Value> cb_v = object()->Get(symbol);
  v8::Local<v8::Function> cb = cb_v.As<v8::Function>();
  CHECK(cb->IsFunction());

  return MakeCallback(cb, argc, argv);
}


inline v8::Handle<v8::Value> AsyncWrap::MakeCallback(
    uint32_t index,
    int argc,
    v8::Handle<v8::Value>* argv) {
  v8::Local<v8::Value> cb_v = object()->Get(index);
  v8::Local<v8::Function> cb = cb_v.As<v8::Function>();
  CHECK(cb->IsFunction());

  return MakeCallback(cb, argc, argv);
}

}  // namespace node

#endif  // SRC_ASYNC_WRAP_INL_H_
