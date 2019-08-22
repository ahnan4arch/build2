// file      : build2/cxx/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/cxx/init.hxx>

#include <libbuild2/scope.hxx>
#include <libbuild2/diagnostics.hxx>

#include <build2/cc/guess.hxx>
#include <build2/cc/module.hxx>

#include <build2/cxx/target.hxx>

#ifndef BUILD2_DEFAULT_CXX
#  ifdef BUILD2_NATIVE_CXX
#    define BUILD2_DEFAULT_CXX BUILD2_NATIVE_CXX
#  else
#    define BUILD2_DEFAULT_CXX ""
#  endif
#endif

using namespace std;
using namespace butl;

namespace build2
{
  namespace cxx
  {
    using cc::compiler_id;
    using cc::compiler_type;
    using cc::compiler_class;
    using cc::compiler_info;

    class config_module: public cc::config_module
    {
    public:
      explicit
      config_module (config_data&& d)
          : config_data (move (d)), cc::config_module (move (d)) {}

      virtual strings
      translate_std (const compiler_info&,
                     scope&,
                     const string*) const override;
    };

    using cc::module;

    strings config_module::
    translate_std (const compiler_info& ci, scope& rs, const string* v) const
    {
      strings r;

      compiler_type ct (ci.id.type);
      compiler_class cl (ci.class_);
      uint64_t mj (ci.version.major);
      uint64_t mi (ci.version.minor);
      uint64_t p (ci.version.patch);

      // Features.
      //
      auto enter = [&rs] (const char* v) -> const variable&
      {
        return rs.ctx.var_pool.rw (rs).insert<bool> (
          v, variable_visibility::project);
      };

      // NOTE: see also module sidebuild subproject if changing anything about
      // modules here.

      //bool concepts (false); auto& v_c (enter ("cxx.features.concepts"));
      bool modules (false); auto& v_m (enter ("cxx.features.modules"));

      // Translate "latest" and "experimental" to the compiler/version-
      // appropriate option(s). Experimental is normally like latest with
      // extra stuff enabled via additional flags. Otherwise translate the
      // standard value.
      //
      bool experimental (                 v != nullptr && *v == "experimental");
      bool latest       (experimental || (v != nullptr && *v == "latest"));

      string o;

      switch (cl)
      {
      case compiler_class::msvc:
        {
          // C++ standard-wise, with VC you got what you got up until 14.2.
          // Starting with 14.3 there is now the /std: switch which defaults
          // to c++14 but can be set to c++latest. And from 15.3 it can be
          // c++17.
          //
          // The question is also whether we should verify that the requested
          // standard is provided by this VC version. And if so, from which
          // version should we say VC supports 11, 14, and 17? We should
          // probably be as loose as possible here since the author will
          // always be able to tighten (but not loosen) this in the buildfile
          // (i.e., detect unsupported versions).
          //
          // For now we are not going to bother doing this for C++03.
          //
          if (latest)
          {
            if (mj > 19 || (mj == 19 && (mi > 0 || (mi == 0 && p >= 24215))))
              o = "/std:c++latest";
          }
          else if (v == nullptr)
            ;
          else if (*v != "98" && *v != "03")
          {
            bool sup (false);

            if      (*v == "11") // C++11 since VS2010/10.0.
            {
              sup = mj >= 16;
            }
            else if (*v == "14") // C++14 since VS2015/14.0.
            {
              sup = mj >= 19;
            }
            else if (*v == "17") // C++17 since VS2015/14.0u2.
            {
              // Note: the VC15 compiler version is 19.10.
              //
              sup = (mj > 19 ||
                     (mj == 19 && (mi > 0 || (mi == 0 && p >= 23918))));
            }

            if (!sup)
              fail << "C++" << *v << " is not supported by " << ci.signature <<
                info << "required by " << project (rs) << '@' << rs;

            if (mj > 19 || (mj == 19 && mi >= 11)) // 15.3
            {
              if      (*v == "14") o = "/std:c++14";
              else if (*v == "17") o = "/std:c++17";
            }
            else if (mj == 19 && (mi > 0 || (mi == 0 && p >= 24215))) // 14.3
            {
              if      (*v == "14") o = "/std:c++14";
              else if (*v == "17") o = "/std:c++latest";
            }
          }

          if (!o.empty ())
            r.push_back (move (o));

          break;
        }
      case compiler_class::gcc:
        {
          if (latest)
          {
            switch (ct)
            {
            case compiler_type::gcc:
              {
                if      (mj >= 8)            o = "-std=c++2a"; // 20
                else if (mj >= 5)            o = "-std=c++1z"; // 17
                else if (mj == 4 && mi >= 8) o = "-std=c++1y"; // 14
                else if (mj == 4 && mi >= 4) o = "-std=c++0x"; // 11

                break;
              }
            case compiler_type::clang:
              {
                // Remap Apple versions to vanilla Clang based on the
                // following release point. Note that Apple no longer
                // discloses the mapping so it's a guesswork and we try to be
                // conservative. For details see:
                //
                // https://gist.github.com/yamaya/2924292
                //
                // 5.1  -> 3.4
                // 6.0  -> 3.5
                // 7.0  -> 3.7
                // 7.3  -> 3.8
                // 8.0  -> 3.9
                // 9.0  -> 4.0 (later ones could be 5.0)
                // 9.1  -> ?
                // 10.0 -> ?
                //
                // Note that this mapping is also used to enable experimental
                // features below.
                //
                if (ci.id.variant == "apple")
                {
                  if      (mj >= 9)            {mj = 4; mi = 0;}
                  else if (mj == 8)            {mj = 3; mi = 9;}
                  else if (mj == 7 && mi >= 3) {mj = 3; mi = 8;}
                  else if (mj == 7)            {mj = 3; mi = 7;}
                  else if (mj == 6)            {mj = 3; mi = 5;}
                  else if (mj == 5 && mi >= 1) {mj = 3; mi = 4;}
                  else                         {mj = 3; mi = 0;}
                }

                if       (mj >= 5)                         o = "-std=c++2a";
                else if  (mj >  3 || (mj == 3 && mi >= 5)) o = "-std=c++1z";
                else if  (mj == 3 && mi >= 4)              o = "-std=c++1y";
                else     /* ??? */                         o = "-std=c++0x";

                break;
              }
            case compiler_type::icc:
              {
                if      (mj >= 17)                         o = "-std=c++1z";
                else if (mj >  15 || (mj == 15 && p >= 3)) o = "-std=c++1y";
                else    /* ??? */                          o = "-std=c++0x";

                break;
              }
            default:
              assert (false);
            }
          }
          else if (v == nullptr)
            ;
          else
          {
            // Translate 11 to 0x, 14 to 1y, 17 to 1z, and 20 to 2a for
            // compatibility with older versions of the compilers.
            //
            o = "-std=";

            if      (*v == "98") o += "c++98";
            else if (*v == "03") o += "c++03";
            else if (*v == "11") o += "c++0x";
            else if (*v == "14") o += "c++1y";
            else if (*v == "17") o += "c++1z";
            else if (*v == "20") o += "c++2a";
            else o += *v; // In case the user specifies e.g., 'gnu++17'.
          }

          if (!o.empty ())
            r.push_back (move (o));

          break;
        }
      }

      if (experimental)
      {
        switch (ct)
        {
        case compiler_type::msvc:
          {
            // Starting with 15.5 (19.12) Visual Studio-created projects
            // default to the strict mode. However, this flag currently tends
            // to trigger too many compiler bugs. So for now we leave it
            // to the experimenters to enjoy.
            //
            if (mj > 19 || (mj == 19 && mi >= 12))
              r.push_back ("/permissive-");

            break;
          }
        default:
          break;
        }

        // Unless disabled by the user, try to enable C++ modules. Here we use
        // a tri-state:
        //
        // - false        - disabled
        // - unspecified  - enabled if practically usable
        // - true         - enabled even if practically unusable
        //
        lookup l;
        if (!(l = rs[v_m]) || cast<bool> (l))
        {
          switch (ct)
          {
          case compiler_type::msvc:
            {
              // While modules are supported in VC15.0 (19.10), there is a bug
              // in separate interface/implementation unit support which makes
              // them pretty much unusable. This has been fixed in VC15.3
              // (19.11). And VC15.5 (19.12) supports the 'export module M;'
              // syntax.
              //
              if (mj > 19 || (mj == 19 && mi >= (l ? 10 : 12)))
              {
                r.push_back (
                  mj > 19 || mi > 11
                  ? "/D__cpp_modules=201704"   // p0629r0 (export module M;)
                  : "/D__cpp_modules=201703"); // n4647   (       module M;)

                r.push_back ("/experimental:module");
                modules = true;
              }
              break;
            }
          case compiler_type::gcc:
            {
              // We now use extended/experimental module mapper support which
              // is currently only available in our c++-modules-ex branch.
              // But let's allow forcing it to plain c++-modules in case
              // things got merged, etc.
              //
              if (mj >= 10 &&
                  ci.version.build.find (l
                                         ? "c++-modules"
                                         : "c++-modules-ex") != string::npos)
              {
                // Currently defines __cpp_modules=201810 which is said to
                // correspond to p1103 (merged modules).
                //
                r.push_back ("-fmodules-ts");
                modules = true;
              }
              break;
            }
          case compiler_type::clang:
            {
              // Enable starting with Clang 6.0.0.
              //
              // Note that we are using Apple to vanilla Clang version re-map
              // from above so may need to update things there as well.
              //
              // Also see Clang modules support hack in cc::compile.
              //
              // @@ Clang 9 enables modules by default in C++2a. We should
              //    probably reflect this in the modules value.
              //
              if (mj >= 6)
              {
                r.push_back ("-D__cpp_modules=201704"); // p0629r0
                r.push_back ("-fmodules-ts");
                modules = true;
              }
              break;
            }
          case compiler_type::icc:
            break; // No modules support yet.
          }
        }
      }

      rs.assign (v_m) = modules;
      //rs.assign (v_c) = concepts;

      return r;
    }

    static const char* const hinters[] = {"c", nullptr};

    // See cc::module for details on guess_init vs config_init.
    //
    bool
    guess_init (scope& rs,
                scope& bs,
                const location& loc,
                unique_ptr<module_base>& mod,
                bool,
                bool,
                const variable_map& hints)
    {
      tracer trace ("cxx::guess_init");
      l5 ([&]{trace << "for " << bs;});

      // We only support root loading (which means there can only be one).
      //
      if (&rs != &bs)
        fail (loc) << "cxx.guess module must be loaded in project root";

      // Load cc.core.vars so that we can cache all the cc.* variables.
      //
      if (!cast_false<bool> (rs["cc.core.vars.loaded"]))
        load_module (rs, rs, "cc.core.vars", loc);

      // Enter all the variables and initialize the module data.
      //
      auto& v (rs.ctx.var_pool.rw (rs));

      cc::config_data d {
        cc::lang::cxx,

        "cxx",
        "c++",
        BUILD2_DEFAULT_CXX,
        ".ii",

        hinters,

        // Note: some overridable, some not.
        //
        v.insert<path>    ("config.cxx",          true),
        v.insert<string>  ("config.cxx.id",       true),
        v.insert<string>  ("config.cxx.version",  true),
        v.insert<string>  ("config.cxx.target",   true),
        v.insert<string>  ("config.cxx.std",      true),
        v.insert<strings> ("config.cxx.poptions", true),
        v.insert<strings> ("config.cxx.coptions", true),
        v.insert<strings> ("config.cxx.loptions", true),
        v.insert<strings> ("config.cxx.aoptions", true),
        v.insert<strings> ("config.cxx.libs",     true),

        // List of importable headers. Inclusion of such headers is translated
        // to the corresponding header unit imports.
        //
        // A header can be specified either as an absolute and normalized path
        // or as a <>-style include name. The latter kind is automatically
        // translated to the absolute form based on the compiler's system
        // header search paths (as opposed to -I). Note also that all entries
        // must be specified before loading the cxx module.
        //
        &v.insert<strings> ("config.cxx.importable_headers", true),

        v.insert<process_path> ("cxx.path"),
        v.insert<dir_paths>    ("cxx.sys_lib_dirs"),
        v.insert<dir_paths>    ("cxx.sys_inc_dirs"),

        v.insert<string>   ("cxx.std", variable_visibility::project),

        v.insert<strings>  ("cxx.poptions"),
        v.insert<strings>  ("cxx.coptions"),
        v.insert<strings>  ("cxx.loptions"),
        v.insert<strings>  ("cxx.aoptions"),
        v.insert<strings>  ("cxx.libs"),

        &v.insert<strings> ("cxx.importable_headers"),

        v["cc.poptions"],
        v["cc.coptions"],
        v["cc.loptions"],
        v["cc.aoptions"],
        v["cc.libs"],

        v.insert<strings>      ("cxx.export.poptions"),
        v.insert<strings>      ("cxx.export.coptions"),
        v.insert<strings>      ("cxx.export.loptions"),
        v.insert<vector<name>> ("cxx.export.libs"),

        v["cc.export.poptions"],
        v["cc.export.coptions"],
        v["cc.export.loptions"],
        v["cc.export.libs"],

        v.insert<string> ("cxx.stdlib"),

        v["cc.runtime"],
        v["cc.stdlib"],

        v["cc.type"],
        v["cc.system"],
        v["cc.module_name"],
        v["cc.reprocess"],

        // Ability to signal that source is already (partially) preprocessed.
        // Valid values are 'none' (not preprocessed), 'includes' (no #include
        // directives in source), 'modules' (as above plus no module
        // declaration depends on preprocessor, e.g., #ifdef, etc), and 'all'
        // (the source is fully preprocessed). Note that for 'all' the source
        // can still contain comments and line continuations. Note also that
        // for some compilers (e.g., VC) there is no way to signal that the
        // source is already preprocessed.
        //
        // What about header unit imports? Well, they are in a sense
        // standardized precompiled headers so we treat them as includes.
        //
        v.insert<string>   ("cxx.preprocessed"),

        nullptr, // cxx.features.symexport (set in init() below).

        v.insert<string>   ("cxx.id"),
        v.insert<string>   ("cxx.id.type"),
        v.insert<string>   ("cxx.id.variant"),

        v.insert<string>   ("cxx.class"),

        v.insert<string>   ("cxx.version"),
        v.insert<uint64_t> ("cxx.version.major"),
        v.insert<uint64_t> ("cxx.version.minor"),
        v.insert<uint64_t> ("cxx.version.patch"),
        v.insert<string>   ("cxx.version.build"),

        v.insert<string>   ("cxx.signature"),
        v.insert<string>   ("cxx.checksum"),

        v.insert<string>   ("cxx.pattern"),

        v.insert<target_triplet> ("cxx.target"),

        v.insert<string>   ("cxx.target.cpu"),
        v.insert<string>   ("cxx.target.vendor"),
        v.insert<string>   ("cxx.target.system"),
        v.insert<string>   ("cxx.target.version"),
        v.insert<string>   ("cxx.target.class")
      };

      // Alias some cc. variables as cxx.
      //
      v.insert_alias (d.c_runtime, "cxx.runtime");
      v.insert_alias (d.c_module_name, "cxx.module_name");

      assert (mod == nullptr);
      config_module* m (new config_module (move (d)));
      mod.reset (m);
      m->guess (rs, loc, hints);
      return true;
    }

    bool
    config_init (scope& rs,
                 scope& bs,
                 const location& loc,
                 unique_ptr<module_base>&,
                 bool,
                 bool,
                 const variable_map& hints)
    {
      tracer trace ("cxx::config_init");
      l5 ([&]{trace << "for " << bs;});

      // We only support root loading (which means there can only be one).
      //
      if (&rs != &bs)
        fail (loc) << "cxx.config module must be loaded in project root";

      // Load cxx.guess.
      //
      if (!cast_false<bool> (rs["cxx.guess.loaded"]))
        load_module (rs, rs, "cxx.guess", loc, false, hints);

      config_module& cm (*rs.lookup_module<config_module> ("cxx.guess"));
      cm.init (rs, loc, hints);
      return true;
    }

    static const target_type* const hdr[] =
    {
      &hxx::static_type,
      &ixx::static_type,
      &txx::static_type,
      &mxx::static_type,
      nullptr
    };

    static const target_type* const inc[] =
    {
      &hxx::static_type,
      &h::static_type,
      &ixx::static_type,
      &txx::static_type,
      &mxx::static_type,
      &cxx::static_type,
      &c::static_type,
      nullptr
    };

    bool
    init (scope& rs,
          scope& bs,
          const location& loc,
          unique_ptr<module_base>& mod,
          bool,
          bool,
          const variable_map& hints)
    {
      tracer trace ("cxx::init");
      l5 ([&]{trace << "for " << bs;});

      // We only support root loading (which means there can only be one).
      //
      if (&rs != &bs)
        fail (loc) << "cxx module must be loaded in project root";

      // Load cxx.config.
      //
      if (!cast_false<bool> (rs["cxx.config.loaded"]))
        load_module (rs, rs, "cxx.config", loc, false, hints);

      config_module& cm (*rs.lookup_module<config_module> ("cxx.guess"));

      auto& vp (rs.ctx.var_pool.rw (rs));

      bool modules (cast<bool> (rs["cxx.features.modules"]));

      bool symexport (false);
      if (modules)
      {
        auto& var (vp.insert<bool> ("cxx.features.symexport",
                                    variable_visibility::project));
        symexport = cast_false<bool> (rs[var]);
        cm.x_symexport = &var;
      }

      cc::data d {
        cm,

        "cxx.compile",
        "cxx.link",
        "cxx.install",
        "cxx.uninstall",

        cm.ci_->id.type,
        cm.ci_->id.variant,
        cm.ci_->class_,
        cm.ci_->version.major,
        cm.ci_->version.minor,
        cast<process_path>   (rs[cm.x_path]),
        cast<target_triplet> (rs[cm.x_target]),

        cm.tstd,

        modules,
        symexport,

        cast<dir_paths> (rs[cm.x_sys_lib_dirs]),
        cast<dir_paths> (rs[cm.x_sys_inc_dirs]),

        cm.sys_lib_dirs_extra,
        cm.sys_inc_dirs_extra,

        cxx::static_type,
        modules ? &mxx::static_type : nullptr,
        hdr,
        inc
      };

      assert (mod == nullptr);
      module* m;
      mod.reset (m = new module (move (d)));
      m->init (rs, loc, hints);
      return true;
    }
  }
}
