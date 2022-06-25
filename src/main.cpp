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

#include "version.h"

#include "admin-link.hpp"
#include "admin-service.hpp"
#include "admin-proxy.hpp"
#include "api/crypto.hpp"
#include "api/logging.hpp"
#include "api/stats.hpp"
#include "codebase.hpp"
#include "file.hpp"
#include "fs.hpp"
#include "filters/tls.hpp"
#include "input.hpp"
#include "listener.hpp"
#include "main-options.hpp"
#include "net.hpp"
#include "status.hpp"
#include "timer.hpp"
#include "utils.hpp"
#include "worker.hpp"

#include <signal.h>

#include <list>
#include <string>
#include <tuple>

#include <openssl/opensslv.h>

using namespace pipy;

AdminService *s_admin = nullptr;
AdminProxy *s_admin_proxy = nullptr;
AdminLink *s_admin_link = nullptr;

//
// Show version
//

static void show_version() {
  std::cout << "Version     : " << PIPY_VERSION << std::endl;
  std::cout << "Commit      : " << PIPY_COMMIT << std::endl;
  std::cout << "Commit Date : " << PIPY_COMMIT_DATE << std::endl;
  std::cout << "Host        : " << PIPY_HOST << std::endl;
  std::cout << "OpenSSL     : " << OPENSSL_VERSION_TEXT << std::endl;

#ifdef PIPY_USE_GUI
  std::cout << "Builtin GUI : " << "Yes" << std::endl;
#else
  std::cout << "Builtin GUI : " << "No" << std::endl;
#endif

#ifdef PIPY_USE_TUTORIAL
  std::cout << "Tutorial    : " << "Yes" << std::endl;
#else
  std::cout << "Tutorial    : " << "No" << std::endl;
#endif
}

//
// Periodically check codebase updates
//

static void start_checking_updates() {
  static Timer timer;
  static std::function<void()> poll;
  poll = []() {
    if (!Worker::exited()) {
      InputContext ic;
      Status::local.timestamp = utils::now();
      Codebase::current()->sync(
        Status::local,
        [&](bool ok) {
          if (ok) {
            Worker::restart();
          }
        }
      );
    }
    timer.schedule(5, poll);
  };
  poll();
}

//
// Periodically report metrics
//

static void start_reporting_metrics(const std::string &url) {
  static Data::Producer s_dp("Metric Reports");
  static Timer timer;
  static std::function<void()> report;
  static int connection_id = 0;
  std::string url_path = url;
  if (url_path.back() != '/') url_path += '/';
  url_path += Status::local.uuid;
  s_admin_link = new AdminLink(url_path);
  logging::Logger::set_admin_link(s_admin_link);
  report = []() {
    if (!Worker::exited()) {
      InputContext ic;
      auto conn_id = s_admin_link->connect();
      Data buf; buf.push("metrics\n", &s_dp);
      stats::Metric::local().collect_all();
      stats::Metric::local().serialize(buf, Status::local.uuid, conn_id != connection_id);
      s_admin_link->send(buf);
      connection_id = conn_id;
    }
    timer.schedule(5, report);
  };
  report();
}

//
// Handle signals
//

static void handle_signal(int sig) {
  static bool s_admin_closed = false;

  if (auto worker = Worker::current()) {
    if (worker->handling_signal(sig)) {
      return;
    }
  }

  switch (sig) {
    case SIGINT:
      if (!s_admin_closed) {
        if (s_admin_link) s_admin_link->close();
        if (s_admin) s_admin->close();
        s_admin_closed = true;
      }
      Worker::exit(-1);
      break;
    case SIGHUP:
      Worker::restart();
      break;
    case SIGTSTP:
      Status::dump_memory();
      break;
  }
}

//
// Wait for signals
//

static void wait_for_signals(asio::signal_set &signals) {
  signals.async_wait(
    [&](const std::error_code &ec, int sig) {
      if (!ec) handle_signal(sig);
      wait_for_signals(signals);
    }
  );
}

//
// Program entrance
//

int main(int argc, char *argv[]) {
  try {
    MainOptions opts(argc, argv);

    if (opts.version) {
      show_version();
      return 0;
    }

    if (opts.help) {
      MainOptions::show_help();
      return 0;
    }

    Status::local.timestamp = utils::now();
    Status::local.name = opts.instance_name;

    if (opts.instance_uuid.empty()) {
      utils::gen_uuid_v4(Status::local.uuid);
    } else {
      Status::local.uuid = opts.instance_uuid;
    }

    Log::init();
    Log::set_level(opts.log_level);
    Status::register_metrics();
    Listener::set_reuse_port(opts.reuse_port);
    crypto::Crypto::init(opts.openssl_engine);
    tls::TLSSession::init();
    File::start_bg_thread();

    AdminService::Options admin_options;
    admin_options.cert = opts.admin_tls_cert;
    admin_options.key = opts.admin_tls_key;
    admin_options.trusted = opts.admin_tls_trusted;

    auto admin_port = opts.admin_port;
    if (!admin_port) admin_port = 6060; // default repo port

    bool is_eval = false;
    bool is_repo = false;
    bool is_repo_proxy = false;
    bool is_remote = false;
    bool is_tls = false;

    if (opts.eval) {
      is_eval = true;

    } else if (opts.filename.empty()) {
      is_repo = true;

    } else if (utils::starts_with(opts.filename, "http://")) {
      is_remote = true;

    } else if (utils::starts_with(opts.filename, "https://")) {
      is_remote = true;
      is_tls = true;

    } else if (utils::is_host_port(opts.filename)) {
      is_repo_proxy = true;

    } else {
      auto full_path = fs::abs_path(opts.filename);
      opts.filename = full_path;
      if (!fs::exists(full_path)) {
        std::string msg("file or directory does not exist: ");
        throw std::runtime_error(msg + full_path);
      }
      is_repo = fs::is_dir(full_path);
    }

    if (is_remote) {
      auto i = opts.filename.find('/');
      auto target = opts.filename.substr(i+2);
      if (!target.empty() && target.back() == '/') {
        target.resize(target.size() - 1);
      }
      if (utils::is_host_port(target)) {
        opts.filename = target;
        is_remote = false;
        is_repo_proxy = true;
      }
    }

    Store *store = nullptr;
    CodebaseStore *repo = nullptr;
    Codebase *codebase = nullptr;

    std::function<void()> load, fail;
    Timer retry_timer;

    // Start as codebase repo service
    if (is_repo) {
      store = opts.filename.empty()
        ? Store::open_memory()
        : Store::open_level_db(opts.filename);
      repo = new CodebaseStore(store);
      s_admin = new AdminService(repo);
      s_admin->open(admin_port, admin_options);
      logging::Logger::set_admin_service(s_admin);

#ifdef PIPY_USE_GUI
      std::cout << std::endl;
      std::cout << "=============================================" << std::endl;
      std::cout << std::endl;
      std::cout << "  You can now view Pipy GUI in the browser:" << std::endl;
      std::cout << std::endl;
      std::cout << "    http://localhost:" << admin_port << '/' << std::endl;
      std::cout << std::endl;
      std::cout << "=============================================" << std::endl;
      std::cout << std::endl;
#endif

    // Start as codebase repo proxy
    } else if (is_repo_proxy) {
      AdminProxy::Options options;
      options.cert = opts.admin_tls_cert;
      options.key = opts.admin_tls_key;
      options.trusted = opts.admin_tls_trusted;
      options.fetch_options.tls = is_tls;
      options.fetch_options.cert = opts.tls_cert;
      options.fetch_options.key = opts.tls_key;
      options.fetch_options.trusted = opts.tls_trusted;
      s_admin_proxy = new AdminProxy(opts.filename);
      s_admin_proxy->open(admin_port, options);

    // Start as a fixed codebase
    } else {
      if (is_remote) {
        Fetch::Options options;
        options.tls = is_tls;
        options.cert = opts.tls_cert;
        options.key = opts.tls_key;
        options.trusted = opts.tls_trusted;
        codebase = Codebase::from_http(opts.filename, options);
      } else if (is_eval) {
        codebase = Codebase::from_fs(
          fs::abs_path("."),
          opts.filename
        );
      } else {
        codebase = Codebase::from_fs(opts.filename);
      }

      codebase->set_current();

      load = [&]() {
        codebase->sync(
          Status::local,
          [&](bool ok) {
            if (!ok) {
              fail();
              return;
            }

            auto &entry = Codebase::current()->entry();
            auto worker = Worker::make();
            auto mod = worker->load_module(entry);

            if (!mod) {
              fail();
              return;
            }

            if (opts.verify) {
              Worker::exit(0);
              return;
            }

            if (!worker->start()) {
              fail();
              return;
            }

            Status::local.version = Codebase::current()->version();
            Status::local.update();

            if (opts.admin_port) {
              s_admin = new AdminService(nullptr);
              s_admin->open(opts.admin_port, admin_options);
              logging::Logger::set_admin_service(s_admin);
            }

            start_checking_updates();

            if (is_remote) {
              start_reporting_metrics(opts.filename);
            }
          }
        );
      };

      fail = [&]() {
        if (is_remote) {
          retry_timer.schedule(5, load);
        } else {
          Worker::exit(-1);
        }
      };

      load();
    }

    asio::signal_set signals(Net::context());
    signals.add(SIGINT);
    signals.add(SIGHUP);
    signals.add(SIGTSTP);
    wait_for_signals(signals);

    Net::run();

    File::stop_bg_thread();

    delete s_admin_link;
    delete s_admin;
    delete s_admin_proxy;
    delete repo;

    if (store) store->close();

    crypto::Crypto::free();

    std::cout << "Done." << std::endl;

  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return Worker::exit_code();
}
