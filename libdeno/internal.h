// Copyright 2018-2019 the Deno authors. All rights reserved. MIT license.
#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <map>
#include <string>
#include <utility>
#include <vector>
#include "deno.h"
#include "third_party/v8/include/v8.h"
#include "third_party/v8/src/base/logging.h"

namespace deno {

struct ModuleInfo {
  std::string name;
  v8::Persistent<v8::Module> handle;
  std::vector<std::string> import_specifiers;

  ModuleInfo(v8::Isolate* isolate, v8::Local<v8::Module> module,
             const char* name_, std::vector<std::string> import_specifiers_)
      : name(name_), import_specifiers(import_specifiers_) {
    handle.Reset(isolate, module);
  }
};

// deno_s = Wrapped Isolate.
class DenoIsolate {
 public:
  explicit DenoIsolate(deno_config config)
      : isolate_(nullptr),
        shared_(config.shared),
        futex_cb(config.futex_cb),
        current_args_(nullptr),
        snapshot_creator_(nullptr),
        global_import_buf_ptr_(nullptr),
        recv_cb_(config.recv_cb),
        next_req_id_(0),
        user_data_(nullptr),
        resolve_cb_(nullptr) {
    array_buffer_allocator_ = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    if (config.load_snapshot.data_ptr) {
      snapshot_.data =
          reinterpret_cast<const char*>(config.load_snapshot.data_ptr);
      snapshot_.raw_size = static_cast<int>(config.load_snapshot.data_len);
    }
  }

  ~DenoIsolate() {
    shared_ab_.Reset();
    if (snapshot_creator_) {
      delete snapshot_creator_;
    } else {
      isolate_->Dispose();
    }
    delete array_buffer_allocator_;
  }

  static inline DenoIsolate* FromIsolate(v8::Isolate* isolate) {
    return static_cast<DenoIsolate*>(isolate->GetData(0));
  }

  void AddIsolate(v8::Isolate* isolate);

  deno_mod RegisterModule(const char* name, const char* source);
  v8::Local<v8::Object> GetBuiltinModules();
  void ClearModules();

  ModuleInfo* GetModuleInfo(deno_mod id) {
    if (id == 0) {
      return nullptr;
    }
    auto it = mods_.find(id);
    if (it != mods_.end()) {
      return &it->second;
    } else {
      return nullptr;
    }
  }

  v8::Isolate* isolate_;
  v8::ArrayBuffer::Allocator* array_buffer_allocator_;
  deno_buf shared_;
  deno_futex_cb futex_cb;
  const v8::FunctionCallbackInfo<v8::Value>* current_args_;
  v8::SnapshotCreator* snapshot_creator_;
  void* global_import_buf_ptr_;
  deno_recv_cb recv_cb_;
  int32_t next_req_id_;
  void* user_data_;

  v8::Persistent<v8::Object> builtin_modules_;
  std::map<deno_mod, ModuleInfo> mods_;
  std::map<std::string, deno_mod> mods_by_name_;
  deno_resolve_cb resolve_cb_;

  v8::Persistent<v8::Context> context_;
  std::map<int32_t, v8::Persistent<v8::Value>> async_data_map_;
  std::map<int, v8::Persistent<v8::Value>> pending_promise_map_;
  std::string last_exception_;
  v8::Persistent<v8::Function> recv_;
  v8::Persistent<v8::Function> idle_;
  v8::StartupData snapshot_;
  v8::Persistent<v8::ArrayBuffer> global_import_buf_;
  v8::Global<v8::SharedArrayBuffer> shared_ab_;
};

class UserDataScope {
  DenoIsolate* deno_;
  void* prev_data_;
  void* data_;  // Not necessary; only for sanity checking.

 public:
  UserDataScope(DenoIsolate* deno, void* data) : deno_(deno), data_(data) {
    CHECK(deno->user_data_ == nullptr || deno->user_data_ == data_);
    prev_data_ = deno->user_data_;
    deno->user_data_ = data;
  }

  ~UserDataScope() {
    CHECK(deno_->user_data_ == data_);
    deno_->user_data_ = prev_data_;
  }
};

struct InternalFieldData {
  uint32_t data;
};

static inline v8::Local<v8::String> v8_str(const char* x) {
  return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), x,
                                 v8::NewStringType::kNormal)
      .ToLocalChecked();
}

void Print(const v8::FunctionCallbackInfo<v8::Value>& args);
void Recv(const v8::FunctionCallbackInfo<v8::Value>& args);
void SetIdle(const v8::FunctionCallbackInfo<v8::Value>& args);
void Send(const v8::FunctionCallbackInfo<v8::Value>& args);
void Futex(const v8::FunctionCallbackInfo<v8::Value>& args);
void EvalContext(const v8::FunctionCallbackInfo<v8::Value>& args);
void ErrorToJSON(const v8::FunctionCallbackInfo<v8::Value>& args);
void Shared(v8::Local<v8::Name> property,
            const v8::PropertyCallbackInfo<v8::Value>& info);
void BuiltinModules(v8::Local<v8::Name> property,
                    const v8::PropertyCallbackInfo<v8::Value>& info);
void MessageCallback(v8::Local<v8::Message> message, v8::Local<v8::Value> data);
static intptr_t external_references[] = {
    reinterpret_cast<intptr_t>(Print),
    reinterpret_cast<intptr_t>(Recv),
    reinterpret_cast<intptr_t>(SetIdle),
    reinterpret_cast<intptr_t>(Send),
    reinterpret_cast<intptr_t>(Futex),
    reinterpret_cast<intptr_t>(EvalContext),
    reinterpret_cast<intptr_t>(ErrorToJSON),
    reinterpret_cast<intptr_t>(Shared),
    reinterpret_cast<intptr_t>(BuiltinModules),
    reinterpret_cast<intptr_t>(MessageCallback),
    0};

static const deno_buf empty_buf = {nullptr, 0, nullptr, 0};

Deno* NewFromSnapshot(void* user_data, deno_recv_cb cb);

void InitializeContext(v8::Isolate* isolate, v8::Local<v8::Context> context);

void DeserializeInternalFields(v8::Local<v8::Object> holder, int index,
                               v8::StartupData payload, void* data);

v8::StartupData SerializeInternalFields(v8::Local<v8::Object> holder, int index,
                                        void* data);

v8::Local<v8::Uint8Array> ImportBuf(DenoIsolate* d, deno_buf buf);

void DeleteDataRef(DenoIsolate* d, int32_t req_id);

bool Execute(v8::Local<v8::Context> context, const char* js_filename,
             const char* js_source);
bool ExecuteMod(v8::Local<v8::Context> context, const char* js_filename,
                const char* js_source, bool resolve_only);

}  // namespace deno

extern "C" {
// This is just to workaround the linker.
struct deno_s {
  deno::DenoIsolate isolate;
};
}

#endif  // INTERNAL_H_
