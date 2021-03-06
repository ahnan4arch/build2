// -*- C++ -*-
//
// This file was generated by CLI, a command line interface
// compiler for C++.
//

#ifndef LIBBUILD2_TEST_SCRIPT_BUILTIN_OPTIONS_HXX
#define LIBBUILD2_TEST_SCRIPT_BUILTIN_OPTIONS_HXX

// Begin prologue.
//
//
// End prologue.

#include <vector>
#include <iosfwd>
#include <string>
#include <cstddef>
#include <exception>

#ifndef CLI_POTENTIALLY_UNUSED
#  if defined(_MSC_VER) || defined(__xlC__)
#    define CLI_POTENTIALLY_UNUSED(x) (void*)&x
#  else
#    define CLI_POTENTIALLY_UNUSED(x) (void)x
#  endif
#endif

namespace build2
{
  namespace test
  {
    namespace script
    {
      namespace cli
      {
        class unknown_mode
        {
          public:
          enum value
          {
            skip,
            stop,
            fail
          };

          unknown_mode (value);

          operator value () const 
          {
            return v_;
          }

          private:
          value v_;
        };

        // Exceptions.
        //

        class exception: public std::exception
        {
          public:
          virtual void
          print (::std::ostream&) const = 0;
        };

        ::std::ostream&
        operator<< (::std::ostream&, const exception&);

        class unknown_option: public exception
        {
          public:
          virtual
          ~unknown_option () throw ();

          unknown_option (const std::string& option);

          const std::string&
          option () const;

          virtual void
          print (::std::ostream&) const;

          virtual const char*
          what () const throw ();

          private:
          std::string option_;
        };

        class unknown_argument: public exception
        {
          public:
          virtual
          ~unknown_argument () throw ();

          unknown_argument (const std::string& argument);

          const std::string&
          argument () const;

          virtual void
          print (::std::ostream&) const;

          virtual const char*
          what () const throw ();

          private:
          std::string argument_;
        };

        class missing_value: public exception
        {
          public:
          virtual
          ~missing_value () throw ();

          missing_value (const std::string& option);

          const std::string&
          option () const;

          virtual void
          print (::std::ostream&) const;

          virtual const char*
          what () const throw ();

          private:
          std::string option_;
        };

        class invalid_value: public exception
        {
          public:
          virtual
          ~invalid_value () throw ();

          invalid_value (const std::string& option,
                         const std::string& value,
                         const std::string& message = std::string ());

          const std::string&
          option () const;

          const std::string&
          value () const;

          const std::string&
          message () const;

          virtual void
          print (::std::ostream&) const;

          virtual const char*
          what () const throw ();

          private:
          std::string option_;
          std::string value_;
          std::string message_;
        };

        class eos_reached: public exception
        {
          public:
          virtual void
          print (::std::ostream&) const;

          virtual const char*
          what () const throw ();
        };

        // Command line argument scanner interface.
        //
        // The values returned by next() are guaranteed to be valid
        // for the two previous arguments up until a call to a third
        // peek() or next().
        //
        class scanner
        {
          public:
          virtual
          ~scanner ();

          virtual bool
          more () = 0;

          virtual const char*
          peek () = 0;

          virtual const char*
          next () = 0;

          virtual void
          skip () = 0;
        };

        class argv_scanner: public scanner
        {
          public:
          argv_scanner (int& argc, char** argv, bool erase = false);
          argv_scanner (int start, int& argc, char** argv, bool erase = false);

          int
          end () const;

          virtual bool
          more ();

          virtual const char*
          peek ();

          virtual const char*
          next ();

          virtual void
          skip ();

          private:
          int i_;
          int& argc_;
          char** argv_;
          bool erase_;
        };

        class vector_scanner: public scanner
        {
          public:
          vector_scanner (const std::vector<std::string>&, std::size_t start = 0);

          std::size_t
          end () const;

          void
          reset (std::size_t start = 0);

          virtual bool
          more ();

          virtual const char*
          peek ();

          virtual const char*
          next ();

          virtual void
          skip ();

          private:
          const std::vector<std::string>& v_;
          std::size_t i_;
        };

        template <typename X>
        struct parser;
      }
    }
  }
}

namespace build2
{
  namespace test
  {
    namespace script
    {
      class set_options
      {
        public:
        set_options ();

        set_options (int& argc,
                     char** argv,
                     bool erase = false,
                     ::build2::test::script::cli::unknown_mode option = ::build2::test::script::cli::unknown_mode::fail,
                     ::build2::test::script::cli::unknown_mode argument = ::build2::test::script::cli::unknown_mode::stop);

        set_options (int start,
                     int& argc,
                     char** argv,
                     bool erase = false,
                     ::build2::test::script::cli::unknown_mode option = ::build2::test::script::cli::unknown_mode::fail,
                     ::build2::test::script::cli::unknown_mode argument = ::build2::test::script::cli::unknown_mode::stop);

        set_options (int& argc,
                     char** argv,
                     int& end,
                     bool erase = false,
                     ::build2::test::script::cli::unknown_mode option = ::build2::test::script::cli::unknown_mode::fail,
                     ::build2::test::script::cli::unknown_mode argument = ::build2::test::script::cli::unknown_mode::stop);

        set_options (int start,
                     int& argc,
                     char** argv,
                     int& end,
                     bool erase = false,
                     ::build2::test::script::cli::unknown_mode option = ::build2::test::script::cli::unknown_mode::fail,
                     ::build2::test::script::cli::unknown_mode argument = ::build2::test::script::cli::unknown_mode::stop);

        set_options (::build2::test::script::cli::scanner&,
                     ::build2::test::script::cli::unknown_mode option = ::build2::test::script::cli::unknown_mode::fail,
                     ::build2::test::script::cli::unknown_mode argument = ::build2::test::script::cli::unknown_mode::stop);

        // Option accessors.
        //
        const bool&
        exact () const;

        const bool&
        newline () const;

        const bool&
        whitespace () const;

        // Implementation details.
        //
        protected:
        bool
        _parse (const char*, ::build2::test::script::cli::scanner&);

        private:
        bool
        _parse (::build2::test::script::cli::scanner&,
                ::build2::test::script::cli::unknown_mode option,
                ::build2::test::script::cli::unknown_mode argument);

        public:
        bool exact_;
        bool newline_;
        bool whitespace_;
      };
    }
  }
}

#include <libbuild2/test/script/builtin-options.ixx>

// Begin epilogue.
//
//
// End epilogue.

#endif // LIBBUILD2_TEST_SCRIPT_BUILTIN_OPTIONS_HXX
