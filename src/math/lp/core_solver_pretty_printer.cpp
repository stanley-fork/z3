/*++
Copyright (c) 2017 Microsoft Corporation

Module Name:

    <name>

Abstract:

    <abstract>

Author:

    Lev Nachmanson (levnach)

Revision History:


--*/
#include "math/lp/numeric_pair.h"
#include "math/lp/core_solver_pretty_printer_def.h"
template lp::core_solver_pretty_printer<lp::mpq, lp::mpq>::core_solver_pretty_printer(const lp::lp_core_solver_base<lp::mpq, lp::mpq> &, std::ostream & out);
template void lp::core_solver_pretty_printer<lp::mpq, lp::mpq>::print();
template lp::core_solver_pretty_printer<lp::mpq, lp::numeric_pair<lp::mpq> >::core_solver_pretty_printer(const lp::lp_core_solver_base<lp::mpq, lp::numeric_pair<lp::mpq> > &, std::ostream & out);
template void lp::core_solver_pretty_printer<lp::mpq, lp::numeric_pair<lp::mpq> >::print();
