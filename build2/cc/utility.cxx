// file      : build2/cc/utility.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2016 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/cc/utility>

#include <build2/variable>
#include <build2/algorithm> // search()

#include <build2/bin/target>

using namespace std;

namespace build2
{
  namespace cc
  {
    using namespace bin;

    lorder
    link_order (scope& bs, otype ot)
    {
      // Initialize to suppress 'may be used uninitialized' warning produced
      // by MinGW GCC 5.4.0.
      //
      const char* var (nullptr);

      switch (ot)
      {
      case otype::e: var = "bin.exe.lib";  break;
      case otype::a: var = "bin.liba.lib"; break;
      case otype::s: var = "bin.libs.lib"; break;
      }

      const auto& v (cast<strings> (bs[var]));
      return v[0] == "shared"
        ? v.size () > 1 && v[1] == "static" ? lorder::s_a : lorder::s
        : v.size () > 1 && v[1] == "shared" ? lorder::a_s : lorder::a;
    }

    target&
    link_member (bin::lib& l, lorder lo)
    {
      bool ls (true);
      const string& at (cast<string> (l["bin.lib"])); // Available members.

      switch (lo)
      {
      case lorder::a:
      case lorder::a_s:
        ls = false; // Fall through.
      case lorder::s:
      case lorder::s_a:
        {
          if (ls ? at == "static" : at == "shared")
          {
            if (lo == lorder::a_s || lo == lorder::s_a)
              ls = !ls;
            else
              fail << (ls ? "shared" : "static") << " variant of " << l
                   << " is not available";
          }
        }
      }

      target* r (ls ? static_cast<target*> (l.s) : l.a);

      if (r == nullptr)
        r = &search (ls ? libs::static_type : liba::static_type,
                     prerequisite_key {nullptr, l.key (), nullptr});

      return *r;
    }

    void
    append_lib_options (cstrings& args, target& l, lorder lo,
                        const variable& cv,
                        const variable& xv)
    {
      using namespace bin;

      for (target* t: l.prerequisite_targets)
      {
        if (lib* l = t->is_a<lib> ())
          t = &link_member (*l, lo); // Pick one of the members.

        if (t->is_a<liba> () || t->is_a<libs> ())
          append_lib_options (args, *t, lo, cv, xv);
      }

      append_options (args, l, cv);
      append_options (args, l, xv);
    }

    void
    hash_lib_options (sha256& csum, target& l, lorder lo,
                      const variable& cv,
                      const variable& xv)
    {
      using namespace bin;

      for (target* t: l.prerequisite_targets)
      {
        if (lib* l = t->is_a<lib> ())
          t = &link_member (*l, lo); // Pick one of the members.

        if (t->is_a<liba> () || t->is_a<libs> ())
          hash_lib_options (csum, *t, lo, cv, xv);
      }

      hash_options (csum, l, cv);
      hash_options (csum, l, xv);
    }
  }
}