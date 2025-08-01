/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    ctx_solver_simplify_tactic.cpp

Abstract:

    Context simplifier for propagating solver assignments.

Author:

    Nikolaj (nbjorner) 2012-3-6

Notes:

Implement the inference rule

     n = V |- F[n] = F[x]
     --------------------
         F[x] = F[V]

where n is an uninterpreted variable (fresh for F[x])
and V is a value (true or false) and x is a subterm 
(different from V).

--*/

#include "smt/tactic/ctx_solver_simplify_tactic.h"
#include "ast/arith_decl_plugin.h"
#include "params/smt_params.h"
#include "smt/smt_kernel.h"
#include "ast/ast_pp.h"
#include "ast/rewriter/mk_simplified_app.h"
#include "ast/ast_util.h"

class ctx_solver_simplify_tactic : public tactic {
    ast_manager&          m;
    params_ref            m_params;
    smt_params            m_front_p;
    smt::kernel           m_solver;
    arith_util            m_arith;
    mk_simplified_app     m_mk_app;
    func_decl_ref         m_fn;
    obj_map<sort, func_decl*> m_fns;
    unsigned              m_num_steps;
public:
    ctx_solver_simplify_tactic(ast_manager & m, params_ref const & p = params_ref()):
        m(m), m_params(p), m_solver(m, m_front_p),  
        m_arith(m), m_mk_app(m), m_fn(m), m_num_steps(0) {
        sort* i_sort = m_arith.mk_int();
        m_fn = m.mk_func_decl(symbol(0xbeef101u), i_sort, m.mk_bool_sort());
    }

    tactic * translate(ast_manager & m) override {
        return alloc(ctx_solver_simplify_tactic, m, m_params);
    }

    ~ctx_solver_simplify_tactic() override {
        for (auto & kv : m_fns)
            m.dec_ref(kv.m_value);       
        m_fns.reset();
    }

    char const* name() const override { return "ctx_solver_simplify"; }

    void updt_params(params_ref const & p) override {
        m_solver.updt_params(p);
    }

    void collect_param_descrs(param_descrs & r) override {
        m_solver.collect_param_descrs(r); 
    }
    
    void collect_statistics(statistics & st) const override {
        st.update("solver-simplify-steps", m_num_steps);
    }

    void reset_statistics() override { m_num_steps = 0; }
    
    void operator()(goal_ref const & in, 
                    goal_ref_buffer & result) override {
        reduce(*(in.get()));
        in->inc_depth();
        result.push_back(in.get());
    }

    void cleanup() override {
        reset_statistics();
        m_solver.reset();
    }

protected:


    void reduce(goal& g) {
        if (m.proofs_enabled())
            return;
        TRACE(ctx_solver_simplify_tactic, g.display(tout););
        expr_ref fml(m);
        tactic_report report("ctx-solver-simplify", g);
        if (g.inconsistent())
            return;
        ptr_vector<expr> fmls;
        g.get_formulas(fmls);
        fml = mk_and(m, fmls.size(), fmls.data());
        m_solver.push();
        reduce(fml);
        m_solver.pop(1);
        if (!m.inc())
            return;
        SASSERT(m_solver.get_scope_level() == 0);
        TRACE(ctx_solver_simplify_tactic,
              for (expr* f : fmls) {
                  tout << mk_pp(f, m) << "\n";
              }
              tout << "=>\n";
              tout << fml << "\n";);
        DEBUG_CODE(
        {
            // enable_trace("after_search");
            m_solver.push();
            expr_ref fml1(m);
            fml1 = mk_and(m, fmls.size(), fmls.data());
            fml1 = m.mk_iff(fml, fml1);
            fml1 = m.mk_not(fml1);
            m_solver.assert_expr(fml1);
            lbool is_sat = m_solver.check();
            TRACE(ctx_solver_simplify_tactic, tout << "is non-equivalence sat?: " << is_sat << "\n";);
            if (is_sat == l_true) {
                model_ref mdl;
                m_solver.get_model(mdl);
                TRACE(ctx_solver_simplify_tactic, 
                      tout << "result is not equivalent to input\n";
                      tout << mk_pp(fml1, m) << "\n";
                      tout << "evaluates to: " << (*mdl)(fml1) << "\n";
                      m_solver.display(tout) << "\n";
                      );
                UNREACHABLE();
            }
            m_solver.pop(1);
        });
        g.reset();
        g.assert_expr(fml, nullptr, nullptr);
        IF_VERBOSE(TACTIC_VERBOSITY_LVL, verbose_stream() << "(ctx-solver-simplify :num-steps " << m_num_steps << ")\n";);
    }

    struct expr_pos {
        unsigned m_parent;
        unsigned m_self;
        unsigned m_idx;
        expr*    m_expr;
        expr_pos(unsigned p, unsigned s, unsigned i, expr* e):
            m_parent(p), m_self(s), m_idx(i), m_expr(e)
        {}
        expr_pos():
            m_parent(0), m_self(0), m_idx(0), m_expr(nullptr)
        {}
    };

    void reduce(expr_ref& result){
        SASSERT(m.is_bool(result));
        ptr_vector<expr> names;
        svector<expr_pos> todo;
        expr_ref_vector fresh_vars(m), trail(m);
        expr_ref res(m), tmp(m);
        obj_map<expr, expr_pos> cache;        
        unsigned id = 1, child_id = 0;
        expr_ref n2(m), fml(m);
        unsigned parent_pos = 0, self_pos = 0, self_idx = 0;
        app * a;
        unsigned sz;
        expr_pos path_r;
        expr_ref_vector args(m);
        expr_ref n = mk_fresh(id, m.mk_bool_sort());
        trail.push_back(n);        

        fml = result.get();
        tmp = m.mk_not(m.mk_iff(fml, n));
        m_solver.assert_expr(tmp);

        todo.push_back(expr_pos(0,0,0,fml));
        names.push_back(n);
        m_solver.push();

        while (!todo.empty() && m.inc()) {            
            expr_ref res(m);
            args.reset();
            expr* e    = todo.back().m_expr;
            self_pos   = todo.back().m_self;
            parent_pos = todo.back().m_parent;
            self_idx   = todo.back().m_idx;
            n = names.back();
            bool found = false;

            
            if (cache.contains(e)) {
                goto done;
            }
            if (m.is_true(e) || m.is_false(e)) {
                res = e;
                goto done;
            }
            if (m.is_bool(e) && simplify_bool(n, res)) {
                TRACE(ctx_solver_simplify_tactic,
                    m_solver.display(tout) << "\n";
                      tout << "simplified: " << mk_pp(n, m) << "\n" << mk_pp(e, m) << " |-> " << mk_pp(res, m) << "\n";);
                goto done;
            }
            if (!is_app(e)) {
                res = e;
                goto done;
            }
            
            a = to_app(e);
            sz = a->get_num_args();            
            n2 = nullptr;


            //
            // This is a single traversal version of the context
            // simplifier. It simplifies only the first occurrence of 
            // a sub-term with respect to the context.
            //                                        

            for (unsigned i = 0; i < sz; ++i) {
                expr* arg = a->get_arg(i);
                if (cache.find(arg, path_r) &&
                    path_r.m_parent == self_pos && path_r.m_idx == i) {
                    args.push_back(path_r.m_expr);
                    found = true;
                    continue;
                }
                args.push_back(arg);
            }

            //
            // the context is not equivalent to top-level 
            // if it is already simplified.
            // Bug exposes such a scenario #5256
            // 
            if (!found) {
                args.reset();            
                for (unsigned i = 0; i < sz; ++i) {
                    expr* arg = a->get_arg(i);
                    if (!n2 && !m.is_value(arg)) {
                        n2 = mk_fresh(id, arg->get_sort());
                        trail.push_back(n2);
                        todo.push_back(expr_pos(self_pos, ++child_id, i, arg));
                        names.push_back(n2);
                        args.push_back(n2);
                    }
                    else {
                        args.push_back(arg);
                    }
                }
            }
            m_mk_app(a->get_decl(), args.size(), args.data(), res);
            trail.push_back(res);
            // child needs to be visited.
            if (n2) {
                SASSERT(!found);
                m_solver.push();
                tmp = m.mk_eq(res, n);
                m_solver.assert_expr(tmp);
                continue;
            }
        
        done:
            if (res) {
                cache.insert(e, expr_pos(parent_pos, self_pos, self_idx, res));
            }            
            
            todo.pop_back();
            names.pop_back();
            m_solver.pop(1);
        }
        if (m.inc()) {
            VERIFY(cache.find(fml, path_r));
            result = path_r.m_expr;
        }
    }

    bool simplify_bool(expr* n, expr_ref& res) {
        expr_ref tmp(m);
        m_solver.push();
        m_solver.assert_expr(n);
        lbool is_sat = m_solver.check();
        m_solver.pop(1);
        if (is_sat == l_false) {
            res = m.mk_true();
            return true;
        }

        m_solver.push();
        tmp = m.mk_not(n);
        m_solver.assert_expr(tmp);
        is_sat = m_solver.check();
        m_solver.pop(1);
        if (is_sat == l_false) {
            res = m.mk_false();
            return true;
        }

        return false;
    }

    expr_ref mk_fresh(unsigned& id, sort* s) {
        func_decl* fn;
        if (m.is_bool(s)) {
            fn = m_fn;
        }
        else if (!m_fns.find(s, fn)) {
            fn = m.mk_func_decl(symbol(0xbeef101 + id), m_arith.mk_int(), s);
            m.inc_ref(fn);
            m_fns.insert(s, fn);
        }
        return expr_ref(m.mk_app(fn, m_arith.mk_int(id++)), m);
    }
    
};

tactic * mk_ctx_solver_simplify_tactic(ast_manager & m, params_ref const & p) {
    return clean(alloc(ctx_solver_simplify_tactic, m, p));
}
