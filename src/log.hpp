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

#ifndef LOG_H
#define LOG_H

#include "pjs/pjs.hpp"

namespace pipy {

class Data;

//
// Log
//

class Log {
public:
  enum Level {
    DEBUG,
    WARN,
    ERROR,
    INFO,
  };

  static void init();
  static void shutdown();
  static void set_level(Level level);
  static void set_graph_enabled(bool enabled);
  static bool is_enabled(Level level);
  static bool is_graph_enabled();

  static auto format_header(Level level, char *buf, size_t len) -> size_t;

  static void write(const Data &data);
  static void write(const std::string &data);

  static void debug(const char *fmt, ...);
  static void info(const char *fmt, ...);
  static void warn(const char *fmt, ...);
  static void error(const char *fmt, ...);

  static void pjs_location(const std::string &source, const std::string &filename, int line, int column);
  static void pjs_error(const pjs::Context::Error &err);
  static void pjs_error(const pjs::Context::Error &err, const std::string &source, const std::string &filename);
};

} // namespace pipy

#endif // LOG_H
