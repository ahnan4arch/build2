// file      : build2/cc/parser.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BUILD2_CC_PARSER_HXX
#define BUILD2_CC_PARSER_HXX

#include <build2/types.hxx>
#include <build2/utility.hxx>

#include <build2/diagnostics.hxx>

namespace build2
{
  namespace cc
  {
    // Extract (currently module) information from a preprocessed C/C++
    // source.
    //
    struct translation_unit
    {
      string         module_name;      // If not empty, then a module unit.
      bool           module_interface; // If true, then module interface unit.
      vector<string> module_imports;   // Imported modules.
    };

    struct token;
    class lexer;

    class parser
    {
    public:
      parser (): fail ("error", &name_), warn ("warning", &name_) {}

      translation_unit
      parse (istream&, const path& name);

    private:
      void
      parse_import (token&);

      void
      parse_module (token&, bool);

      string
      parse_module_name (token&);

    private:
      const path* name_;

      const fail_mark  fail;
      const basic_mark warn;

      lexer* l_;
      translation_unit* u_;
    };
  }
}

#endif // BUILD2_CC_PARSER_HXX
