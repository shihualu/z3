/*++
  Copyright (c) 2017 Microsoft Corporation

  Module Name:

  <name>

  Abstract:

  <abstract>

  Author:
  Nikolaj Bjorner (nbjorner)
  Lev Nachmanson (levnach)

  Revision History:


  --*/
#pragma once
#include <functional>
#include "math/lp/nla_expr.h"
namespace nla {
class cross_nested {
    typedef nla_expr<rational> nex;
    std::function<void (unsigned)> m_call_on_result;
public:
    cross_nested(std::function<void (unsigned)> call_on_result): m_call_on_result(call_on_result) {}
 
    void cross_nested_of_expr_on_front_elem(nex& e, nex* c, vector<nex*>& front) {
        SASSERT(c->is_sum());
        vector<lpvar> occurences = get_mult_occurences(*c);
        TRACE("nla_cn", tout << "e=" << e << "\nc=" << *c << "\noccurences="; print_vector(occurences, tout) << "\nfront:"; print_vector_of_ptrs(front, tout) << "\n";);
    
        if (occurences.empty()) {
            if(front.empty()) {
                TRACE("nla_cn_cn", tout << "got the cn form: e=" << e << "\n";);
                SASSERT(!can_be_cross_nested_more(e));
                auto i = interval_of_expr(e);
                m_intervals.check_interval_for_conflict_on_zero(i);
            } else {
                nex* c = pop_back(front);
                cross_nested_of_expr_on_front_elem(e, c, front);     
            }
        } else {
            TRACE("nla_cn", tout << "save c=" << *c << "front:"; print_vector_of_ptrs(front, tout) << "\n";);           
            nex copy_of_c = *c;
            for(lpvar j : occurences) {
                cross_nested_of_expr_on_sum_and_var(e, c, j, front);
                *c = copy_of_c;
                TRACE("nla_cn", tout << "restore c=" << *c << ", e=" << e << "\n";);   
            }
        }
        TRACE("nla_cn", tout << "exit\n";);
    }
// e is the global expression, c is the sub expressiond which is going to changed from sum to the cross nested form
void horner::cross_nested_of_expr_on_sum_and_var(nex& e, nex* c, lpvar j, vector<nex*>& front) {
    TRACE("nla_cn", tout << "e=" << e << "\nc=" << *c << "\nj = v" << j << "\nfront="; print_vector_of_ptrs(front, tout) << "\n";);
    split_with_var(*c, j, front);
    TRACE("nla_cn", tout << "after split c=" << *c << "\nfront="; print_vector_of_ptrs(front, tout) << "\n";);    
    do {
        nex* n = pop_back(front);
        cross_nested_of_expr_on_front_elem(e, n, front);
    } while (!front.empty());
}
void process_var_occurences(lpvar j, std::unordered_set<lpvar>& seen, std::unordered_map<lpvar, unsigned>& occurences) {
    if (seen.find(j) != seen.end()) return;
    seen.insert(j);
    auto it = occurences.find(j);
    if (it == occurences.end())
        occurences[j] = 1;
    else
        it->second ++;
}

void process_mul_occurences(const nex& e, std::unordered_set<lpvar>& seen, std::unordered_map<lpvar, unsigned>& occurences) {
    SASSERT(e.type() == expr_type::MUL);
    for (const auto & ce : e.children()) {
         if (ce.type() ==  expr_type::VAR) {
            process_var_occurences(ce.var(), seen, occurences);
        } else if (ce.type() ==  expr_type::MUL){
            process_mul_occurences(ce, seen, occurences);
        } 
    }
}


// j -> the number of expressions j appears in as a multiplier
vector<lpvar> horner::get_mult_occurences(const nex& e) const {
    std::unordered_map<lpvar, unsigned> occurences;
    SASSERT(e.type() == expr_type::SUM);
    for (const auto & ce : e.children()) {
        std::unordered_set<lpvar> seen;
        if (ce.type() == expr_type::MUL) {
            for (const auto & cce : ce.children()) {
                if (cce.type() ==  expr_type::VAR) {
                    process_var_occurences(cce.var(), seen, occurences);
                } else if (cce.type() == expr_type::MUL) {
                    process_mul_occurences(cce, seen, occurences);
                } else {
                    continue;
                }
            }
        } else if (ce.type() ==  expr_type::VAR) {
            process_var_occurences(ce.var(), seen, occurences);
        }
    }
    TRACE("nla_cn_details",
          tout << "{";
          for(auto p: occurences) {
              tout << "(v" << p.first << "->" << p.second << ")";
          }
          tout << "}" << std::endl;);    
    vector<lpvar> r;
    for(auto p: occurences) {
        if (p.second > 1)
            r.push_back(p.first);
    }
    return r;
}
bool horner::can_be_cross_nested_more(const nex& e) const {
    switch (e.type()) {
    case expr_type::SCALAR:
        return false;
    case expr_type::SUM: {
        return !get_mult_occurences(e).empty();
    }
    case expr_type::MUL:
        {
            for (const auto & c: e.children()) {
                if (can_be_cross_nested_more(c))
                    return true;
            }
            return false;
        }
    case expr_type::VAR:
        return false;
    default:
        TRACE("nla_cn_details", tout << e.type() << "\n";);
        SASSERT(false);
        return false;
    }
}
void horner::split_with_var(nex& e, lpvar j, vector<nex*> & front) {
    TRACE("nla_cn_details", tout << "e = " << e << ", j = v" << j << "\n";);
    if (!e.is_sum())
        return;
    nex a, b;
    for (const nex & ce: e.children()) {
        if ((ce.is_mul() && ce.contains(j)) || (ce.is_var() && ce.var() == j)) {
            a.add_child(ce / j);
        } else {
            b.add_child(ce);
        }        
    }
    a.type() = expr_type::SUM;
    TRACE("nla_cn_details", tout << "a = " << a << "\n";);
    SASSERT(a.children().size() >= 2);
    
    if (b.children().size() == 1) {
        nex t = b.children()[0];
        b = t;      
    } else if (b.children().size() > 1) {
        b.type() = expr_type::SUM;        
    }

    if (b.is_undef()) {
        SASSERT(b.children().size() == 0);
        e = nex(expr_type::MUL);        
        e.add_child(nex::var(j));
        e.add_child(a);
        if (a.size() > 1) {
            front.push_back(&e.children().back());
            TRACE("nla_cn_details", tout << "push to front " << e.children().back() << "\n";);
        }
      
    } else {
        TRACE("nla_cn_details", tout << "b = " << b << "\n";);
        e = nex::sum(nex::mul(nex::var(j), a), b);
        if (a.is_sum()) {
            front.push_back(&(e.children()[0].children()[1]));
            TRACE("nla_cn_details", tout << "push to front " << e.children()[0].children()[1] << "\n";);
        }
        if (b.is_sum()) {
            front.push_back(&(e.children()[1]));
            TRACE("nla_cn_details", tout << "push to front " << e.children()[1] << "\n";);
        }
    }
}
std::set<lpvar> horner::get_vars_of_expr(const nex &e ) const {
    std::set<lpvar> r;
    switch (e.type()) {
    case expr_type::SCALAR:
        return r;
    case expr_type::SUM:
    case expr_type::MUL:
        {
            for (const auto & c: e.children())
                for ( lpvar j : get_vars_of_expr(c))
                    r.insert(j);
        }
        return r;
    case expr_type::VAR:
        r.insert(e.var());
        return r;
    default:
        TRACE("nla_cn_details", tout << e.type() << "\n";);
        SASSERT(false);
        return r;
    }

}

};
}