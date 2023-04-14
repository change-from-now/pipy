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

#ifndef ON_BODY_HPP
#define ON_BODY_HPP

#include "filter.hpp"
#include "buffer.hpp"

namespace pipy {

//
// OnBody
//

class OnBody : public Filter {
public:
  OnBody(pjs::Function *callback, const Buffer::Options &options);

private:
  OnBody(const OnBody &r);
  ~OnBody();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  //
  // OnBody::Callback
  //

  class Callback : public pjs::ObjectTemplate<Callback, pjs::Promise::Callback> {
    Callback(OnBody *filter) : m_filter(filter) {}
    virtual void on_resolved(const pjs::Value &value) override;
    virtual void on_rejected(const pjs::Value &error) override;
    friend class pjs::ObjectTemplate<Callback, pjs::Promise::Callback>;
    OnBody* m_filter;
  };

  pjs::Ref<Data> m_body;
  pjs::Ref<pjs::Function> m_callback;
  Buffer m_data_buffer;
  EventBuffer m_event_buffer;
  bool m_waiting = false;
  bool m_started = false;

  void handle(Event *evt);
  void flush();
};

} // namespace pipy

#endif // ON_BODY_HPP
