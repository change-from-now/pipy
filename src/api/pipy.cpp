/*
 *  Copyright (c) 2019 by flomesh.io
 *
 *  Unless prior written consent has been obtained from the copyright
 *  owner, the following shall not be allowed.
 *
 *  1. The distribution of any source codes, header files, make files,
 *     or libraries of the software.
 *
 *  2. Disclosure of any source codes pertaining to the software to any
 *     additional parties.
 *
 *  3. Alteration or removal of any notices in or on the software or
 *     within the documentation included within the software.
 *
 *  ALL SOURCE CODE AS WELL AS ALL DOCUMENTATION INCLUDED WITH THIS
 *  SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pipy.hpp"
#include "codebase.hpp"
#include "configuration.hpp"
#include "context.hpp"
#include "worker.hpp"
#include "worker-thread.hpp"
#include "status.hpp"
#include "net.hpp"
#include "outbound.hpp"
#include "os-platform.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <exception>

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

namespace pipy {

static std::function<void(int)> s_on_exit;

void Pipy::on_exit(const std::function<void(int)> &on_exit) {
  s_on_exit = on_exit;
}

thread_local static Data::Producer s_dp("pipy.exec()");

#ifndef _WIN32

static auto exec_argv(const std::list<std::string> &args) -> Data* {
  auto argc = args.size();
  if (!argc) {
    throw std::runtime_error("exec() with no arguments");
  }

  int n = 0;
  char *argv[argc + 1];
  for (const auto &arg : args) argv[n++] = strdup(arg.c_str());
  argv[n] = nullptr;

  int in[2], out[2];
  pipe(in);
  pipe(out);

  auto pid = fork();
  if (!pid) {
    dup2(in[0], 0);
    dup2(out[1], 1);
    execvp(argv[0], argv);
    std::terminate();
  } else if (pid < 0) {
    throw std::runtime_error("unable to fork");
  }

  pjs::Ref<SharedData> output_sd;
  std::thread t(
    [&]() {
      Data output;
      Data::Builder output_db(output, &s_dp);
      char buf[DATA_CHUNK_SIZE];
      while (auto len = read(out[0], buf, sizeof(buf))) {
        output_db.push(buf, len);
      }
      output_db.flush();
      output_sd = SharedData::make(output);
    }
  );

  int status;
  waitpid(pid, &status, 0);
  close(out[1]);

  if (Log::is_enabled(Log::SUBPROC)) {
    std::string cmd_line;
    for (const auto &arg : args) {
      if (!cmd_line.empty()) cmd_line += ' ';
      cmd_line += arg;
    }
    Log::debug(Log::SUBPROC,
      "[exec] child process started [pid = %d]: %s",
      pid, cmd_line.c_str()
    );
  }

  t.join();

  Log::debug(Log::SUBPROC, "[exec] child process exited [pid = %d]", pid);

  close(out[0]);
  close(out[1]);
  close(in[0]);
  close(in[1]);

  for (int i = 0; i < n; i++) std::free(argv[i]);

  Data output;
  if (output_sd) output_sd->to_data(output);

  return Data::make(std::move(output));
}

auto Pipy::exec(const std::string &cmd) -> Data* {
  return exec_argv(utils::split_argv(cmd));
}

auto Pipy::exec(pjs::Array *argv) -> Data* {
  if (!argv) return nullptr;

  std::list<std::string> args;
  argv->iterate_all(
    [&](pjs::Value &v, int) {
      auto *s = v.to_string();
      args.push_back(s->str());
      s->release();
    }
  );

  return exec_argv(args);
}

#else // _WIN32

static auto exec_line(const std::string &line) {
  SECURITY_ATTRIBUTES sa;
  ZeroMemory(&sa, sizeof(sa));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;

  HANDLE in, out;
  if (!CreatePipe(&in, &out, &sa, 0)) {
    throw std::runtime_error(
      "unable to create pipe: " + os::windows::get_last_error()
    );
  }

  PROCESS_INFORMATION pif = {};
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = INVALID_HANDLE_VALUE;
  si.hStdOutput = out;
  si.hStdError = out;

  auto line_w = os::windows::a2w(line);
  pjs::vl_array<wchar_t, 1000> buf_line(line_w.length() + 1);
  std::memcpy(buf_line.data(), line_w.c_str(), buf_line.size() * sizeof(wchar_t));

  if (!CreateProcessW(
    NULL,
    buf_line.data(),
    NULL, NULL, TRUE, 0, NULL, NULL,
    &si, &pif
  )) {
    CloseHandle(out);
    CloseHandle(in);
    throw std::runtime_error(
      "unable to create process '" + line + "': " + os::windows::get_last_error()
    );
  }

  Data output;
  Data::Builder output_db(output, &s_dp);
  std::thread t([&]() {
    char buf[DATA_CHUNK_SIZE];
    DWORD len;
    while (ReadFile(in, buf, sizeof(buf), &len, NULL)) {
      output_db.push(buf, len);
    }
  });

  WaitForSingleObject(pif.hProcess, INFINITE);
  CloseHandle(pif.hThread);
  CloseHandle(pif.hProcess);
  CloseHandle(out);
  CloseHandle(in);

  t.join();
  output_db.flush();

  return Data::make(std::move(output));
}

auto Pipy::exec(const std::string &cmd) -> Data* {
  return exec_line(cmd);
}

auto Pipy::exec(pjs::Array *argv) -> Data* {
  if (!argv) return nullptr;

  std::list<std::string> args;
  argv->iterate_all(
    [&](pjs::Value &v, int) {
      auto *s = v.to_string();
      args.push_back(s->str());
      s->release();
    }
  );

  return exec_line(os::windows::encode_argv(args));
}

#endif // _WIN32

void Pipy::operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
  pjs::Value ret_obj;
  pjs::Object *context_prototype = nullptr;
  if (!ctx.arguments(0, &context_prototype)) return;
  if (context_prototype && context_prototype->is_function()) {
    auto *f = context_prototype->as<pjs::Function>();
    (*f)(ctx, 0, nullptr, ret_obj);
    if (!ctx.ok()) return;
    if (!ret_obj.is_object()) {
      ctx.error("function did not return an object");
      return;
    }
    context_prototype = ret_obj.o();
  }
  try {
    auto config = Configuration::make(context_prototype);
    ret.set(config);
  } catch (std::runtime_error &err) {
    ctx.error(err);
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Pipy>::init() {
  super<Function>();
  ctor();

  variable("inbound", class_of<Pipy::Inbound>());
  variable("outbound", class_of<Pipy::Outbound>());

  accessor("pid", [](Object *, Value &ret) {
    ret.set(os::process_id());
  });

  accessor("since", [](Object *, Value &ret) {
    ret.set(Status::LocalInstance::since);
  });

  accessor("source", [](Object *, Value &ret) {
    thread_local static pjs::Ref<pjs::Str> str;
    if (!str) str = pjs::Str::make(Status::LocalInstance::source);
    ret.set(str);
  });

  accessor("name", [](Object *, Value &ret) {
    thread_local static pjs::Ref<pjs::Str> str;
    if (!str) str = pjs::Str::make(Status::LocalInstance::name);
    ret.set(str);
  });

  accessor("uuid", [](Object *, Value &ret) {
    thread_local static pjs::Ref<pjs::Str> str;
    if (!str) str = pjs::Str::make(Status::LocalInstance::uuid);
    ret.set(str);
  });

  method("load", [](Context &ctx, Object*, Value &ret) {
    std::string filename;
    if (!ctx.arguments(1, &filename)) return;
    auto path = utils::path_normalize(filename);
    auto data = Codebase::current()->get(path);
    ret.set(data ? pipy::Data::make(*data) : nullptr);
    if (data) data->release();
  });

  method("list", [](Context &ctx, Object*, Value &ret) {
    std::string pathname;
    if (!ctx.arguments(1, &pathname)) return;
    auto codebase = Codebase::current();
    auto a = Array::make();
    std::function<void(const std::string&, const std::string&)> list_dir;
    list_dir = [&](const std::string &path, const std::string &base) {
      for (const auto &name : codebase->list(path)) {
        if (name.back() == '/') {
          auto sub = name.substr(0, name.length() - 1);
          auto str = path + '/' + sub;
          list_dir(str, base + sub + '/');
        } else {
          a->push(Str::make(base + name));
        }
      }
    };
    list_dir(utils::path_normalize(pathname), "");
    ret.set(a);
  });

  method("solve", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    auto worker = static_cast<pipy::Context*>(ctx.root())->worker();
    worker->solve(ctx, filename, ret);
  });

  method("restart", [](Context&, Object*, Value&) {
    Net::main().post(
      []() {
        InputContext ic;
        Codebase::current()->sync(
          true, [](bool ok) {
            if (ok) {
              WorkerManager::get().reload();
            }
          }
        );
      }
    );
  });

  method("exit", [](Context &ctx, Object*, Value&) {
    int exit_code = 0;
    if (!ctx.arguments(0, &exit_code)) return;
    Net::main().post(
      [=]() {
        WorkerManager::get().stop(true);
        if (s_on_exit) s_on_exit(exit_code);
      }
    );
  });

  method("exec", [](Context &ctx, Object*, Value &ret) {
    Str *cmd;
    Array *argv;
    try {
      if (ctx.get(0, cmd)) {
        ret.set(Pipy::exec(cmd->str()));
      } else if (ctx.get(0, argv)) {
        ret.set(Pipy::exec(argv));
      } else {
        ctx.error_argument_type(0, "a string or an array");
      }
    } catch (const std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Pipy::Inbound>::init() {
  ctor();

  accessor("count", [](Object*, Value &ret) { ret.set(pipy::Inbound::count()); });

  method("forEach", [](Context &ctx, Object*, Value&) {
    Function *cb;
    if (!ctx.arguments(1, &cb)) return;
    pipy::Inbound::for_each(
      [&](pipy::Inbound *ib) {
        Value arg(ib), ret;
        (*cb)(ctx, 1, &arg, ret);
        return ctx.ok();
      }
    );
  });
}

template<> void ClassDef<Pipy::Outbound>::init() {
  ctor();

  accessor("count", [](Object*, Value &ret) { ret.set(pipy::Outbound::count()); });

  method("forEach", [](Context &ctx, Object*, Value&) {
    Function *cb;
    if (!ctx.arguments(1, &cb)) return;
    pipy::Outbound::for_each(
      [&](pipy::Outbound *ob) {
        Value arg(ob), ret;
        (*cb)(ctx, 1, &arg, ret);
        return ctx.ok();
      }
    );
  });
}

} // namespace pjs
