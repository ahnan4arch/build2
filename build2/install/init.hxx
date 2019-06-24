// file      : build2/install/init.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BUILD2_INSTALL_INIT_HXX
#define BUILD2_INSTALL_INIT_HXX

#include <libbuild2/types.hxx>
#include <libbuild2/utility.hxx>

#include <libbuild2/module.hxx>

namespace build2
{
  namespace install
  {
    bool
    boot (scope&, const location&, unique_ptr<module_base>&);

    bool
    init (scope&,
          scope&,
          const location&,
          unique_ptr<module_base>&,
          bool,
          bool,
          const variable_map&);
  }
}

#endif // BUILD2_INSTALL_INIT_HXX
