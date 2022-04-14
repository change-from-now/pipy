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

#ifndef ADMIN_SERVICE_HPP
#define ADMIN_SERVICE_HPP

#include "api/http.hpp"
#include "api/crypto.hpp"
#include "api/stats.hpp"
#include "filter.hpp"
#include "message.hpp"
#include "data.hpp"
#include "tar.hpp"
#include "codebase-store.hpp"
#include "status.hpp"

#include <map>
#include <set>

namespace pipy {

//
// AdminService
//

class AdminService {
public:
  struct Options {
    pjs::Ref<crypto::Certificate> cert;
    pjs::Ref<crypto::PrivateKey> key;
    pjs::Ref<pjs::Array> trusted;
  };

  AdminService(CodebaseStore *store);

  void open(int port, const Options &options);
  void close();

private:

  //
  // AdminService::AdminLinkHandler
  //

  class AdminLinkHandler : public Filter {
  public:
    AdminLinkHandler(AdminService *service)
      : m_service(service) {}

    AdminLinkHandler(const AdminLinkHandler &r)
      : m_service(r.m_service) {}

  private:
    virtual auto clone() -> Filter* override;
    virtual void reset() override;
    virtual void process(Event *evt) override;
    virtual void dump(std::ostream &out) override;

    AdminService* m_service;
    Data m_payload;
    bool m_started;
  };

  struct Instance {
    Status status;
    stats::MetricSet metrics;
  };

  int m_port;
  CodebaseStore* m_store;
  std::string m_current_codebase;
  std::string m_current_program;
  std::map<std::string, std::map<std::string, Instance>> m_instances;

  Tarball m_www_files;
  std::map<std::string, pjs::Ref<http::File>> m_www_file_cache;

  pjs::Ref<http::ResponseHead> m_response_head_text;
  pjs::Ref<http::ResponseHead> m_response_head_json;
  pjs::Ref<Message> m_response_ok;
  pjs::Ref<Message> m_response_created;
  pjs::Ref<Message> m_response_deleted;
  pjs::Ref<Message> m_response_not_found;
  pjs::Ref<Message> m_response_method_not_allowed;

  auto handle(Message *req) -> Message*;

  Message* metrics_GET();

  Message* repo_HEAD(const std::string &path);
  Message* repo_GET(const std::string &path);
  Message* repo_POST(const std::string &path, Data *data);

  Message* api_v1_repo_GET(const std::string &path);
  Message* api_v1_repo_POST(const std::string &path, Data *data);
  Message* api_v1_repo_PATCH(const std::string &path, Data *data);
  Message* api_v1_repo_DELETE(const std::string &path);

  Message* api_v1_files_GET(const std::string &path);
  Message* api_v1_files_POST(const std::string &path, Data *data);
  Message* api_v1_files_DELETE(const std::string &path);

  Message* api_v1_program_GET();
  Message* api_v1_program_POST(Data *data);
  Message* api_v1_program_DELETE();

  Message* api_v1_status_GET();

  Message* api_v1_graph_POST(Data *data);

  Message* api_v1_log_GET(Message *req);

  Message* response(const Data &text);
  Message* response(const std::string &text);
  Message* response(const std::set<std::string> &list);
  Message* response(int status_code, const std::string &message);

  auto codebase_of(const std::string &path) -> CodebaseStore::Codebase*;
  auto codebase_of(const std::string &path, std::string &filename) -> CodebaseStore::Codebase*;

  static auto response_head(
    int status,
    const std::map<std::string, std::string> &headers
  ) -> http::ResponseHead*;

  void on_metrics(const Data &data);
};

} // namespace pipy

#endif // ADMIN_SERVICE_HPP
