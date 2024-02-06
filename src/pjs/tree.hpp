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

#ifndef PJS_TREE_HPP
#define PJS_TREE_HPP

#include "types.hpp"

namespace pjs {

class Expr;
class Stmt;

//
// Tree (common base of Expr and Stmt)
//

class Tree {
public:

  //
  // Tree::Error
  //

  struct Error {
    Tree *tree = nullptr;
    std::string message;
  };

  //
  // Tree::Imports
  //

  class Imports {
  public:
    void add(Str *name, int file, Str *original_name);
    bool get(Str *name, int *file, Str **original_name);

  private:
    std::map<Ref<Str>, std::pair<int, Ref<Str>>> m_imports;
  };

  //
  // Tree::Scope
  //

  class Scope {
  public:
    enum Kind {
      MODULE,
      FUNCTION,
      BLOCK,
      LABEL,
      SWITCH,
      LOOP,
    };
  
    Scope(Kind kind, Scope *p = nullptr) : m_kind(kind), m_parent(p) {}
    Scope(Str* label, Scope *p = nullptr) : m_kind(LABEL), m_parent(p), m_label(label) {}

    auto kind() const -> Kind { return m_kind; }
    auto label() const -> Str* { return m_label; }
    auto parent() const -> Scope* { return m_parent; }
    bool is_root() const { return !m_parent; }
    void declare_arg(Expr *expr);
    void declare_var(Str *name, Expr *value = nullptr);
    auto variables() -> std::vector<pjs::Scope::Variable>& { init_variables(); return m_variables; }
    auto new_scope(Context &ctx) -> pjs::Scope*;

  private:
    struct InitArg {
      int index;
      int unpack_index = 0;
      Expr* value = nullptr;
      Expr* default_value = nullptr;
      Expr* unpack = nullptr;
    };

    struct InitVar {
      int index;
      Expr* value;
    };

    Kind m_kind;
    Scope* m_parent;
    Ref<Str> m_label;
    std::vector<pjs::Scope::Variable> m_variables;
    std::vector<Ref<Str>> m_args, m_vars;
    std::list<InitArg> m_init_args;
    std::list<InitVar> m_init_vars;
    size_t m_size = 0;
    bool m_initialized = false;

    void init_variables();
  };

  //
  // Tree location in script
  //

  auto source() const -> const Source* { return m_source; }
  auto line() const -> int { return m_line; }
  auto column() const -> int { return m_column; }

  void locate(const Source *source, int line, int column) {
    m_source = source;
    m_line = line;
    m_column = column;
  }

  //
  // Tree base methods
  //

  virtual bool declare(Scope &scope, Error &error) { return true; }
  virtual void resolve(Context &ctx, int l = -1, Imports *imports = nullptr) {}

private:
  const Source* m_source = nullptr;
  int m_line = 0;
  int m_column = 0;
};

} // namespace pjs

#endif // PJS_TREE_HPP
