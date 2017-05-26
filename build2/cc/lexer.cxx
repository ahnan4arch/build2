// file      : build2/cc/lexer.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/cc/lexer.hxx>

using namespace std;
using namespace butl;

// Diagnostics plumbing.
//
namespace butl // ADL
{
  inline build2::location
  get_location (const butl::char_scanner::xchar& c, const void* data)
  {
    using namespace build2;

    assert (data != nullptr); // E.g., must be &lexer::name_.
    return location (static_cast<const path*> (data), c.line, c.column);
  }
}

namespace build2
{
  namespace cc
  {
    inline auto lexer::
    get (bool e) -> xchar
    {
      if (unget_)
      {
        unget_ = false;
        return ungetc_;
      }
      else
      {
        xchar c (peek (e));
        base::get (c);
        return c;
      }
    }

    auto lexer::
    peek (bool e) -> xchar
    {
      if (unget_)
        return ungetc_;

      if (unpeek_)
        return unpeekc_;

      xchar c (base::peek ());

      if (e && c == '\\')
      {
        base::get (c);
        xchar p (base::peek ());

        if (p == '\n')
        {
          base::get (p);
          return peek (e); // Recurse.
        }

        // Save in the unpeek buffer so that it is returned on the subsequent
        // calls to peek() (until get()).
        //
        unpeek_ = true;
        unpeekc_ = c;
      }

      return c;
    }

    using type = token_type;

    void lexer::
    next (token& t, xchar c, bool ignore_pp)
    {
      for (;; c = skip_spaces ())
      {
        t.line = c.line;
        t.column = c.column;

        if (eos (c))
        {
          t.type = type::eos;
          return;
        }

        switch (c)
        {
          // Preprocessor lines.
          //
        case '#':
          {
            // It is tempting to simply scan until the newline ignoring
            // anything in between. However, these lines can start a
            // multi-line C-style comment. So we have to tokenize it. Note
            // that we assume there cannot be #include directives.
            //
            // This may not work for things like #error that can contain
            // pretty much anything. Also note that lines that start with
            // # can contain # further down.
            //
            if (ignore_pp)
            {
              for (;;)
              {
                c = skip_spaces (false); // Stop at newline.

                if (eos (c) || c == '\n')
                  break;

                next (t, c, false); // Keep using the passed token for buffers.
              }
              break;
            }
            else
            {
              t.type = type::punctuation;
              return;
            }
          }
          // Single-letter punctuation.
          //
        case ';': t.type = type::semi;    return;
        case '{': t.type = type::lcbrace; return;
        case '}': t.type = type::rcbrace; return;
          // Other single-letter punctuation.
          //
        case '(':
        case ')':
        case '[':
        case ']':
        case ',':
        case '?':
        case '~':
        case '\\': t.type = type::punctuation; return;
          // Potentially multi-letter punctuation.
          //
        case '.': // . .* .<N> ...
          {
            xchar p (peek ());

            if (p == '*')
            {
              get (p);
              t.type = type::punctuation;
              return;
            }
            else if (p >= '0' && p <= '9')
            {
              number_literal (t, c);
              return;
            }
            else if (p == '.')
            {
              get (p);
              xchar q (peek ());
              if (q == '.')
              {
                get (q);
                t.type = type::punctuation;
                return;
              }
              unget (p);
              // Fall through.
            }

            t.type = type::dot;
            return;
          }
        case '=': // = ==
        case '!': // ! !=
        case '*': // * *=
        case '/': // / /=   (/* and // handled by skip_spaced() above)
        case '%': // % %=
        case '^': // ^ ^=
          {
            xchar p (peek ());

            if (p == '=')
              get (p);

            t.type = type::punctuation;
            return;
          }
        case '>': // > >= >> >>=
        case '<': // < <= << <<=
          {
            xchar p (peek ());

            if (p == c)
            {
              get (p);
              if ((p = peek ()) == '=')
                get (p);
            }
            else if (p == '=')
              get (p);

            t.type = type::punctuation;
            return;
          }
        case '+': // + ++ +=
        case '-': // - -- -= -> ->*
          {
            xchar p (peek ());

            if (p == c)
              get (p);
            else if (p == '=')
              get (p);
            else if (c == '-' && p == '>')
            {
              get (p);
              if ((p = peek ()) == '*')
                get (p);
            }

            t.type = type::punctuation;
            return;
          }
        case '&': // & && &=
        case '|': // | || |=
          {
            xchar p (peek ());

            if (p == c)
              get (p);
            else if (p == '=')
              get (p);

            t.type = type::punctuation;
            return;
          }
        case ':': // : ::
          {
            xchar p (peek ());

            if (p == ':')
              get (p);

            t.type = type::punctuation;
            return;
          }
          // Number (and also .<N> above).
          //
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          {
            number_literal (t, c);
            return;
          }
          // Char/string literal, identifier, or other (\, $, @, `).
          //
        default:
          {
            bool raw (false); // Raw string literal.

            if (alpha (c) || c == '_')
            {
              string& id (t.value);
              id.clear ();

              for (id += c; (c = peek ()) == '_' || alnum (c); get (c))
                id += c;

              // If the following character is a quote, see if the identifier
              // is one of the literal prefixes.
              //
              if (c == '\'' || c == '\"')
              {
                size_t n (id.size ()), i (0);
                switch (id[0])
                {
                case 'u':
                  {
                    if (n > 1 && id[1] == '8')
                      ++i;
                    // Fall through.
                  }
                case 'L':
                case 'U':
                  {
                    ++i;

                    if (c == '\"' && n > i && id[i] == 'R')
                    {
                      ++i;
                      raw = true;
                    }
                    break;
                  }
                case 'R':
                  {
                    if (c == '\"')
                    {
                      ++i;
                      raw = true;
                    }
                    break;
                  }
                }

                if (i == n) // All characters "consumed".
                {
                  get (c);
                  id.clear ();
                }
              }

              if (!id.empty ())
              {
                t.type = type::identifier;
                return;
              }
            }

            switch (c)
            {
            case '\'':
              {
                char_literal (t, c);
                return;
              }
            case '\"':
              {
                if (raw)
                  raw_string_literal (t, c);
                else
                  string_literal (t, c);
                return;
              }
            default:
              {
                t.type = type::other;
                return;
              }
            }
          }
        }
      }
    }

    void lexer::
    number_literal (token& t, xchar c)
    {
      t.line = c.line;
      t.column = c.column;

      // A number (integer or floating point literal) can:
      //
      // 1. Start with a dot (which must be followed by a digit, e.g., .123).
      //
      // 2. Can have a radix prefix (0b101, 0123, 0X12AB).
      //
      // 3. Can have an exponent (1e10, 0x1.p-10, 1.).
      //
      // 4. Digits can be separated with ' (123'456, 0xff00'00ff).
      //
      // 5. End with a built-in or user defined literal (123f, 123UL, 123_X)
      //
      // Quoting from GCC's preprocessor documentation:
      //
      // "Formally preprocessing numbers begin with an optional period, a
      // required decimal digit, and then continue with any sequence of
      // letters, digits, underscores, periods, and exponents. Exponents are
      // the two-character sequences 'e+', 'e-', 'E+', 'E-', 'p+', 'p-', 'P+',
      // and 'P-'."
      //
      // So it looks like a "C++ number" is then any unseparated (with
      // whitespace or punctuation) sequence of those plus '. The only mildly
      // tricky part is then to recognize +/- as being part of the exponent.
      //
      while (!eos ((c = peek ())))
      {
        switch (c)
        {
          // All the whitespace, punctuation, and other characters that end
          // the number.
          //
        case ' ':
        case '\n':
        case '\t':
        case '\r':
        case '\f':
        case '\v':

        case '#':
        case ';':
        case '{':
        case '}':
        case '(':
        case ')':
        case '[':
        case ']':
        case ',':
        case '?':
        case '~':
        case '=':
        case '!':
        case '*':
        case '/':
        case '%':
        case '^':
        case '>':
        case '<':
        case '&':
        case '|':
        case ':':
        case '+': // The exponent case is handled below.
        case '-': // The exponent case is handled below.
        case '"':
        case '\\':

        case '@':
        case '$':
        case '`':
          break;

          // Recognize +/- after the exponent.
          //
        case 'e':
        case 'E':
        case 'p':
        case 'P':
          {
            get (c);
            c = peek ();
            if (c == '+' || c == '-')
              get (c);
            continue;
          }

        case '_':
        case '.':
        case '\'':
        default: // Digits and letters.
          {
            get (c);
            continue;
          }
        }

        break;
      }

      t.type = type::number;
    }

    void lexer::
    char_literal (token& t, xchar c)
    {
      t.line = c.line;
      t.column = c.column;

      char p (c); // Previous character (see below).

      for (;;)
      {
        c = get ();

        if (eos (c))
          fail (location (&name_, t.line, t.column)) << "unterminated literal";

        if (c == '\'' && p != '\\')
          break;

        // Keep track of \\-escapings so we don't confuse them with \', as in
        // '\\'.
        //
        p = (c == '\\' && p == '\\') ? '\0' : static_cast<char> (c);
      }

      // See if we have a user-defined suffix (which is an identifier).
      //
      if ((c = peek ()) == '_' || alpha (c))
        literal_suffix (c);

      t.type = type::character;
    }

    void lexer::
    string_literal (token& t, xchar c)
    {
      t.line = c.line;
      t.column = c.column;

      char p (c); // Previous character (see below).

      for (;;)
      {
        c = get ();

        if (eos (c))
          fail (location (&name_, t.line, t.column)) << "unterminated literal";

        if (c == '\"' && p != '\\')
          break;

        // Keep track of \\-escapings so we don't confuse them with \", as in
        // "\\".
        //
        p = (c == '\\' && p == '\\') ? '\0' : static_cast<char> (c);
      }

      // See if we have a user-defined suffix (which is an identifier).
      //
      if ((c = peek ()) == '_' || alpha (c))
        literal_suffix (c);

      t.type = type::string;
    }

    void lexer::
    raw_string_literal (token& t, xchar c)
    {
      t.line = c.line;
      t.column = c.column;

      // The overall form is:
      //
      // R"<delimiter>(<raw_characters>)<delimiter>"
      //
      // Where <delimiter> is a potentially-empty character sequence made of
      // any source character but parentheses, backslash and spaces. It can be
      // at most 16 characters long.
      //
      // Note that the <raw_characters> are not processed in any way, not even
      // for line continuations.
      //

      // As a first step, parse the delimiter (including the openning paren).
      //
      string d (1, ')');

      for (;;)
      {
        c = get ();

        if (eos (c) || c == '\"' || c == ')' || c == '\\' || c == ' ')
          fail (location (&name_, t.line, t.column)) << "invalid raw literal";

        if (c == '(')
          break;

        d += c;
      }

      d += '"';

      // Now parse the raw characters while trying to match the closing
      // delimiter.
      //
      for (size_t i (0);;) // Position to match in d.
      {
        c = get (false); // No newline escaping.

        if (eos (c))
          fail (location (&name_, t.line, t.column)) << "invalid raw literal";

        if (c != d[i] && i != 0) // Restart from the beginning.
          i = 0;

        if (c == d[i])
        {
          if (++i == d.size ())
            break;
        }
      }

      // See if we have a user-defined suffix (which is an identifier).
      //
      if ((c = peek ()) == '_' || alpha (c))
        literal_suffix (c);

      t.type = type::string;
    }

    void lexer::
    literal_suffix (xchar c)
    {
      // Parse a user-defined literal suffix identifier.
      //
      for (get (c); (c = peek ()) == '_' || alnum (c); get (c)) ;
    }

    auto lexer::
    skip_spaces (bool nl) -> xchar
    {
      xchar c (get ());

      for (; !eos (c); c = get ())
      {
        switch (c)
        {
        case '\n':
          {
            if (!nl)
              break;

            // Fall through.
          }
        case ' ':
        case '\t':
        case '\r':
        case '\f':
        case '\v': continue;

        case '/':
          {
            xchar p (peek ());

            // C++ comment.
            //
            if (p == '/')
            {
              get (p);
              do { c = get (); } while (!eos (c) && c != '\n');

              if (!nl)
                break;

              continue;
            }

            // C comment.
            //
            if (p == '*')
            {
              get (p);

              for (;;)
              {
                c = get ();

                if (eos (c))
                  fail (p) << "unterminated comment";

                if (c == '*' && (c = peek ()) == '/')
                {
                  get (c);
                  break;
                }
              }
              continue;
            }
            break;
          }
        }
        break;
      }

      return c;
    }

    ostream&
    operator<< (ostream& o, const token& t)
    {
      switch (t.type)
      {
      case type::dot:         o << "'.'";                   break;
      case type::semi:        o << "';'";                   break;
      case type::lcbrace:     o << "'{'";                   break;
      case type::rcbrace:     o << "'}'";                   break;
      case type::punctuation: o << "<punctuation>";         break;

      case type::identifier:  o << '\'' << t.value << '\''; break;

      case type::number:      o << "<number literal>";      break;
      case type::character:   o << "<char literal>";        break;
      case type::string:      o << "<string literal>";      break;

      case type::other:       o << "<other>";               break;
      case type::eos:         o << "<end of file>";         break;
      }

      return o;
    }
  }
}