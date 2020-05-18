// file      : libbuild2/script/lexer.test.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <cassert>
#include <iostream>

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/script/token.hxx>
#include <libbuild2/script/lexer.hxx>

using namespace std;

namespace build2
{
  namespace script
  {
    // Usage: argv[0] <lexer-mode>
    //
    int
    main (int argc, char* argv[])
    {
      lexer_mode m;
      {
        assert (argc == 2);
        string s (argv[1]);

        if      (s == "command-expansion") m = lexer_mode::command_expansion;
        else if (s == "here-line-single")  m = lexer_mode::here_line_single;
        else if (s == "here-line-double")  m = lexer_mode::here_line_double;
        else                               assert (false);
      }

      try
      {
        cin.exceptions (istream::failbit | istream::badbit);

        path_name in ("<stdin>");
        lexer l (cin, in, m);

        // No use printing eos since we will either get it or loop forever.
        //
        for (token t (l.next ()); t.type != token_type::eos; t = l.next ())
        {
          // Print each token on a separate line without quoting operators.
          //
          t.printer (cout, t, print_mode::normal);
          cout << endl;
        }
      }
      catch (const failed&)
      {
        return 1;
      }

      return 0;
    }
  }
}

int
main (int argc, char* argv[])
{
  return build2::script::main (argc, argv);
}
