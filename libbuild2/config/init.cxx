// file      : libbuild2/config/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <libbuild2/config/init.hxx>

#include <libbuild2/file.hxx>
#include <libbuild2/rule.hxx>
#include <libbuild2/scope.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/filesystem.hxx>  // exists()
#include <libbuild2/diagnostics.hxx>

#include <libbuild2/config/module.hxx>
#include <libbuild2/config/utility.hxx>
#include <libbuild2/config/operation.hxx>

using namespace std;
using namespace butl;

namespace build2
{
  namespace config
  {
    bool
    boot (scope& rs, const location&, unique_ptr<module_base>& mod)
    {
      tracer trace ("config::boot");

      l5 ([&]{trace << "for " << rs;});

      const string& mname (current_mname);
      const string& oname (current_oname);

      // Only create the module if we are configuring or creating. This is a
      // bit tricky since the build2 core may not yet know if this is the
      // case. But we know.
      //
      if ((                   mname == "configure" || mname == "create") ||
          (mname.empty () && (oname == "configure" || oname == "create")))
      {
        unique_ptr<module> m (new module);

        // Adjust priority for the import pseudo-module so that
        // config.import.* values come first in config.build.
        //
        m->save_module ("import", INT32_MIN);

        mod = move (m);
      }

      // Register meta-operations. Note that we don't register create_id
      // since it will be pre-processed into configure.
      //
      rs.insert_meta_operation (configure_id, mo_configure);
      rs.insert_meta_operation (disfigure_id, mo_disfigure);

      return true; // Initialize first (load config.build).
    }

    bool
    init (scope& rs,
          scope&,
          const location& l,
          unique_ptr<module_base>&,
          bool first,
          bool,
          const variable_map& config_hints)
    {
      tracer trace ("config::init");

      if (!first)
      {
        warn (l) << "multiple config module initializations";
        return true;
      }

      const dir_path& out_root (rs.out_path ());
      l5 ([&]{trace << "for " << out_root;});

      assert (config_hints.empty ()); // We don't known any hints.

      auto& vp (rs.ctx.var_pool.rw (rs));

      // Load config.build if one exists (we don't need to worry about
      // disfigure since we will never be init'ed).
      //
      const variable& c_v (vp.insert<uint64_t> ("config.version", false));

      {
        path f (config_file (rs));

        if (exists (f))
        {
          // Check the config version. We assume that old versions cannot
          // understand new configs and new versions are incompatible with old
          // configs.
          //
          // We extract the value manually instead of loading and then
          // checking in order to be able to fixup/migrate the file which we
          // may want to do in the future.
          //
          {
            // Assume missing version is 0.
            //
            auto p (extract_variable (rs.ctx, f, c_v));
            uint64_t v (p.second ? cast<uint64_t> (p.first) : 0);

            if (v != module::version)
              fail (l) << "incompatible config file " << f <<
                info << "config file version   " << v
                         << (p.second ? "" : " (missing)") <<
                info << "config module version " << module::version <<
                info << "consider reconfiguring " << project (rs) << '@'
                         << out_root;
          }

          source (rs, rs, f);
        }
      }

      // Register alias and fallback rule for the configure meta-operation.
      //
      // We need this rule for out-of-any-project dependencies (e.g.,
      // libraries imported from /usr/lib). We are registring it on the
      // global scope similar to builtin rules.
      //
      {
        auto& r (rs.global_scope ().rules);
        r.insert<mtime_target> (
          configure_id, 0, "config.file", file_rule::instance);
      }
      {
        auto& r (rs.rules);

        //@@ outer
        r.insert<alias> (configure_id, 0, "config.alias", alias_rule::instance);

        // This allows a custom configure rule while doing nothing by default.
        //
        r.insert<target> (configure_id, 0, "config", noop_rule::instance);
        r.insert<file> (configure_id, 0, "config.file", noop_rule::instance);
      }

      return true;
    }

    static const module_functions mod_functions[] =
    {
      {"config", &boot,   &init},
      {nullptr,  nullptr, nullptr}
    };

    const module_functions*
    build2_config_load ()
    {
      // Initialize the config entry points in the build system core.
      //
      config_save_variable = &save_variable;
      config_preprocess_create = &preprocess_create;

      return mod_functions;
    }
  }
}
