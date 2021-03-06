// file      : libbuild2/file.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <libbuild2/file.hxx>

#include <iomanip> // left, setw()
#include <sstream>

#include <libbuild2/scope.hxx>
#include <libbuild2/target.hxx>
#include <libbuild2/context.hxx>
#include <libbuild2/filesystem.hxx>
#include <libbuild2/diagnostics.hxx>
#include <libbuild2/prerequisite-key.hxx>

#include <libbuild2/token.hxx>
#include <libbuild2/lexer.hxx>
#include <libbuild2/parser.hxx>

#include <libbuild2/config/utility.hxx> // lookup_config()

using namespace std;
using namespace butl;

namespace build2
{
  // Standard and alternative build file/directory naming schemes.
  //

  // build:

  const dir_path std_build_dir     ("build");
  const dir_path std_root_dir      (dir_path (std_build_dir) /= "root");
  const dir_path std_bootstrap_dir (dir_path (std_build_dir) /= "bootstrap");

  const path std_root_file      (std_build_dir     / "root.build");
  const path std_bootstrap_file (std_build_dir     / "bootstrap.build");
  const path std_src_root_file  (std_bootstrap_dir / "src-root.build");
  const path std_out_root_file  (std_bootstrap_dir / "out-root.build");
  const path std_export_file    (std_build_dir     / "export.build");

  const string std_build_ext        ("build");
  const path   std_buildfile_file   ("buildfile");
  const path   std_buildignore_file (".buildignore");

  // build2:

  const dir_path alt_build_dir     ("build2");
  const dir_path alt_root_dir      (dir_path (alt_build_dir) /= "root");
  const dir_path alt_bootstrap_dir (dir_path (alt_build_dir) /= "bootstrap");

  const path alt_root_file      (alt_build_dir     / "root.build2");
  const path alt_bootstrap_file (alt_build_dir     / "bootstrap.build2");
  const path alt_src_root_file  (alt_bootstrap_dir / "src-root.build2");
  const path alt_out_root_file  (alt_bootstrap_dir / "out-root.build2");
  const path alt_export_file    (alt_build_dir     / "export.build2");

  const string alt_build_ext        ("build2");
  const path   alt_buildfile_file   ("build2file");
  const path   alt_buildignore_file (".build2ignore");

  ostream&
  operator<< (ostream& os, const subprojects& sps)
  {
    for (auto b (sps.begin ()), i (b); os && i != sps.end (); ++i)
    {
      // See find_subprojects() for details.
      //
      const project_name& n (
        path::traits_type::is_separator (i->first.string ().back ())
        ? empty_project_name
        : i->first);

      os << (i != b ? " " : "") << n << '@' << i->second;
    }

    return os;
  }

  // Check if the standard/alternative file/directory exists, returning empty
  // path if it does not.
  //
  template <typename T>
  static T
  exists (const dir_path& d, const T& s, const T& a, optional<bool>& altn)
  {
    T p;
    bool e;

    if (altn)
    {
      p = d / (*altn ? a : s);
      e = exists (p);
    }
    else
    {
      // Check the alternative name first since it is more specific.
      //
      p = d / a;

      if ((e = exists (p)))
        altn = true;
      else
      {
        p = d / s;

        if ((e = exists (p)))
          altn = false;
      }
    }

    return e ? p : T ();
  }

  bool
  is_src_root (const dir_path& d, optional<bool>& altn)
  {
    // We can't have root without bootstrap.build.
    //
    return !exists (d, std_bootstrap_file, alt_bootstrap_file, altn).empty ();
  }

  bool
  is_out_root (const dir_path& d, optional<bool>& altn)
  {
    return !exists (d, std_src_root_file, alt_src_root_file, altn).empty ();
  }

  dir_path
  find_src_root (const dir_path& b, optional<bool>& altn)
  {
    assert (b.absolute ());

    for (dir_path d (b); !d.root () && d != home; d = d.directory ())
    {
      if (is_src_root (d, altn))
        return d;
    }

    return dir_path ();
  }

  pair<dir_path, bool>
  find_out_root (const dir_path& b, optional<bool>& altn)
  {
    assert (b.absolute ());

    for (dir_path d (b); !d.root () && d != home; d = d.directory ())
    {
      bool s;
      if ((s = is_src_root (d, altn)) || is_out_root (d, altn))
        return make_pair (move (d), s);
    }

    return make_pair (dir_path (), false);
  }

  // Remap the src_root variable value if it is inside old_src_root.
  //
  static inline void
  remap_src_root (context& ctx, value& v)
  {
    if (!ctx.old_src_root.empty ())
    {
      dir_path& d (cast<dir_path> (v));

      if (d.sub (ctx.old_src_root))
        d = ctx.new_src_root / d.leaf (ctx.old_src_root);
    }
  }

  static void
  source (parser& p, scope& root, scope& base, lexer& l)
  {
    tracer trace ("source");

    const path_name& fn (l.name ());

    try
    {
      l5 ([&]{trace << "sourcing " << fn;});
      p.parse_buildfile (l, &root, base);
    }
    catch (const io_error& e)
    {
      fail << "unable to read buildfile " << fn << ": " << e;
    }
  }

  static inline void
  source (parser& p,
          scope& root,
          scope& base,
          istream& is,
          const path_name& in)
  {
    lexer l (is, in);
    source (p, root, base, l);
  }

  static void
  source (parser& p, scope& root, scope& base, const path& bf)
  {
    path_name fn (bf);
    try
    {
      ifdstream ifs;
      return source (p, root, base, open_file_or_stdin (fn, ifs), fn);
    }
    catch (const io_error& e)
    {
      fail << "unable to read buildfile " << fn << ": " << e;
    }
  }

  static bool
  source_once (parser& p,
               scope& root,
               scope& base,
               const path& bf,
               scope& once)
  {
    tracer trace ("source_once");

    if (!once.buildfiles.insert (bf).second)
    {
      l5 ([&]{trace << "skipping already sourced " << bf;});
      return false;
    }

    source (p, root, base, bf);
    return true;
  }

  void
  source (scope& root, scope& base, const path& bf)
  {
    parser p (root.ctx);
    source (p, root, base, bf);
  }

  void
  source (scope& root, scope& base, istream& is, const path_name& in)
  {
    parser p (root.ctx);
    source (p, root, base, is, in);
  }

  void
  source (scope& root, scope& base, lexer& l, load_stage s)
  {
    parser p (root.ctx, s);
    source (p, root, base, l);
  }

  bool
  source_once (scope& root, scope& base, const path& bf, scope& once)
  {
    parser p (root.ctx);
    return source_once (p, root, base, bf, once);
  }

  // Source (once) pre-*.build (pre is true) or post-*.build (otherwise) hooks
  // from the specified directory (build/{bootstrap,root}/ of out_root) which
  // must exist.
  //
  static void
  source_hooks (parser& p, scope& root, const dir_path& d, bool pre)
  {
    // While we could have used the wildcard pattern matching functionality,
    // our needs are pretty basic and performance is quite important, so let's
    // handle this ourselves.
    //
    try
    {
      for (const dir_entry& de: dir_iterator (d, false /* ignore_dangling */))
      {
        // If this is a link, then type() will try to stat() it. And if the
        // link is dangling or points to something inaccessible, it will fail.
        // So let's first check that the name matches and only then check the
        // type.
        //
        const path& n (de.path ());

        if (n.string ().compare (0,
                                 pre ? 4 : 5,
                                 pre ? "pre-" : "post-") != 0 ||
            n.extension () != root.root_extra->build_ext)
          continue;

        path f (d / n);

        try
        {
          if (de.type () != entry_type::regular)
            continue;
        }
        catch (const system_error& e)
        {
          fail << "unable to read buildfile " << f << ": " << e;
        }

        source_once (p, root, root, f, root);
      }
    }
    catch (const system_error& e)
    {
      fail << "unable to iterate over " << d << ": " << e;
    }
  }

  scope_map::iterator
  create_root (scope& s, const dir_path& out_root, const dir_path& src_root)
  {
    auto i (s.ctx.scopes.rw (s).insert (out_root, true /* root */));
    scope& rs (i->second);

    context& ctx (rs.ctx);

    // Set out_path. Note that src_path is set in setup_root() below.
    //
    if (rs.out_path_ != &i->first)
    {
      assert (rs.out_path_ == nullptr);
      rs.out_path_ = &i->first;
    }

    // If this is already a root scope, verify that things are consistent.
    //
    {
      value& v (rs.assign (ctx.var_out_root));

      if (!v)
        v = out_root;
      else
      {
        const dir_path& p (cast<dir_path> (v));

        if (p != out_root)
          fail << "new out_root " << out_root << " does not match "
               << "existing " << p;
      }
    }

    if (!src_root.empty ())
    {
      value& v (rs.assign (ctx.var_src_root));

      if (!v)
        v = src_root;
      else
      {
        const dir_path& p (cast<dir_path> (v));

        if (p != src_root)
          fail << "new src_root " << src_root << " does not match "
               << "existing " << p;
      }
    }

    return i;
  }

  void
  setup_root (scope& s, bool forwarded)
  {
    context& ctx (s.ctx);

    // The caller must have made sure src_root is set on this scope.
    //
    value& v (s.assign (ctx.var_src_root));
    assert (v);
    const dir_path& d (cast<dir_path> (v));

    if (s.src_path_ == nullptr)
      s.src_path_ = &d;
    else
      assert (s.src_path_ == &d);

    s.assign (ctx.var_forwarded) = forwarded;
  }

  scope&
  setup_base (scope_map::iterator i,
              const dir_path& out_base,
              const dir_path& src_base)
  {
    scope& s (i->second);
    context& ctx (s.ctx);

    // Set src/out_base variables.
    //
    value& ov (s.assign (ctx.var_out_base));

    if (!ov)
      ov = out_base;
    else
      assert (cast<dir_path> (ov) == out_base);

    value& sv (s.assign (ctx.var_src_base));

    if (!sv)
      sv = src_base;
    else
      assert (cast<dir_path> (sv) == src_base);

    // Set src/out_path. The key (i->first) is out_base.
    //
    if (s.out_path_ == nullptr)
      s.out_path_ = &i->first;
    else
      assert (*s.out_path_ == out_base);

    if (s.src_path_ == nullptr)
      s.src_path_ = &cast<dir_path> (sv);
    else
      assert (*s.src_path_ == src_base);

    return s;
  }

  pair<scope&, scope*>
  switch_scope (scope& root, const dir_path& p, bool proj)
  {
    // First, enter the scope into the map and see if it is in any project. If
    // it is not, then there is nothing else to do.
    //
    auto i (root.ctx.scopes.rw (root).insert (p));
    scope& base (i->second);

    scope* rs (nullptr);

    if (proj && (rs = base.root_scope ()) != nullptr)
    {
      // Path p can be src_base or out_base. Figure out which one it is.
      //
      dir_path out_base (p.sub (rs->out_path ()) ? p : src_out (p, *rs));

      // Create and bootstrap root scope(s) of subproject(s) that this scope
      // may belong to. If any were created, load them. Note that we need to
      // do this before figuring out src_base since we may switch the root
      // project (and src_root with it).
      //
      {
        scope* nrs (&create_bootstrap_inner (*rs, out_base));

        if (rs != nrs)
          rs = nrs;
      }

      // Switch to the new root scope.
      //
      if (rs != &root)
        load_root (*rs); // Load new root(s) recursively.

      // Now we can figure out src_base and finish setting the scope.
      //
      dir_path src_base (src_out (out_base, *rs));
      setup_base (i, move (out_base), move (src_base));
    }

    return pair<scope&, scope*> (base, rs);
  }

  dir_path
  bootstrap_fwd (context& ctx, const dir_path& src_root, optional<bool>& altn)
  {
    path f (exists (src_root, std_out_root_file, alt_out_root_file, altn));

    if (f.empty ())
      return src_root;

    // We cannot just source the buildfile since there is no scope to do
    // this on yet.
    //
    auto p (extract_variable (ctx, f, *ctx.var_out_root));

    if (!p.second)
      fail << "variable out_root expected as first line in " << f;

    auto r (convert<dir_path> (move (p.first)));

    if (r.relative ())
      fail << "relative path in out_root value in " << f;

    return r;
  }

  static void
  setup_root_extra (scope& root, optional<bool>& altn)
  {
    assert (altn && root.root_extra == nullptr);
    bool a (*altn);

    root.root_extra.reset (
      new scope::root_extra_type {
        a,
        a ? alt_build_ext        : std_build_ext,
        a ? alt_build_dir        : std_build_dir,
        a ? alt_buildfile_file   : std_buildfile_file,
        a ? alt_buildignore_file : std_buildignore_file,
        a ? alt_root_dir         : std_root_dir,
        a ? alt_bootstrap_dir    : std_bootstrap_dir,
        a ? alt_bootstrap_file   : std_bootstrap_file,
        a ? alt_root_file        : std_root_file,
        a ? alt_export_file      : std_export_file,
        a ? alt_src_root_file    : std_src_root_file,
        a ? alt_out_root_file    : std_out_root_file,
        {}, /* meta_operations */
        {}, /* operations */
        {}, /* modules */
        {}, /* override_cache */
        {}} /* target_types */);

    // Enter built-in meta-operation and operation names. Loading of
    // modules (via the src bootstrap; see below) can result in
    // additional meta/operations being added.
    //
    root.insert_meta_operation (noop_id,    mo_noop);
    root.insert_meta_operation (perform_id, mo_perform);
    root.insert_meta_operation (info_id,    mo_info);

    root.insert_operation (default_id, op_default);
    root.insert_operation (update_id,  op_update);
    root.insert_operation (clean_id,   op_clean);
  }

  value&
  bootstrap_out (scope& root, optional<bool>& altn)
  {
    context& ctx (root.ctx);
    const dir_path& out_root (root.out_path ());

    path f (exists (out_root, std_src_root_file, alt_src_root_file, altn));

    if (!f.empty ())
    {
      if (root.root_extra == nullptr)
        setup_root_extra (root, altn);

      //@@ TODO: if bootstrap files can source other bootstrap files (for
      //   example, as a way to express dependecies), then we need a way to
      //   prevent multiple sourcing. We handle it here but we still need
      //   something like source_once (once [scope] source) in buildfiles.
      //
      parser p (ctx, load_stage::boot);
      source_once (p, root, root, f, root);
    }

    value& v (root.assign (ctx.var_src_root));

    if (!f.empty ())
    {
      // Verify the value set by src-root.build is sensible.
      //
      // Note: keeping diagnostics consistent with bootstrap_fwd() and
      // find_project_name().
      //
      if (!v)
        fail << "variable src_root expected as first line in " << f;

      if (cast<dir_path> (v).relative ())
        fail << "relative path in src_root value in " << f;
    }

    return v;
  }

  pair<value, bool>
  extract_variable (context& ctx, lexer& l, const variable& var)
  {
    const path_name& fn (l.name ());

    try
    {
      token t (l.next ());
      token_type tt;

      if (t.type != token_type::word || t.value != var.name ||
          ((tt = l.next ().type) != token_type::assign &&
           tt != token_type::prepend &&
           tt != token_type::append))
      {
        return make_pair (value (), false);
      }

      parser p (ctx);
      temp_scope tmp (ctx.global_scope.rw ());
      p.parse_variable (l, tmp, var, tt);

      value* v (tmp.vars.lookup_to_modify (var).first);
      assert (v != nullptr);

      // Steal the value, the scope is going away.
      //
      return make_pair (move (*v), true);
    }
    catch (const io_error& e)
    {
      fail << "unable to read buildfile " << fn << ": " << e << endf;
    }
  }

  pair<value, bool>
  extract_variable (context& ctx,
                    istream& is,
                    const path& bf,
                    const variable& var)
  {
    path_name in (bf);
    lexer l (is, in);
    return extract_variable (ctx, l, var);
  }

  pair<value, bool>
  extract_variable (context& ctx, const path& bf, const variable& var)
  {
    try
    {
      ifdstream ifs (bf);
      return extract_variable (ctx, ifs, bf, var);
    }
    catch (const io_error& e)
    {
      fail << "unable to read buildfile " << bf << ": " << e << endf;
    }
  }



  // Extract the project name from bootstrap.build.
  //
  static project_name
  find_project_name (context& ctx,
                     const dir_path& out_root,
                     const dir_path& fallback_src_root,
                     optional<bool> out_src, // True if out_root is src_root.
                     optional<bool>& altn)
  {
    tracer trace ("find_project_name");

    // First check if the root scope for this project has already been setup
    // in which case we will have src_root and maybe even the name.
    //
    const dir_path* src_root (nullptr);
    const scope& s (ctx.scopes.find (out_root));

    if (s.root_scope () == &s && s.out_path () == out_root)
    {
      if (s.root_extra != nullptr)
      {
        if (!altn)
          altn = s.root_extra->altn;
        else
          assert (*altn == s.root_extra->altn);
      }

      if (lookup l = s.vars[ctx.var_project])
        return cast<project_name> (l);

      src_root = s.src_path_;
    }

    // Load the project name. If this subdirectory is the subproject's
    // src_root, then we can get directly to that. Otherwise, we first have to
    // discover its src_root.
    //
    value src_root_v; // Need it to live until the end.

    if (src_root == nullptr)
    {
      if (out_src ? *out_src : is_src_root (out_root, altn))
        src_root = &out_root;
      else
      {
        path f (exists (out_root, std_src_root_file, alt_src_root_file, altn));

        if (f.empty ())
        {
          // Note: the same diagnostics as in main().
          //
          if (fallback_src_root.empty ())
            fail << "no bootstrapped src_root for " << out_root <<
              info << "consider reconfiguring this out_root";

          src_root = &fallback_src_root;
        }
        else
        {
          auto p (extract_variable (ctx, f, *ctx.var_src_root));

          if (!p.second)
            fail << "variable src_root expected as first line in " << f;

          if (cast<dir_path> (p.first).relative ())
            fail << "relative path in src_root value in " << f;

          src_root_v = move (p.first);
          remap_src_root (ctx, src_root_v); // Remap if inside old_src_root.
          src_root = &cast<dir_path> (src_root_v);

          l5 ([&]{trace << "extracted src_root " << *src_root
                        << " for " << out_root;});
        }
      }
    }

    project_name name;
    {
      path f (exists (*src_root, std_bootstrap_file, alt_bootstrap_file, altn));

      if (f.empty ())
        fail << "no build/bootstrap.build in " << *src_root;

      auto p (extract_variable (ctx, f, *ctx.var_project));

      if (!p.second)
        fail << "variable " << ctx.var_project->name << " expected "
             << "as a first line in " << f;

      name = cast<project_name> (move (p.first));
    }

    l5 ([&]{trace << "extracted project name '" << name << "' for "
                  << *src_root;});
    return name;
  }

  // Scan the specified directory for any subprojects. If a subdirectory
  // is a subproject, then enter it into the map, handling the duplicates.
  //
  static void
  find_subprojects (context& ctx,
                    subprojects& sps,
                    const dir_path& d,
                    const dir_path& root,
                    bool out)
  {
    tracer trace ("find_subprojects");

    try
    {
      for (const dir_entry& de: dir_iterator (d, true /* ignore_dangling */))
      {
        if (de.type () != entry_type::directory)
          continue;

        dir_path sd (d / path_cast<dir_path> (de.path ()));

        bool src (false);
        optional<bool> altn;

        if (!((out && is_out_root (sd, altn)) ||
              (src =  is_src_root (sd, altn))))
        {
          // We used to scan for subproject recursively but this is probably
          // too loose (think of some tests laying around). In the future we
          // should probably allow specifying something like extra/* or
          // extra/** in subprojects.
          //
          //find_subprojects (sps, sd, root, out);
          //
          continue;
        }

        // Calculate relative subdirectory for this subproject.
        //
        dir_path dir (sd.leaf (root));
        l5 ([&]{trace << "subproject " << sd << " as " << dir;});

        // Load its name. Note that here we don't use fallback src_root
        // since this function is used to scan both out_root and src_root.
        //
        project_name name (
          find_project_name (ctx, sd, dir_path (), src, altn));

        // If the name is empty, then is is an unnamed project. While the
        // 'project' variable stays empty, here we come up with a surrogate
        // name for a key. The idea is that such a key should never conflict
        // with a real project name. We ensure this by using the project's
        // sub-directory and appending a trailing directory separator to it.
        //
        if (name.empty ())
          name = project_name (dir.posix_string () + '/',
                               project_name::raw_string);

        // @@ Can't use move() because we may need the values in diagnostics
        // below. Looks like C++17 try_emplace() is what we need.
        //
        auto rp (sps.emplace (name, dir));

        // Handle duplicates.
        //
        if (!rp.second)
        {
          const dir_path& dir1 (rp.first->second);

          if (dir != dir1)
            fail << "inconsistent subproject directories for " << name <<
              info << "first alternative: " << dir1 <<
              info << "second alternative: " << dir;

          l6 ([&]{trace << "skipping duplicate";});
        }
      }
    }
    catch (const system_error& e)
    {
      fail << "unable to iterate over " << d << ": " << e;
    }
  }

  bool
  bootstrap_src (scope& rs, optional<bool>& altn)
  {
    tracer trace ("bootstrap_src");

    context& ctx (rs.ctx);

    bool r (false);

    const dir_path& out_root (rs.out_path ());
    const dir_path& src_root (rs.src_path ());

    {
      path f (exists (src_root, std_bootstrap_file, alt_bootstrap_file, altn));

      if (rs.root_extra == nullptr)
      {
        // If nothing so far has indicated the naming, assume standard.
        //
        if (!altn)
          altn = false;

        setup_root_extra (rs, altn);
      }

      if (!f.empty ())
      {
        // We assume that bootstrap out cannot load this file explicitly. It
        // feels wrong to allow this since that makes the whole bootstrap
        // process hard to reason about. But we may try to bootstrap the same
        // root scope multiple time.
        //
        if (rs.buildfiles.insert (f).second)
        {
          parser p (rs.ctx, load_stage::boot);
          source (p, rs, rs, f);
        }
        else
          l5 ([&]{trace << "skipping already sourced " << f;});

        r = true;
      }
    }

    // See if we are a part of an amalgamation. There are two key players: the
    // outer root scope which may already be present (i.e., we were loaded as
    // part of an amalgamation) and the amalgamation variable that may or may
    // not be set by the user (in bootstrap.build) or by an earlier call to
    // this function for the same scope. When set by the user, the empty
    // special value means that the project shall not be amalgamated (and
    // which we convert to NULL below). When calculated, the NULL value
    // indicates that we are not amalgamated.
    //
    // Note: the amalgamation variable value is always a relative directory.
    //
    {
      auto rp (rs.vars.insert (*ctx.var_amalgamation)); // Set NULL by default.
      value& v (rp.first);

      if (v && v.empty ()) // Convert empty to NULL.
        v = nullptr;

      if (scope* ars = rs.parent_scope ()->root_scope ())
      {
        const dir_path& ad (ars->out_path ());
        dir_path rd (ad.relative (out_root));

        // If we already have the amalgamation variable set, verify
        // that aroot matches its value.
        //
        if (!rp.second)
        {
          if (!v)
          {
            fail << out_root << " cannot be amalgamated" <<
              info << "amalgamated by " << ad;
          }
          else
          {
            const dir_path& vd (cast<dir_path> (v));

            if (vd != rd)
            {
              fail << "inconsistent amalgamation of " << out_root <<
                info << "specified: " << vd <<
                info << "actual: " << rd << " by " << ad;
            }
          }
        }
        else
        {
          // Otherwise, use the outer root as our amalgamation.
          //
          l5 ([&]{trace << out_root << " amalgamated as " << rd;});
          v = move (rd);
        }
      }
      else if (rp.second)
      {
        // If there is no outer root and the amalgamation variable
        // hasn't been set, then we need to check if any of the
        // outer directories is a project's out_root. If so, then
        // that's our amalgamation.
        //
        optional<bool> altn;
        const dir_path& ad (find_out_root (out_root.directory (), altn).first);

        if (!ad.empty ())
        {
          dir_path rd (ad.relative (out_root));
          l5 ([&]{trace << out_root << " amalgamated as " << rd;});
          v = move (rd);
        }
      }
    }

    // See if we have any subprojects. In a sense, this is the other
    // side/direction of the amalgamation logic above. Here, the subprojects
    // variable may or may not be set by the user (in bootstrap.build) or by
    // an earlier call to this function for the same scope. When set by the
    // user, the empty special value means that there are no subproject and
    // none should be searched for (and which we convert to NULL below).
    // Otherwise, it is a list of [project@]directory pairs. The directory
    // must be relative to our out_root. If the project name is not specified,
    // then we have to figure it out. When subprojects are calculated, the
    // NULL value indicates that we found no subprojects.
    //
    {
      auto rp (rs.vars.insert (*ctx.var_subprojects)); // Set NULL by default.
      value& v (rp.first);

      if (rp.second)
      {
        // No subprojects set so we need to figure out if there are any.
        //
        // First we are going to scan our out_root and find all the
        // pre-configured subprojects. Then, if out_root != src_root,
        // we are going to do the same for src_root. Here, however,
        // we need to watch out for duplicates.
        //
        subprojects sps;

        if (exists (out_root))
        {
          l5 ([&]{trace << "looking for subprojects in " << out_root;});
          find_subprojects (rs.ctx, sps, out_root, out_root, true);
        }

        if (out_root != src_root)
        {
          l5 ([&]{trace << "looking for subprojects in " << src_root;});
          find_subprojects (rs.ctx, sps, src_root, src_root, false);
        }

        if (!sps.empty ()) // Keep it NULL if no subprojects.
          v = move (sps);
      }
      else if (v)
      {
        // Convert empty to NULL.
        //
        if (v.empty ())
          v = nullptr;
        else
        {
          // Scan the (untyped) value and convert it to the "canonical" form,
          // that is, a list of name@dir pairs.
          //
          subprojects sps;
          names& ns (cast<names> (v));

          for (auto i (ns.begin ()); i != ns.end (); ++i)
          {
            // Project name.
            //
            project_name n;
            if (i->pair)
            {
              if (i->pair != '@')
                fail << "unexpected pair style in variable subprojects";

              try
              {
                n = convert<project_name> (move (*i));

                if (n.empty ())
                  fail << "empty project name in variable subprojects";
              }
              catch (const invalid_argument&)
              {
                fail << "expected project name instead of '" << *i << "' in "
                     << "variable subprojects";
              }

              ++i; // Got to have the second half of the pair.
            }

            // Directory.
            //
            dir_path d;
            try
            {
              d = convert<dir_path> (move (*i));

              if (d.empty ())
                fail << "empty directory in variable subprojects";
            }
            catch (const invalid_argument&)
            {
              fail << "expected directory instead of '" << *i << "' in "
                   << "variable subprojects";
            }

            // Figure out the project name if the user didn't specify one.
            //
            if (n.empty ())
            {
              optional<bool> altn;

              // Pass fallback src_root since this is a subproject that was
              // specified by the user so it is most likely in our src.
              //
              n = find_project_name (rs.ctx,
                                     out_root / d,
                                     src_root / d,
                                     nullopt /* out_src */,
                                     altn);

              // See find_subprojects() for details on unnamed projects.
              //
              if (n.empty ())
                n = project_name (d.posix_string () + '/',
                                  project_name::raw_string);
            }

            sps.emplace (move (n), move (d));
          }

          // Change the value to the typed map.
          //
          v = move (sps);
        }
      }
    }

    return r;
  }

  void
  bootstrap_pre (scope& root, optional<bool>& altn)
  {
    const dir_path& out_root (root.out_path ());

    // This test is a bit loose in a sense that there can be a stray
    // build/bootstrap/ directory that will make us mis-treat a project as
    // following the standard naming scheme (the other way, while also
    // possible, is a lot less likely). If this does becomes a problem, we can
    // always tighten the test by also looking for a hook file with the
    // correct extension.
    //
    dir_path d (exists (out_root, std_bootstrap_dir, alt_bootstrap_dir, altn));

    if (!d.empty ())
    {
      if (root.root_extra == nullptr)
        setup_root_extra (root, altn);

      parser p (root.ctx, load_stage::boot);
      source_hooks (p, root, d, true /* pre */);
    }
  }

  void
  bootstrap_post (scope& root)
  {
    const dir_path& out_root (root.out_path ());

    dir_path d (out_root / root.root_extra->bootstrap_dir);

    if (exists (d))
    {
      parser p (root.ctx, load_stage::boot);
      source_hooks (p, root, d, false /* pre */);
    }
  }

  bool
  bootstrapped (scope& rs)
  {
    // Use the subprojects variable set by bootstrap_src() as an indicator.
    // It should either be NULL or typed (so we assume that the user will
    // never set it to NULL).
    //
    auto l (rs.vars[rs.ctx.var_subprojects]);
    return l.defined () && (l->null || l->type != nullptr);
  }

  // Return true if the inner/outer project (identified by out/src_root) of
  // the 'origin' project (identified by orig) should be forwarded.
  //
  static inline bool
  forwarded (const scope& orig,
             const dir_path& out_root,
             const dir_path& src_root,
             optional<bool>& altn)
  {
    context& ctx (orig.ctx);

    // The conditions are:
    //
    // 1. Origin is itself forwarded.
    //
    // 2. Inner/outer src_root != out_root.
    //
    // 3. Inner/outer out-root.build exists in src_root and refers out_root.
    //
    return (out_root != src_root                            &&
            cast_false<bool> (orig.vars[ctx.var_forwarded]) &&
            bootstrap_fwd (ctx, src_root, altn) == out_root);
  }

  void
  create_bootstrap_outer (scope& root)
  {
    context& ctx (root.ctx);

    auto l (root.vars[ctx.var_amalgamation]);

    if (!l)
      return;

    const dir_path& d (cast<dir_path> (l));
    dir_path out_root (root.out_path () / d);
    out_root.normalize (); // No need to actualize (d is a bunch of ..)

    // src_root is a bit more complicated. Here we have three cases:
    //
    // 1. Amalgamation's src_root is "parallel" to the sub-project's.
    // 2. Amalgamation's src_root is the same as its out_root.
    // 3. Some other pre-configured (via src-root.build) src_root.
    //
    // So we need to try all these cases in some sensible order. #3 should
    // probably be tried first since that src_root was explicitly configured
    // by the user. After that, #2 followed by #1 seems reasonable.
    //
    scope& rs (create_root (root, out_root, dir_path ())->second);

    bool bstrapped (bootstrapped (rs));

    optional<bool> altn;
    if (!bstrapped)
    {
      value& v (bootstrap_out (rs, altn)); // #3 happens here (or it can be #1)

      if (!v)
      {
        if (is_src_root (out_root, altn)) // #2
          v = out_root;
        else // #1
        {
          dir_path src_root (root.src_path () / d);
          src_root.normalize (); // No need to actualize (as above).
          v = move (src_root);
        }
      }
      else
        remap_src_root (ctx, v); // Remap if inside old_src_root.

      setup_root (rs, forwarded (root, out_root, v.as<dir_path> (), altn));
      bootstrap_pre (rs, altn);
      bootstrap_src (rs, altn);
      // bootstrap_post() delayed until after create_bootstrap_outer().
    }
    else
    {
      altn = rs.root_extra->altn;

      if (forwarded (root, rs.out_path (), rs.src_path (), altn))
        rs.assign (ctx.var_forwarded) = true; // Only upgrade (see main()).
    }

    create_bootstrap_outer (rs);

    if (!bstrapped)
      bootstrap_post (rs);

    // Check if we are strongly amalgamated by this outer root scope.
    //
    if (root.src_path ().sub (rs.src_path ()))
      root.strong_ = rs.strong_scope (); // Itself or some outer scope.
  }

  scope&
  create_bootstrap_inner (scope& root, const dir_path& out_base)
  {
    context& ctx (root.ctx);

    scope* r (&root);

    if (auto l = root.vars[ctx.var_subprojects])
    {
      for (const auto& p: cast<subprojects> (l))
      {
        dir_path out_root (root.out_path () / p.second);

        if (!out_base.empty () && !out_base.sub (out_root))
          continue;

        // The same logic to src_root as in create_bootstrap_outer().
        //
        scope& rs (create_root (root, out_root, dir_path ())->second);

        optional<bool> altn;
        if (!bootstrapped (rs))
        {
          value& v (bootstrap_out (rs, altn));

          if (!v)
          {
            v = is_src_root (out_root, altn)
              ? out_root
              : (root.src_path () / p.second);
          }
          else
            remap_src_root (ctx, v); // Remap if inside old_src_root.

          setup_root (rs, forwarded (root, out_root, v.as<dir_path> (), altn));
          bootstrap_pre (rs, altn);
          bootstrap_src (rs, altn);
          bootstrap_post (rs);
        }
        else
        {
          altn = rs.root_extra->altn;
          if (forwarded (root, rs.out_path (), rs.src_path (), altn))
            rs.assign (ctx.var_forwarded) = true; // Only upgrade (see main()).
        }

        // Check if we strongly amalgamated this inner root scope.
        //
        if (rs.src_path ().sub (root.src_path ()))
          rs.strong_ = root.strong_scope (); // Itself or some outer scope.

        // See if there are more inner roots.
        //
        r = &create_bootstrap_inner (rs, out_base);

        if (!out_base.empty ())
          break; // We have found our subproject.
      }
    }

    return *r;
  }

  void
  load_root (scope& root)
  {
    tracer trace ("load_root");

    const dir_path& out_root (root.out_path ());
    const dir_path& src_root (root.src_path ());

    // As an optimization, check if we have already loaded root.build. If
    // that's the case, then we have already been called for this project.
    //
    path f (src_root / root.root_extra->root_file);

    if (root.buildfiles.find (f) != root.buildfiles.end ())
      return;

    // First load outer roots, if any.
    //
    if (scope* rs = root.parent_scope ()->root_scope ())
      load_root (*rs);

    // Finish off initializing bootstrapped modules.
    //
    for (auto& p: root.root_extra->modules)
    {
      module_state& s (p.second);

      if (s.boot && s.first)
        init_module (root, root, p.first, s.loc);
    }

    for (auto& p: root.root_extra->modules)
    {
      module_state& s (p.second);

      if (s.boot && !s.first)
        init_module (root, root, p.first, s.loc);
    }

    // Load hooks and root.build.
    //
    // We can load the pre hooks before finishing off loading the bootstrapped
    // modules (which, in case of config would load config.build) or after and
    // one can come up with a plausible use-case for either approach. Note,
    // however, that one can probably achieve adequate pre-modules behavior
    // with a post-bootstrap hook.
    //
    dir_path hd (out_root / root.root_extra->root_dir);

    bool he (exists (hd));
    bool fe (exists (f));


    // Reuse the parser to accumulate the configuration variable information.
    //
    parser p (root.ctx, load_stage::root);

    if (he) {source_hooks (p, root, hd, true  /* pre */); p.reset ();}
    if (fe) {source_once (p, root, root, f, root);}
    if (he) {p.reset (); source_hooks (p, root, hd, false /* pre */);}

    // Print the project configuration report, similar to how we do it in
    // build system modules.
    //
    if (!p.config_report.empty () && verb >= (p.config_report_new ? 2 : 3))
    {
      const project_name& proj (named_project (root)); // Can be empty.

      // @@ TODO/MAYBE:
      //
      // - Should we be printing NULL values? Maybe make this configurable?
      // - Quoted printing format (single/double)?
      //

      // Use the special `config` module name (which doesn't have its own
      // report) for project configuration.
      //
      diag_record dr (text);
      dr << "config " << proj << '@' << root;

      // Printing the whole variable name would add too much noise with all
      // the repetitive config.<project>. So we are only going to print the
      // part after <project> (see parser::parse_config() for details).
      //
      // But if there is no named project, then we print everything after
      // config. This feels right since there could be zero identifiable
      // information about the project in the header line. For example:
      //
      // config @/tmp/tests
      //   libhello.tests.remote true
      //
      string stem (!proj.empty () ? '.' + proj.variable () + '.' : string ());

      names storage;
      for (const pair<lookup, string>& lf: p.config_report)
      {
        lookup l (lf.first);
        const string& f (lf.second);

        // If the report variable has been overriden, now is the time to
        // lookup its value.
        //
        string n;
        if (l.value == nullptr)
        {
          n = l.var->name; // Use the name as is.
          l = root[*l.var];
        }
        else
        {
          size_t p (!stem.empty ()
                    ? l.var->name.find (stem) + stem.size ()
                    : 7); // "config."
          n = string (l.var->name, p);
        }

        dr << "\n  ";

        if (const value& v = *l)
        {
          storage.clear ();
          auto ns (reverse (v, storage));

          if (f == "multiline")
          {
            dr << n;
            for (auto& n: ns)
              dr << "\n    " << n;
          }
          else
            dr << left << setw (10) << n << ' ' << ns;
        }
        else
          dr << left << setw (10) << n << " [null]";
      }
    }
  }

  scope&
  load_project (scope& s,
                const dir_path& out_root,
                const dir_path& src_root,
                bool forwarded,
                bool load)
  {
    assert (!forwarded || out_root != src_root);

    context& ctx (s.ctx);

    auto i (create_root (s, out_root, src_root));
    scope& rs (i->second);

    if (!bootstrapped (rs))
    {
      optional<bool> altn;
      bootstrap_out (rs, altn);
      setup_root (rs, forwarded);
      bootstrap_pre (rs, altn);
      bootstrap_src (rs, altn);
      bootstrap_post (rs);
    }
    else
    {
      if (forwarded)
        rs.assign (ctx.var_forwarded) = true; // Only upgrade (see main()).
    }

    if (load)
    {
      load_root (rs);
      setup_base (i, out_root, src_root); // Setup as base.
    }

    return rs;
  }

  // Find or insert a target based on the file path.
  //
  static const target*
  find_target (tracer& trace, context& ctx,
               const target_type& tt, const path& p)
  {
    const target* t (
      ctx.targets.find (tt,
                        p.directory (),
                        dir_path (),
                        p.leaf ().base ().string (),
                        p.extension (),
                        trace));

    if (t != nullptr)
    {
      if (const file* f = t->is_a<file> ())
        assert (f->path () == p);
    }

    return t;
  }

  static pair<target&, ulock>
  insert_target (tracer& trace, context& ctx,
                 const target_type& tt, path p)
  {
    auto r (
      ctx.targets.insert_locked (tt,
                                 p.directory (),
                                 dir_path (),    // No out (not in project).
                                 p.leaf ().base ().string (),
                                 p.extension (), // Always specified.
                                 true /* implied */,
                                 trace));

    if (const file* f = r.first.is_a<file> ())
      f->path (move (p));

    return r;
  }

  // Extract metadata for an executable target by executing it with the
  // --build2-metadata option. In case of an error, issue diagnostics and fail
  // if opt is false and return nullopt if it's true.
  //
  // Note that loading of the metadata is split into two steps, extraction and
  // parsing, because extraction also serves as validation that the executable
  // is runnable, what we expected, etc. In other words, we sometimes do the
  // extraction without parsing. In this light it would have been more
  // efficient for extract to return the running process with a pipe rather
  // than the extracted data. But this would complicate the code quite a bit
  // plus we don't expect the data to be large, typically.
  //
  // Also note that we do not check the export.metadata here leaving it to
  // the caller to do for both this case and export stub.
  //
  static optional<string>
  extract_metadata (const process_path& pp,
                    const string& key,
                    bool opt,
                    const location& loc)
  {
    // Note: to ease handling (think patching third-party code) we will always
    // specify the --build2-metadata option in this single-argument form.
    //
    const char* args[] {pp.recall_string (), "--build2-metadata=1", nullptr};

    // @@ TODO This needs some more thinking/clarification. Specifically, what
    //    does it mean "x not found/not ours"? Is it just not found in PATH?
    //    That plus was not able to execute (e.g., some shared libraries
    //    missing)? That plus abnormal termination? That plus x that we found
    //    is something else?
    //
    //    Specifically, at least on Linux, when a shared library is not found,
    //    it appears exec() issues the diagnostics and calls exit(127) (that
    //    is, exec() does not return). So this is a normal termination with a
    //    peculiar exit code.
    //
    //    Overall, it feels like we should only silently ignore the "not
    //    found" and "not ours" cases since for all others the result is
    //    ambigous: it could be "ours" but just broken and the user expects
    //    us to use it but we silently ignored it. But then the same can be
    //    said about the "not ours" case: the user expected us to find "ours"
    //    but we didn't and silently ignored it.
    //
    try
    {
      // Note: not using run_*() functions since need to be able to suppress
      // all errors, including inability to exec.
      //
      if (verb >= 3)
        print_process (args);

      process pr (pp,
                  args,
                  -2           /* stdin  to /dev/null                 */,
                  -1           /* stdout to pipe                      */,
                  opt ? -2 : 2 /* stderr to /dev/null or pass-through */);

      try
      {
        ifdstream is (move (pr.in_ofd), fdstream_mode::skip);
        string r;
        getline (is, r, '\0'); // Will fail if there is no data.
        is.close (); // Detect errors.

        if (pr.wait ())
        {
          // Check the signature line. It should be in the following form:
          //
          // # build2 buildfile <key>
          //
          // This makes sure we don't treat bogus output as metadata and also
          // will allow us to support other formats (say, JSON) in the future.
          // Note that we won't be able to add more options since trying them
          // will be expensive.
          //
          if (r.compare (0,
                         20 + key.size (),
                         "# build2 buildfile " + key + '\n') == 0)
            return r;

          if (!opt)
            error (loc) << "invalid metadata signature in " << args[0]
                        << " output";

          goto fail;
        }

        // Process error, fall through.
      }
      catch (const io_error&)
      {
        // IO error (or process error), fall through.
      }

      // Deal with process or IO error.
      //
      if (pr.wait ())
      {
        if (!opt)
          error (loc) << "unable to read metadata from " << args[0];
      }
      else
      {
        // The child process presumably issued diagnostics but if it didn't,
        // the result will be very confusing. So let's issue something
        // generic for good measure.
        //
        if (!opt)
          error (loc) << "unable to extract metadata from " << args[0];
      }

      goto fail;
    }
    catch (const process_error& e)
    {
      if (!opt)
        error (loc) << "unable to execute " << args[0] << ": " << e;

      if (e.child)
        exit (1);

      goto fail;
    }

    fail:

    if (opt)
      return nullopt;
    else
      throw failed ();
  }

  static void
  parse_metadata (target& t, const string& md, const location& loc)
  {
    istringstream is (md);
    path_name in ("<metadata>");

    auto df = make_diag_frame (
      [&t, &loc] (const diag_record& dr)
      {
        dr << info (loc) << "while loading metadata for " << t;
      });

    parser p (t.ctx);
    p.parse_buildfile (is, in,
                       nullptr /* root */,
                       t.base_scope ().rw (), // Load phase.
                       &t);
  }

  // Return the processed target name as well as the project directory, if
  // any.
  //
  // Absent project directory means nothing importable for this target was
  // found (and the returned target name is the same as the original). Empty
  // project directory means the target was found in an ad hoc manner, outside
  // of any project (in which case it may still be qualified; see
  // config.import.<proj>.<name>[.<type>]).
  //
  // Return empty name if an ad hoc import resulted in a NULL target (only
  // allowed if optional is true).
  //
  pair<name, optional<dir_path>>
  import_search (bool& new_value,
                 scope& ibase,
                 name tgt,
                 bool opt,
                 const optional<string>& meta,
                 bool subp,
                 const location& loc,
                 const char* what)
  {
    tracer trace ("import_search");

    // If there is no project specified for this target, then our run will be
    // short and sweet: we simply return it as empty-project-qualified and
    // let someone else (e.g., a rule) take a stab at it.
    //
    if (tgt.unqualified ())
    {
      tgt.proj = project_name ();
      return make_pair (move (tgt), optional<dir_path> ());
    }

    context& ctx (ibase.ctx);

    // Otherwise, get the project name and convert the target to unqualified.
    //
    project_name proj (move (*tgt.proj));
    tgt.proj = nullopt;

    scope& iroot (*ibase.root_scope ());

    // Figure out the imported project's out_root.
    //
    optional<dir_path> out_root;

    // First try the config.import.* mechanism. The idea is that if the user
    // explicitly told us the project's location, then we should prefer that
    // over anything that we may discover. In particular, we will prefer it
    // over any bundled subprojects.
    //
    auto& vp (iroot.var_pool ());

    using config::lookup_config;

    for (;;) // Break-out loop.
    {
      string projv (proj.variable ());
      string n ("config.import." + projv);

      // Skip import phase 1.
      //
      auto skip = [&tgt, &proj, &trace] ()
      {
        tgt.proj = move (proj);
        l5 ([&]{trace << "skipping " << tgt;});
        return make_pair (move (tgt), optional<dir_path> ());
      };

      // Add hoc import.
      //
      // config.import.<proj>.<name>.<type>
      // config.import.<proj>.<name>
      //
      // For example: config.import.build2.b.exe=/opt/build2/bin/b
      //
      // If <type> is exe and <proj> and <name> are the same, then we also
      // recognize the special config.<proj> (tool importation; we could
      // also handle the case where <proj> is not the same as <name> via
      // the config.<proj>.<name> variable). For backwards-compatibility
      // reasons, it takes precedence over config.import.
      //
      // Note: see import phase 2 diagnostics if changing anything here.
      //
      // @@ How will this work for snake-case targets, say libs{build2-foo}?
      //    As well as for dot-separated target types, say, cli.cxx{}?
      //
      // @@ This duality has a nasty side-effect: if we have config.<proj>
      //    configured, then specifying config.<proj>.import has no effect
      //    (see also a note below on priority just among these options).
      //
      //    Some ideas on how to resolve this include: using lookup depth,
      //    using override info, and using the "new value" status. All of
      //    these undoubtfully will complicate this logic (i.e., we will have
      //    to lookup all of them and then decide which one "wins").
      //
      if (!tgt.value.empty ())
      {
        // Return NULL if not found and empty path if NULL. For executable
        // targets (exe is true), also treat the special `false` value as
        // NULL.
        //
        auto lookup = [&new_value, &iroot, opt, &loc, what] (
          const variable& var, bool exe) -> const path*
        {
          auto l (lookup_config (new_value, iroot, var));

          if (l.defined ())
          {
            const path* p (cast_null<path> (l));

            if (p != nullptr)
            {
              if (p->empty ())
                fail (loc) << "empty path in " << var;

              if (!exe || p->to_directory () || p->string () != "false")
                return p;
            }

            if (!opt)
              fail (loc) << (p == nullptr ? "null" : "false") << " in "
                         << var << " for non-optional " << what;

            return &empty_path;
          }

          return nullptr;
        };

        // First try config.<proj>, then import.<name>.<type>, and finally
        // just import.<name>.
        //
        // @@ What should we do if several of them are specified? For example,
        //    one is inherited from amalgamation while the other is specified
        //    on the project's root? We could pick the one with the least
        //    lookup depth. On the other hand, we expect people to stick with
        //    the config.<proj> notation for tools (since it's a lot easier to
        //    type) so let's not complicate things for the time being.
        //
        //    Another alternative would be to see which one is new.
        //
        const path* p (nullptr);

        if (tgt.typed ())
        {
          bool e (tgt.type == "exe");

          // The config.import.* vars are pattern-typed in context ctor as an
          // overridable variable of type path. The config.<proj> we have to
          // type manually.
          //
          if (e && (projv == tgt.value || proj == tgt.value))
            p = lookup (vp.insert<path> ("config." + projv), e);

          if (p == nullptr)
            p = lookup (vp.insert (n + '.' + tgt.value + '.' + tgt.type), e);
        }

        if (p == nullptr)
          p = lookup (vp.insert (n + '.' + tgt.value), false);

        if (p != nullptr)
        {
          if (p->empty ())
            tgt = name (); // NULL
          else
          {
            tgt.dir = p->directory ();
            tgt.value = p->leaf ().string ();

            // If the path is relative, then keep it project-qualified
            // assuming import phase 2 knows what to do with it. Think:
            //
            // config.import.build2.b=b-boot
            //
            // @@ Maybe we should still complete it if it's not simple? After
            //    all, this is a path, do we want interpretations other than
            //    relative to CWD? Maybe we do, who knows. Doesn't seem to
            //    harm anything at the moment.
            //
            // Why not call import phase 2 directly here? Well, one good
            // reason would be to allow for rule-specific import resolution.
            //
            if (p->relative ())
              tgt.proj = move (proj);
            else
            {
              // Enter the target and assign its path (this will most commonly
              // be some out of project file).
              //
              // @@ Should we check that the file actually exists (and cache
              //    the extracted timestamp)? Or just let things take their
              //    natural course?
              //
              name n (tgt);
              auto r (ibase.find_target_type (n, loc));

              if (r.first == nullptr)
                fail (loc) << "unknown target type " << n.type << " in " << n;

              // Note: not using the extension extracted by find_target_type()
              // to be consistent with import phase 2.
              //
              target& t (insert_target (trace, ctx, *r.first, *p).first);

              // Load the metadata, similar to import phase 2.
              //
              if (meta)
              {
                if (exe* e = t.is_a<exe> ())
                {
                  if (!e->vars[ctx.var_export_metadata].defined ())
                  {
                    parse_metadata (*e,
                                    *extract_metadata (e->process_path (),
                                                       *meta,
                                                       false /* optional */,
                                                       loc),
                                    loc);
                  }
                }
              }
            }
          }

          return make_pair (move (tgt), optional<dir_path> (dir_path ()));
        }
      }

      // Normal import.
      //
      // config.import.<proj>
      //
      // Note: see import phase 2 diagnostics if changing anything here.
      //
      {
        // Note: pattern-typed in context ctor as an overridable variable of
        // type abs_dir_path (path auto-completion).
        //
        auto l (lookup_config (new_value, iroot, vp.insert (n)));

        if (l.defined ())
        {
          const dir_path* d (cast_null<dir_path> (l));

          // Empty/NULL config.import.* value means don't look in subprojects
          // or amalgamations and go straight to the rule-specific import
          // (e.g., to use system-installed).
          //
          if (d == nullptr || d->empty ())
            return skip ();

          out_root = *d; // Normalized and actualized.
          break;
        }
      }

      // import.build2
      //
      // Note that the installed case is taken care of by special code in the
      // cc module's search_library().
      //
      if (proj == "build2")
      {
        // Note that this variable can be set to NULL to disable relying on
        // the built-in path. We use this in our tests to make sure we are
        // importing and testing the build system being built and not the one
        // doing the building.
        //
        if (auto l = iroot[ctx.var_import_build2])
        {
          out_root = cast<dir_path> (l);

          if (out_root->empty ())
            return skip ();

          break;
        }
      }

      // Otherwise search subprojects, starting with our root and then trying
      // outer roots for as long as we are inside an amalgamation.
      //
      if (subp)
      {
        for (scope* r (&iroot);; r = r->parent_scope ()->root_scope ())
        {
          l5 ([&]{trace << "looking in " << *r;});

          // First check the amalgamation itself.
          //
          if (r != &iroot &&
              cast<project_name> (r->vars[ctx.var_project]) == proj)
          {
            out_root = r->out_path ();
            break;
          }

          if (auto l = r->vars[ctx.var_subprojects])
          {
            const auto& m (cast<subprojects> (l));
            auto i (m.find (proj));

            if (i != m.end ())
            {
              const dir_path& d ((*i).second);
              out_root = r->out_path () / d;
              break;
            }
          }

          if (!r->vars[ctx.var_amalgamation])
            break;
        }
      }

      break;
    }

    // Add the qualification back to the target (import_load() will remove it
    // again).
    //
    tgt.proj = move (proj);

    return make_pair (move (tgt), move (out_root));
  }

  pair<names, const scope&>
  import_load (context& ctx,
               pair<name, optional<dir_path>> x,
               bool meta,
               const location& loc)
  {
    tracer trace ("import_load");

    assert (x.second);

    name tgt (move (x.first));
    dir_path out_root (move (*x.second));

    assert (tgt.proj);
    project_name proj (move (*tgt.proj));
    tgt.proj = nullopt;

    // Bootstrap the imported root scope. This is pretty similar to what we do
    // in main() except that here we don't try to guess src_root.
    //
    // The user can also specify the out_root of the amalgamation that contains
    // our project. For now we only consider top-level sub-projects.
    //
    scope* root;
    dir_path src_root;

    // See if this is a forwarded configuration. For top-level project we want
    // to use the same logic as in main() while for inner subprojects -- as in
    // create_bootstrap_inner().
    //
    bool fwd (false);
    optional<bool> altn;
    if (is_src_root (out_root, altn))
    {
      src_root = move (out_root);
      out_root = bootstrap_fwd (ctx, src_root, altn);
      fwd = (src_root != out_root);
    }

    scope& gs (ctx.global_scope.rw ());

    for (const scope* proot (nullptr); ; proot = root)
    {
      bool top (proot == nullptr);

      root = &create_root (gs, out_root, src_root)->second;

      bool bstrapped (bootstrapped (*root));

      if (!bstrapped)
      {
        value& v (bootstrap_out (*root, altn));

        // Check that the bootstrap process set src_root.
        //
        if (v)
        {
          // Note that unlike main() here we fail hard. The idea is that if
          // the project we are importing is misconfigured, then it should be
          // fixed first.
          //
          const dir_path& p (cast<dir_path> (v));

          if (!src_root.empty () && p != src_root)
            fail (loc) << "configured src_root " << p << " does not match "
                       << "discovered " << src_root;
        }
        else
          fail (loc) << "unable to determine src_root for imported " << proj <<
            info << "consider configuring " << out_root;

        setup_root (*root,
                    (top
                     ? fwd
                     : forwarded (*proot, out_root, v.as<dir_path> (), altn)));

        bootstrap_pre (*root, altn);
        bootstrap_src (*root, altn);
        if (!top)
          bootstrap_post (*root);
      }
      else
      {
        altn = root->root_extra->altn;

        if (src_root.empty ())
          src_root = root->src_path ();

        if (top ? fwd : forwarded (*proot, out_root, src_root, altn))
          root->assign (ctx.var_forwarded) = true; // Only upgrade (see main()).
      }

      if (top)
      {
        create_bootstrap_outer (*root);

        if (!bstrapped)
          bootstrap_post (*root);
      }

      // Now we know this project's name as well as all its subprojects.
      //
      if (cast<project_name> (root->vars[ctx.var_project]) == proj)
        break;

      if (auto l = root->vars[ctx.var_subprojects])
      {
        const auto& m (cast<subprojects> (l));
        auto i (m.find (proj));

        if (i != m.end ())
        {
          const dir_path& d ((*i).second);
          altn = nullopt;
          out_root = root->out_path () / d;
          src_root = is_src_root (out_root, altn) ? out_root : dir_path ();
          continue;
        }
      }

      fail (loc) << out_root << " is not out_root for " << proj;
    }

    // Load the imported root scope.
    //
    load_root (*root);

    // Use a temporary scope so that the export stub doesn't mess anything up.
    //
    temp_scope ts (gs);

    // "Pass" the imported project's roots to the stub.
    //
    ts.assign (ctx.var_out_root) = move (out_root);
    ts.assign (ctx.var_src_root) = move (src_root);

    // Pass the target being imported in import.target.
    //
    {
      value& v (ts.assign (ctx.var_import_target));

      if (!tgt.empty ()) // Otherwise leave NULL.
        v = tgt; // Can't move (need for diagnostics below).
    }

    // Pass the metadata compatibility version in import.metadata.
    //
    // This serves both as an indication that the metadata is required (can be
    // useful, for example, in cases where it is expensive to calculate) as
    // well as the maximum version we recognize. The exporter may return it in
    // any version up to and including this maximum. And it may return it even
    // if not requested (but only in version 1). The exporter should also set
    // the returned version as the target-specific export.metadata variable.
    //
    if (meta)
      ts.assign (ctx.var_import_metadata) = uint64_t (1);

    // Load the export stub. Note that it is loaded in the context
    // of the importing project, not the imported one. The export
    // stub will normally switch to the imported root scope at some
    // point.
    //
    path es (root->src_path () / root->root_extra->export_file);

    try
    {
      ifdstream ifs (es);

      l5 ([&]{trace << "importing " << es;});

      // @@ Should we verify these are all unqualified names? Or maybe
      // there is a use-case for the export stub to return a qualified
      // name?
      //
      parser p (ctx);
      names v (p.parse_export_stub (ifs, path_name (es), gs, ts));

      // If there were no export directive executed in an export stub, assume
      // the target is not exported.
      //
      if (v.empty () && !tgt.empty ())
        fail (loc) << "target " << tgt << " is not exported by project "
                   << proj;

      return pair<names, const scope&> (move (v), *root);
    }
    catch (const io_error& e)
    {
      fail (loc) << "unable to read buildfile " << es << ": " << e << endf;
    }
  }

  pair<names, import_kind>
  import (scope& base,
          name tgt,
          bool ph2,
          bool opt,
          bool metadata,
          const location& loc)
  {
    tracer trace ("import");

    l5 ([&]{trace << tgt << " from " << base;});

    assert ((!opt || ph2) && (!metadata || ph2));

    context& ctx (base.ctx);
    assert (ctx.phase == run_phase::load);

    // If metadata is requested, delegate to import_direct() which will lookup
    // the target and verify the metadata was loaded.
    //
    if (metadata)
    {
      pair<const target*, import_kind> r (
        import_direct (base, move (tgt), ph2, opt, metadata, loc));

      return make_pair (r.first != nullptr ? r.first->as_name () : names {},
                        r.second);
    }

    // Save the original target name as metadata key.
    //
    auto meta (metadata ? optional<string> (tgt.value) : nullopt);

    pair<name, optional<dir_path>> r (
      import_search (base, move (tgt), opt, meta, true /* subpproj */, loc));

    // If there is no project, we are either done or go straight to phase 2.
    //
    if (!r.second || r.second->empty ())
    {
      names ns;

      if (r.first.empty ())
      {
        assert (opt); // NULL
      }
      else
      {
        ns.push_back (move (r.first));

        // If the target is still qualified, it is either phase 2 now or we
        // return it as is to let someone else (e.g., a rule, import phase 2)
        // take a stab at it later.
        //
        if (ns.back ().qualified ())
        {
          if (ph2)
          {
            // This is tricky: we only want the optional semantics for the
            // fallback case.
            //
            if (const target* t = import (ctx,
                                          base.find_prerequisite_key (ns, loc),
                                          opt && !r.second  /* optional */,
                                          meta,
                                          false             /* existing */,
                                          loc))
              ns = t->as_name ();
            else
              ns.clear (); // NULL
          }
          else
            l5 ([&]{trace << "postponing " << r.first;});
        }
      }

      return make_pair (
        move (ns),
        r.second.has_value () ? import_kind::adhoc : import_kind::fallback);
    }

    return make_pair (
      import_load (base.ctx, move (r), metadata, loc).first,
      import_kind::normal);
  }

  const target*
  import (context& ctx,
          const prerequisite_key& pk,
          bool opt,
          const optional<string>& meta,
          bool exist,
          const location& loc)
  {
    tracer trace ("import");

    assert (!meta || !exist);

    assert (pk.proj);
    const project_name& proj (*pk.proj);

    // Target type-specific search.
    //
    const target_key& tk (pk.tk);
    const target_type& tt (*tk.type);

    // Try to find the executable in PATH (or CWD if relative).
    //
    for (; tt.is_a<exe> (); ) // Breakout loop.
    {
      path n (*tk.dir);
      n /= *tk.name;
      if (tk.ext)
      {
        n += '.';
        n += *tk.ext;
      }

      // Only search in PATH (or CWD if not simple).
      //
      process_path pp (
        process::try_path_search (n,
                                  false       /* init */,
                                  dir_path () /* fallback */,
                                  true        /* path_only */));
      if (pp.empty ())
        break;

      const path& p (pp.effect);
      assert (!p.empty ()); // We searched for a relative path.

      if (exist) // Note: then meta is false.
      {
        if (const target* t = find_target (trace, ctx, tt, p))
          return t;

        break;
      }

      // Try hard to avoid re-extracting the metadata (think of a tool that is
      // used by multiple projects in an amalgamation).
      //
      optional<string> md;
      optional<const target*> t;
      if (meta)
      {
        t = find_target (trace, ctx, tt, p);

        if (*t != nullptr && (*t)->vars[ctx.var_export_metadata].defined ())
          return *t; // We've got all we need.

        if (!(md = extract_metadata (pp, *meta, opt, loc)))
          break;
      }

      if (!t || *t == nullptr)
      {
        pair<target&, ulock> r (insert_target (trace, ctx, tt, p));
        t = &r.first;

        // Cache the process path if we've created the target (it's possible
        // that the same target will be imported via different paths, e.g., as
        // a simple name via PATH search and as an absolute path in which case
        // the first import will determine the path).
        //
        if (r.second.owns_lock ())
        {
          r.first.as<exe> ().process_path (move (pp));
          r.second.unlock ();
        }
      }

      // Save the metadata. Note that this happens during the load phase and
      // so MT-safe.
      //
      if (meta)
        parse_metadata ((*t)->rw (), *md, loc);

      return *t;
    }

    if (opt || exist)
      return nullptr;

    diag_record dr;
    dr << fail (loc) << "unable to import target " << pk;

    if (proj.empty ())
      dr << info << "consider adding its installation location" <<
        info << "or explicitly specify its project name";
    else
    {
      string projv (proj.variable ());

      // Suggest normal import.
      //
      dr << info << "use config.import." << projv << " configuration variable "
         << "to specify its project out_root";

      // Suggest ad hoc import.
      //
      string v (tt.is_a<exe> () && (projv == *tk.name || proj == *tk.name)
                ? "config." + projv
                : "config.import." + projv + '.' + *tk.name + '.' + tt.name);

      dr << info << "or use " << v << " configuration variable to specify its "
         << "path";
    }

    dr << endf;
  }

  pair<const target*, import_kind>
  import_direct (bool& new_value,
                 scope& base,
                 name tgt,
                 bool ph2,
                 bool opt,
                 bool metadata,
                 const location& loc,
                 const char* what)
  {
    // This is like normal import() except we return the target rather than
    // its name.
    //
    tracer trace ("import_direct");

    l5 ([&]{trace << tgt << " from " << base << " for " << what;});

    assert ((!opt || ph2) && (!metadata || ph2));

    context& ctx (base.ctx);
    assert (ctx.phase == run_phase::load);

    auto meta (metadata ? optional<string> (tgt.value) : nullopt);

    names ns;
    import_kind k;
    const target* t (nullptr);

    pair<name, optional<dir_path>> r (
      import_search (new_value,
                     base,
                     move (tgt),
                     opt,
                     meta,
                     true /* subpproj */,
                     loc,
                     what));

    // If there is no project, we are either done or go straight to phase 2.
    //
    if (!r.second || r.second->empty ())
    {
      k = r.second.has_value () ? import_kind::adhoc : import_kind::fallback;

      if (r.first.empty ())
      {
        assert (opt);
        return make_pair (t, k); // NULL
      }
      else if (r.first.qualified ())
      {
        if (ph2)
        {
          names ns {move (r.first)};

          // This is tricky: we only want the optional semantics for the
          // fallback case.
          //
          t = import (ctx,
                      base.find_prerequisite_key (ns, loc),
                      opt && !r.second,
                      meta,
                      false /* existing */,
                      loc);
        }

        if (t == nullptr)
          return make_pair (t, k); // NULL

        // Otherwise fall through.
      }
      else
        ns.push_back (move (r.first)); // And fall through.
    }
    else
    {
      k = import_kind::normal;
      ns = import_load (base.ctx, move (r), metadata, loc).first;
    }

    if (t == nullptr)
    {
      // Similar logic to perform's search().
      //
      target_key tk (base.find_target_key (ns, loc));
      t = ctx.targets.find (tk, trace);
      if (t == nullptr)
        fail (loc) << "unknown imported target " << tk;
    }

    if (meta)
    {
      if (auto* v = cast_null<uint64_t> (t->vars[ctx.var_export_metadata]))
      {
        if (*v != 1)
          fail (loc) << "unexpected metadata version " << *v
                     << " in imported target " << *t;
      }
      else
        fail (loc) << "no metadata for imported target " << *t;
    }

    return make_pair (t, k);
  }

  ostream&
  operator<< (ostream& o, const pair<const exe*, import_kind>& p)
  {
    assert (p.first != nullptr);

    if (p.second == import_kind::normal)
      o << *p.first;
    else
      o << p.first->process_path ();

    return o;
  }

  void
  create_project (const dir_path& d,
                  const optional<dir_path>& amal,
                  const strings& bmod,
                  const string&  rpre,
                  const strings& rmod,
                  const string&  rpos,
                  const optional<string>& config,
                  bool buildfile,
                  const char* who,
                  uint16_t verbosity)
  {
    string hdr ("# Generated by " + string (who) + ". Edit if you know"
                " what you are doing.\n"
                "#");

    // If the directory exists, verify it's empty. Otherwise, create it.
    //
    if (exists (d))
    {
      if (!empty (d))
        fail << "directory " << d << " exists and is not empty";
    }
    else
      mkdir_p (d, verbosity);

    // Create the build/ subdirectory.
    //
    // Note that for now we use the standard build file/directory scheme.
    //
    mkdir (d / std_build_dir, verbosity);

    // Write build/bootstrap.build.
    //
    {
      path f (d / std_bootstrap_file);

      if (verb >= verbosity)
        text << (verb >= 2 ? "cat >" : "save ") << f;

      try
      {
        ofdstream ofs (f);

        ofs << hdr << endl
            << "project =" << endl;

        if (amal)
        {
          ofs << "amalgamation =";

          if (!amal->empty ())
          {
            ofs << ' ';
            to_stream (ofs, *amal, true /* representation */);
          }

          ofs << endl;
        }

        ofs << endl;

        if (config)
          ofs << "using " << *config << endl;

        for (const string& m: bmod)
        {
          if (!config || m != *config)
            ofs << "using " << m << endl;
        }

        ofs.close ();
      }
      catch (const io_error& e)
      {
        fail << "unable to write to " << f << ": " << e;
      }
    }

    // Write build/root.build.
    //
    {
      path f (d / std_root_file);

      if (verb >= verbosity)
        text << (verb >= 2 ? "cat >" : "save ") << f;

      try
      {
        ofdstream ofs (f);

        ofs << hdr << endl;

        if (!rpre.empty ())
          ofs << rpre << endl
              << endl;

        for (const string& cm: rmod)
        {
          // If the module name start with '?', then use optional load.
          //
          bool opt (cm.front () == '?');
          string m (cm, opt ? 1 : 0);

          // Append .config unless the module name ends with '.', in which
          // case strip it.
          //
          if (m.back () == '.')
            m.pop_back ();
          else
            m += ".config";

          ofs << "using" << (opt ? "?" : "") << " " << m << endl;
        }

        if (!rpos.empty ())
          ofs << endl
              << rpre << endl;

        ofs.close ();
      }
      catch (const io_error& e)
      {
        fail << "unable to write to " << f << ": " << e;
      }
    }

    // Write root buildfile.
    //
    if (buildfile)
    {
      path f (d / std_buildfile_file);

      if (verb >= verbosity)
        text << (verb >= 2 ? "cat >" : "save ") << f;

      try
      {
        ofdstream ofs (f);

        ofs << hdr << endl
            << "./: {*/ -build/}" << endl;

        ofs.close ();
      }
      catch (const io_error& e)
      {
        fail << "unable to write to " << f << ": " << e;
      }
    }
  }
}
