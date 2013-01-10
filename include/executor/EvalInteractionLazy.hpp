#pragma once

#include "EvaluatorBase.hpp"

#include "INITM.hpp"
#include "INITL.hpp"
#include "P2M.hpp"
#include "M2M.hpp"
#include "M2L.hpp"
#include "M2P.hpp"
#include "P2P.hpp"
#include "L2P.hpp"
#include "L2L.hpp"

#include <functional>
#include <set>
#include <unordered_set>


template <typename Context, bool IS_FMM>
class EvalInteractionLazy : public EvaluatorBase<Context>
{
  //! type of box
  typedef typename Context::box_type box_type;
  //! Pair of boxees
  typedef std::pair<box_type, box_type> box_pair;
  //! List for P2P interactions    TODO: could further compress these...
  mutable std::vector<box_pair> P2P_list;
  //! List for P2M calls
  mutable std::vector<box_type> P2M_list;
  //! List for M2M calls
  mutable std::vector<box_pair> M2M_list;
  //! List for Long-range (M2P / M2L) interactions
  mutable std::vector<box_pair> LR_list;
  //! List for L2L calls
  mutable std::vector<box_pair> L2L_list;
  //! List for L2P calls
  mutable std::vector<box_type> L2P_list;
  //! Set of unsigned integers
  typedef std::unordered_set<int> Set;
  //! Set for Needed local expansions
  mutable std::set<box_type> L_list;
  //! Set of initialised multipole expansions
  mutable Set initialised_M;
  //! Set of initialised local expansions
  mutable Set initialised_L;
  //! Set of already added L2P operations
  mutable std::set<unsigned> L2P_set;

 public:

	/** Constructor
	 * Precompute the interaction lists, P2P_list and LR_list
	 */
	EvalInteractionLazy(Context& bc) {
    // Queue based tree traversal for P2P, M2P, and/or M2L operations
    std::deque<box_pair> pairQ;
    pairQ.push_back(box_pair(bc.source_tree().root(),
                             bc.target_tree().root()));

    while (!pairQ.empty()) {
      auto b1 = pairQ.front().first;
      auto b2 = pairQ.front().second;
      pairQ.pop_front();

      if (b1.is_leaf()) {
	      if (b2.is_leaf()) {
		      // Both are leaves, P2P
		      P2P_list.push_back(std::make_pair(b2,b1));
	      } else {
		      // Split the second box into children and interact
		      auto c_end = b2.child_end();
		      for (auto cit = b2.child_begin(); cit != c_end; ++cit)
			      interact(bc, b1, *cit, pairQ);
	      }
      } else if (b2.is_leaf()) {
	      // Split the first box into children and interact
	      auto c_end = b1.child_end();
	      for (auto cit = b1.child_begin(); cit != c_end; ++cit)
		      interact(bc, *cit, b2, pairQ);
      } else {
	      // Split the larger of the two into children and interact
	      if (b1.side_length() > b2.side_length()) {
		      // Split the first box into children and interact
		      auto c_end = b1.child_end();
		      for (auto cit = b1.child_begin(); cit != c_end; ++cit)
			      interact(bc, *cit, b2, pairQ);
	      } else {
		      // Split the second box into children and interact
		      auto c_end = b2.child_end();
		      for (auto cit = b2.child_begin(); cit != c_end; ++cit)
			      interact(bc, b1, *cit, pairQ);
	      }
      }
    }
    // run through interaction lists and generate all call lists
    resolve_LR_interactions(bc);
    // Generate L2L and L2P lists
    eval_L_list(bc);
	}

	/** Execute this evaluator by applying the operators to the interaction lists
	 */
  void execute(Context& bc) const {
    // Generate all Multipole coefficients
    eval_P2M_list(bc);
    // Evaluate all M2M operations
    eval_M2M_list(bc);
    // Evaluate queued long-range interactions
    eval_LR_list(bc);
    // Evaluate L2L operations
    eval_L2L_list(bc);
    // Evaluate L2P operations
    eval_L2P_list(bc);
    // Evaluate queued P2P interactions
    eval_P2P_list(bc);
  }

 private:

  /** Recursively resolve all needed multipole expansions */
  void resolve_multipole(Context& bc, const box_type& b) const
  {
    // Early exit if already initialised
    if (initialised_M.count(b.index())) return;

    // setup memory for the expansion
    INITM::eval(bc.kernel(), bc, b);

    if (b.is_leaf()) {
      // eval P2M
      // P2M::eval(bc.kernel(), bc, b);
      P2M_list.push_back(b);
    }
    else {
      // recursively call resolve_multipole on children
      for (auto it=b.child_begin(); it!=b.child_end(); ++it) {
        // resolve the lower multipole
        resolve_multipole(bc, *it);
        // now invoke M2M to get child multipoles
        M2M_list.push_back(std::make_pair(*it,b));
      }
    }
    // set this box as initialised
    initialised_M.insert(b.index());
  }

  /** Downward pass of L2L & L2P
   *  We can always assume that the called Local expansion has been initialised
   */
  void propagate_local(Context& bc, const box_type& b) const
  {
    if (b.is_leaf()) {
      // call L2P
      if (!L2P_set.count(b.index())) {
        L2P_list.push_back(b);
        L2P_set.insert(b.index());
      }
    } else {
      // loop over children and propagate
      for (auto cit=b.child_begin(); cit!=b.child_end(); ++cit) {
        if (!initialised_L.count(cit->index())) {
          // initialised the expansion if necessary
	        INITL::eval(bc.kernel(), bc, *cit);
          initialised_L.insert(cit->index());
        }

        // call L2L on parent -> child
        L2L_list.push_back(std::make_pair(b,*cit));
        // now recurse down the tree
        propagate_local(bc, *cit);
      }
    }
  }

  /** Evaluate long-range interactions
   *  Generate all necessary multipoles */
  // void eval_LR_list(Context& bc) const
  void resolve_LR_interactions(Context& bc) const
  {
    for (auto it=LR_list.begin(); it!=LR_list.end(); ++it) {
      // resolve all needed multipole expansions from lower levels of the tree
      resolve_multipole(bc, it->first);

      // If needed, initialise Local expansion for target box
      if (IS_FMM) {
        if (!initialised_L.count(it->second.index())) {
          // initialise Local expansion at target box if necessary
	        INITL::eval(bc.kernel(), bc, it->second);
          initialised_L.insert(it->second.index());
        }
      }
    }
  }

  void eval_L_list(Context& bc) const
  {
    for (auto it=L_list.begin(); it!=L_list.end(); ++it) {
      // propagate this local expansion down the tree
      propagate_local(bc, *it);
    }
  }

  template <typename Q>
  void interact(Context& bc,
                const box_type& b1, const box_type& b2,
                Q& pairQ) const {
    if (bc.accept_multipole(b1, b2)) {
      // These boxes satisfy the multipole acceptance criteria
      LR_list.push_back(box_pair(b1,b2));
      if (IS_FMM)
        L_list.insert(b2);
    } else {
      pairQ.push_back(box_pair(b1,b2));
    }
  }

  void eval_P2P_list(Context& bc) const
  {
    for (auto it=P2P_list.begin(); it!=P2P_list.end(); ++it) {
      // evaluate this pair using P2P
      P2P::eval(bc.kernel(), bc, it->first, it->second, P2P::ONE_SIDED());
    }
  }

  void eval_P2M_list(Context& bc) const
  {
    for (auto it=P2M_list.begin(); it!=P2M_list.end(); ++it) {
      P2M::eval(bc.kernel(), bc, *it);
    }
  }

  void eval_M2M_list(Context& bc) const
  {
    for (auto it=M2M_list.begin(); it!=M2M_list.end(); ++it) {
      M2M::eval(bc.kernel(), bc, it->first, it->second);
    }
  }

  void eval_LR_list(Context& bc) const
  {
    for (auto it=LR_list.begin(); it!=LR_list.end(); ++it) {
      if (IS_FMM)
        M2L::eval(bc.kernel(), bc, it->first, it->second);
      else
        M2P::eval(bc.kernel(), bc, it->first, it->second);
    }
  }

  void eval_L2L_list(Context& bc) const
  {
    for (auto it=L2L_list.begin(); it!=L2L_list.end(); ++it) {
      L2L::eval(bc.kernel(), bc, it->first, it->second);
    }
  }

  void eval_L2P_list(Context& bc) const
  {
    for (auto it=L2P_list.begin(); it!=L2P_list.end(); ++it) {
      // printf("Calling L2P::eval(%d)\n",it->index());
      L2P::eval(bc.kernel(), bc, *it);
    }
  }
};


template <typename Context, typename Options>
EvaluatorBase<Context>* make_lazy_eval(Context& c, Options& opts) {
  if (opts.evaluator == FMMOptions::FMM) {
	  return new EvalInteractionLazy<Context, true>(c);
  } else if (opts.evaluator == FMMOptions::TREECODE) {
	  return new EvalInteractionLazy<Context, false>(c);
  }
  return nullptr;
}
