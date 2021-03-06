// file      : libbuild2/dist/module.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef LIBBUILD2_DIST_MODULE_HXX
#define LIBBUILD2_DIST_MODULE_HXX

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/module.hxx>
#include <libbuild2/variable.hxx>

#include <libbuild2/export.hxx>

namespace build2
{
  namespace dist
  {
    struct LIBBUILD2_SYMEXPORT module: build2::module
    {
      static const string name;

      const variable& var_dist_package;

      // Distribution post-processing callbacks.
      //
      // The last component in the pattern may contain shell wildcards. If the
      // path contains a directory, then it is matched from the distribution
      // root only. Otherwise, it is matched against all the files being
      // distributed. For example:
      //
      // buildfile        - every buildfile
      // ./buildfile      - root buildfile only
      // tests/buildfile  - tests/buildfile only
      //
      // The callback is called with the absolute path of the matching file
      // after it has been copied to the distribution directory. The project's
      // root scope and callback-specific data are passed along.
      //
      // Note that if registered, the callbacks are also called (recursively)
      // in subprojects.
      //
      using callback_func = void (const path&, const scope&, void*);

      void
      register_callback (path pattern, callback_func* f, void* data)
      {
        callbacks_.push_back (callback {move (pattern), f, data});
      }

      // Implementation details.
      //
      module (const variable& v_d_p)
          : var_dist_package (v_d_p) {}

    public:
      struct callback
      {
        const path     pattern;
        callback_func* function;
        void*          data;
      };
      using callbacks = vector<callback>;

      callbacks callbacks_;
    };
  }
}

#endif // LIBBUILD2_DIST_MODULE_HXX
