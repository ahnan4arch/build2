// file      : build2/test/target.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/test/target.hxx>

using namespace std;
using namespace butl;

namespace build2
{
  namespace test
  {
    static const char*
    testscript_target_extension (const target_key& tk)
    {
      // If the name is special 'testscript', then there is no extension,
      // otherwise it is .test.
      //
      return *tk.name == "testscript" ? "" : "test";
    }

    static bool
    testscript_target_pattern (const target_type&,
                               const scope&,
                               string& v,
                               optional<string>& e,
                               bool r)
    {
      if (r)
      {
        assert (e);
        e = nullopt;
      }
      else if (!e && v != "testscript")
      {
        e = "test";
        return true;
      }

      return false;
    }

    const target_type testscript::static_type
    {
      "test",
      &file::static_type,
      &target_factory<testscript>,
      &testscript_target_extension,
      nullptr,  /* default_extension */
      &testscript_target_pattern,
      nullptr,
      &file_search,
      false
    };
  }
}
