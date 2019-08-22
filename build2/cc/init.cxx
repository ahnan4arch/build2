// file      : build2/cc/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/cc/init.hxx>

#include <libbuild2/file.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/filesystem.hxx>
#include <libbuild2/diagnostics.hxx>

#include <libbuild2/config/utility.hxx>

#include <build2/cc/target.hxx>
#include <build2/cc/utility.hxx>

using namespace std;
using namespace butl;

namespace build2
{
  namespace cc
  {
    // Scope operation callback that cleans up module sidebuilds.
    //
    static target_state
    clean_module_sidebuilds (action, const scope& rs, const dir&)
    {
      const dir_path& out_root (rs.out_path ());

      dir_path d (out_root / rs.root_extra->build_dir / modules_sidebuild_dir);

      if (exists (d))
      {
        if (build2::rmdir_r (d))
        {
          // Clean up cc/ if it became empty.
          //
          d = out_root / rs.root_extra->build_dir / module_dir;
          if (empty (d))
          {
            rmdir (d);

            // And build/ if it also became empty (e.g., in case of a build
            // with a transient configuration).
            //
            d = out_root / rs.root_extra->build_dir;
            if (empty (d))
              rmdir (d);
          }

          return target_state::changed;
        }
      }

      return target_state::unchanged;
    }

    bool
    core_vars_init (scope& rs,
                    scope&,
                    const location& loc,
                    unique_ptr<module_base>&,
                    bool first,
                    bool,
                    const variable_map&)
    {
      tracer trace ("cc::core_vars_init");
      l5 ([&]{trace << "for " << rs;});

      assert (first);

      // Load bin.vars (we need its config.bin.target/pattern for hints).
      //
      if (!cast_false<bool> (rs["bin.vars.loaded"]))
        load_module (rs, rs, "bin.vars", loc);

      // Enter variables. Note: some overridable, some not.
      //
      auto& v (rs.ctx.var_pool.rw (rs));

      auto v_t (variable_visibility::target);

      v.insert<strings> ("config.cc.poptions", true);
      v.insert<strings> ("config.cc.coptions", true);
      v.insert<strings> ("config.cc.loptions", true);
      v.insert<strings> ("config.cc.aoptions", true);
      v.insert<strings> ("config.cc.libs",     true);

      v.insert<strings> ("cc.poptions");
      v.insert<strings> ("cc.coptions");
      v.insert<strings> ("cc.loptions");
      v.insert<strings> ("cc.aoptions");
      v.insert<strings> ("cc.libs");

      v.insert<strings>      ("cc.export.poptions");
      v.insert<strings>      ("cc.export.coptions");
      v.insert<strings>      ("cc.export.loptions");
      v.insert<vector<name>> ("cc.export.libs");

      // Hint variables (not overridable).
      //
      v.insert<string>         ("config.cc.id");
      v.insert<string>         ("config.cc.hinter"); // Hinting module.
      v.insert<string>         ("config.cc.pattern");
      v.insert<target_triplet> ("config.cc.target");

      // Compiler runtime and C standard library.
      //
      v.insert<string> ("cc.runtime");
      v.insert<string> ("cc.stdlib");

      // Target type, for example, "C library" or "C++ library". Should be set
      // on the target as a rule-specific variable by the matching rule to the
      // name of the module (e.g., "c", "cxx"). Currenly only set for
      // libraries and is used to decide which *.libs to use during static
      // linking.
      //
      // It can also be the special "cc" value which means a C-common library
      // but specific language is not known. Used in the import installed
      // logic.
      //
      v.insert<string> ("cc.type", v_t);

      // If set and is true, then this (imported) library has been found in a
      // system library search directory.
      //
      v.insert<bool> ("cc.system", v_t);

      // C++ module name. Set on the bmi*{} target as a rule-specific variable
      // by the matching rule. Can also be set by the user (normally via the
      // x.module_name alias) on the x_mod{} source.
      //
      v.insert<string> ("cc.module_name", v_t);

      // Ability to disable using preprocessed output for compilation.
      //
      v.insert<bool> ("config.cc.reprocess", true);
      v.insert<bool> ("cc.reprocess");

      // Register scope operation callback.
      //
      // It feels natural to do clean up sidebuilds as a post operation but
      // that prevents the (otherwise-empty) out root directory to be cleaned
      // up (via the standard fsdir{} chain).
      //
      rs.operation_callbacks.emplace (
        perform_clean_id,
        scope::operation_callback {&clean_module_sidebuilds, nullptr /*post*/});

      return true;
    }

    bool
    core_guess_init (scope& rs,
                     scope&,
                     const location& loc,
                     unique_ptr<module_base>&,
                     bool first,
                     bool,
                     const variable_map& h)
    {
      tracer trace ("cc::core_guess_init");
      l5 ([&]{trace << "for " << rs;});

      assert (first);

      // Load cc.core.vars.
      //
      if (!cast_false<bool> (rs["cc.core.vars.loaded"]))
        load_module (rs, rs, "cc.core.vars", loc);

      // config.cc.{id,hinter}
      //
      {
        // These values must be hinted.
        //
        rs.assign<string> ("cc.id") = cast<string> (h["config.cc.id"]);
        rs.assign<string> ("cc.hinter") = cast<string> (h["config.cc.hinter"]);
      }

      // config.cc.target
      //
      {
        // This value must be hinted.
        //
        const auto& t (cast<target_triplet> (h["config.cc.target"]));

        // Also enter as cc.target.{cpu,vendor,system,version,class} for
        // convenience of access.
        //
        rs.assign<string> ("cc.target.cpu")     = t.cpu;
        rs.assign<string> ("cc.target.vendor")  = t.vendor;
        rs.assign<string> ("cc.target.system")  = t.system;
        rs.assign<string> ("cc.target.version") = t.version;
        rs.assign<string> ("cc.target.class")   = t.class_;

        rs.assign<target_triplet> ("cc.target") = t;
      }

      // config.cc.pattern
      //
      {
        // This value could be hinted.
        //
        rs.assign<string> ("cc.pattern") =
          cast_empty<string> (h["config.cc.pattern"]);
      }

      // cc.runtime
      // cc.stdlib
      //
      rs.assign ("cc.runtime") = cast<string> (h["cc.runtime"]);
      rs.assign ("cc.stdlib") = cast<string> (h["cc.stdlib"]);

      return true;
    }

    bool
    core_config_init (scope& rs,
                      scope&,
                      const location& loc,
                      unique_ptr<module_base>&,
                      bool first,
                      bool,
                      const variable_map& hints)
    {
      tracer trace ("cc::core_config_init");
      l5 ([&]{trace << "for " << rs;});

      assert (first);

      // Load cc.core.guess.
      //
      if (!cast_false<bool> (rs["cc.core.guess.loaded"]))
        load_module (rs, rs, "cc.core.guess", loc);

      // Configure.
      //

      // Adjust module priority (compiler).
      //
      config::save_module (rs, "cc", 250);

      // Note that we are not having a config report since it will just
      // duplicate what has already been printed by the hinting module.

      // config.cc.{p,c,l}options
      // config.cc.libs
      //
      // @@ Same nonsense as in module.
      //
      //
      rs.assign ("cc.poptions") += cast_null<strings> (
        config::optional (rs, "config.cc.poptions"));

      rs.assign ("cc.coptions") += cast_null<strings> (
        config::optional (rs, "config.cc.coptions"));

      rs.assign ("cc.loptions") += cast_null<strings> (
        config::optional (rs, "config.cc.loptions"));

      rs.assign ("cc.aoptions") += cast_null<strings> (
        config::optional (rs, "config.cc.aoptions"));

      rs.assign ("cc.libs") += cast_null<strings> (
        config::optional (rs, "config.cc.libs"));

      if (lookup l = config::omitted (rs, "config.cc.reprocess").first)
        rs.assign ("cc.reprocess") = *l;

      // Load the bin.config module.
      //
      if (!cast_false<bool> (rs["bin.config.loaded"]))
      {
        // Prepare configuration hints. They are only used on the first load
        // of bin.config so we only populate them on our first load.
        //
        variable_map h (rs.ctx);

        if (first)
        {
          // Note that all these variables have already been registered.
          //
          h.assign ("config.bin.target") =
            cast<target_triplet> (rs["cc.target"]).string ();

          if (auto l = hints["config.bin.pattern"])
            h.assign ("config.bin.pattern") = cast<string> (l);
        }

        load_module (rs, rs, "bin.config", loc, false, h);
      }

      // Verify bin's target matches ours (we do it even if we loaded it
      // ourselves since the target can come from the configuration and not
      // our hint).
      //
      if (first)
      {
        const auto& ct (cast<target_triplet> (rs["cc.target"]));
        const auto& bt (cast<target_triplet> (rs["bin.target"]));

        if (bt != ct)
        {
          const auto& h (cast<string> (rs["cc.hinter"]));

          fail (loc) << h << " and bin module target mismatch" <<
            info << h << " target is " << ct <<
            info << "bin target is " << bt;
        }
      }

      // Load bin.*.config for bin.* modules we may need (see core_init()
      // below).
      //
      const string& tsys (cast<string> (rs["cc.target.system"]));

      if (!cast_false<bool> (rs["bin.ar.config.loaded"]))
        load_module (rs, rs, "bin.ar.config", loc);

      if (tsys == "win32-msvc")
      {
        if (!cast_false<bool> (rs["bin.ld.config.loaded"]))
          load_module (rs, rs, "bin.ld.config", loc);
      }

      if (tsys == "mingw32")
      {
        if (!cast_false<bool> (rs["bin.rc.config.loaded"]))
          load_module (rs, rs, "bin.rc.config", loc);
      }

      return true;
    }

    bool
    core_init (scope& rs,
               scope&,
               const location& loc,
               unique_ptr<module_base>&,
               bool first,
               bool,
               const variable_map& hints)
    {
      tracer trace ("cc::core_init");
      l5 ([&]{trace << "for " << rs;});

      assert (first);

      const string& tsys (cast<string> (rs["cc.target.system"]));

      // Load cc.core.config.
      //
      if (!cast_false<bool> (rs["cc.core.config.loaded"]))
        load_module (rs, rs, "cc.core.config", loc, false, hints);

      // Load the bin module.
      //
      if (!cast_false<bool> (rs["bin.loaded"]))
        load_module (rs, rs, "bin", loc);

      // Load the bin.ar module.
      //
      if (!cast_false<bool> (rs["bin.ar.loaded"]))
        load_module (rs, rs, "bin.ar", loc);

      // For this target we link things directly with link.exe so load the
      // bin.ld module.
      //
      if (tsys == "win32-msvc")
      {
        if (!cast_false<bool> (rs["bin.ld.loaded"]))
          load_module (rs, rs, "bin.ld", loc);
      }

      // If our target is MinGW, then we will need the resource compiler
      // (windres) in order to embed manifests into executables.
      //
      if (tsys == "mingw32")
      {
        if (!cast_false<bool> (rs["bin.rc.loaded"]))
          load_module (rs, rs, "bin.rc", loc);
      }

      return true;
    }

    // The cc module is an "alias" for c and cxx. Its intended use is to make
    // sure that the C/C++ configuration is captured in an amalgamation rather
    // than subprojects.
    //
    static inline bool
    init_alias (tracer& trace,
                scope& rs,
                scope& bs,
                const char* m,
                const char* c,
                const char* c_loaded,
                const char* cxx,
                const char* cxx_loaded,
                const location& loc,
                const variable_map& hints)
    {
      l5 ([&]{trace << "for " << bs;});

      // We only support root loading (which means there can only be one).
      //
      if (&rs != &bs)
        fail (loc) << m << " module must be loaded in project root";

      // We want to order the loading to match what user specified on the
      // command line (config.c or config.cxx). This way the first loaded
      // module (with user-specified config.*) will hint the compiler to the
      // second.
      //
      bool lc (!cast_false<bool> (rs[c_loaded]));
      bool lp (!cast_false<bool> (rs[cxx_loaded]));

      // If none of them are already loaded, load c first only if config.c
      // is specified.
      //
      if (lc && lp && rs["config.c"])
      {
        load_module (rs, rs, c, loc, false, hints);
        load_module (rs, rs, cxx, loc, false, hints);
      }
      else
      {
        if (lp) load_module (rs, rs, cxx, loc, false, hints);
        if (lc) load_module (rs, rs, c, loc, false, hints);
      }

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
      tracer trace ("cc::config_init");
      return init_alias (trace, rs, bs,
                         "cc.config",
                         "c.config",   "c.config.loaded",
                         "cxx.config", "cxx.config.loaded",
                         loc, hints);
    }

    bool
    init (scope& rs,
          scope& bs,
          const location& loc,
          unique_ptr<module_base>&,
          bool,
          bool,
          const variable_map& hints)
    {
      tracer trace ("cc::init");
      return init_alias (trace, rs, bs,
                         "cc",
                         "c",   "c.loaded",
                         "cxx", "cxx.loaded",
                         loc, hints);
    }
  }
}
