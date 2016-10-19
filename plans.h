/* -*-C++-*- */
/*
 * Partial plans, and their components.
 *
 * Copyright (C) 2002-2004 Carnegie Mellon University
 * Written by H�kan L. S. Younes.
 *
 * Permission is hereby granted to distribute this software for
 * non-commercial research purposes, provided that this copyright
 * notice is included with any such distribution.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
 * SOFTWARE IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU
 * ASSUME THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.
 *
 * $Id: plans.h,v 6.7 2003-09-10 18:14:20 lorens Exp $
 */
#ifndef PLANS_H
#define PLANS_H

#include "chain.h"
#include "flaws.h"
#include "orderings.h"
#include "actions.h"
#include "links.h"
#include "decompositions.h"

struct Parameters;
struct BindingList;
struct Literal;
struct Atom;
struct Negation;
struct Effect;
struct EffectList;
struct Action;
struct Problem;
struct Bindings;
struct ActionEffectMap;
struct FlawSelectionOrder;


/* ====================================================================== */
/* DecompositionFrame */

/* Represents an instantiated decomposition. It's the decompositional analogue to
   the basic plan step. */
struct DecompositionFrame {

    /* Constructs a decomposition step instantiated from a decomposition. */
    DecompositionFrame(int id, const Decomposition& decomposition)
        : id_(id), 
          decomposition_(&decomposition),
          steps_(decomposition.pseudo_steps()),
          binding_list_(decomposition.binding_list()),
          ordering_list_(decomposition.ordering_list()),
          link_list_(decomposition.link_list()) {}

    /* Constructs a decomposition step. */
    DecompositionFrame(const DecompositionFrame& ds)
        : id_(ds.id_), 
          decomposition_(ds.decomposition_),
          steps_(ds.steps_),
          binding_list_(ds.binding_list()),
          ordering_list_(ds.ordering_list()),
          link_list_(ds.link_list()) {}

    /* Returns the decomposition step id. */
    int id() const { return id_; }

    /* Returns the decomposition that this step is instantiated from. */
    const Decomposition& decomposition() const { return *decomposition_; }

    /* Returns the steps that belong to this decomposition. */
    const StepList steps() const { return steps_; }

    /* Returns the list of bindings for this decomposition. */
    const BindingList binding_list() const { return binding_list_; }

    /* Returns the list of ordering constraints of this decomposition. */
    const OrderingList ordering_list() const { return ordering_list_; }

    /* Returns the list of causal links of this decomposition. */
    const LinkList link_list() const { return link_list_; }

private:

    /* Decomposition step id */
    int id_;

    /* Decomposition that this decomposition step is instantiated from. */
    const Decomposition* decomposition_;

    /* List of steps that belong to this decomposition. */
    StepList steps_;

    /* List of bindings for this decomposition. */
    BindingList binding_list_;

    /* List of ordering constraints of this decomposition. */
    OrderingList ordering_list_;

    /* List of causal links of this decomposition. */
    LinkList link_list_;
};


/* ====================================================================== */
/* Decomposition Link */

/*
* A DecompositionLink is used to record the fact that the purpose of some
* step s is to be part of a more-primitive realization of some other step.
*/
struct DecompositionLink {

    /* Constructs a decomposition link. */
    DecompositionLink(int composite_id, DecompositionFrame& decomposition_step)
        : composite_id_(composite_id), decomposition_step_(decomposition_step) {}

    /* Returns the id of the composite step being decomposed. */
    int composite_id() const { return composite_id_; }

    /* Returns the decomposition step that refines the composite step of this decomposition link. */
    const DecompositionFrame decomposition_step() const { return decomposition_step_; }

private:

    /* Id of the composite step being decomposed. */
    int composite_id_;

    /* The decomposition step that refines the composite step identified by the id. */
    DecompositionFrame decomposition_step_;

};

/* Equality operator for decomposition links. */
inline bool operator==(const DecompositionLink& l1, const DecompositionLink& l2) {
    return &l1 == &l2;
}


/* ====================================================================== */
/* Plan */

/*
 * Plan.
 */
struct Plan {
    /* Id of goal step. */
    static const int GOAL_ID;

    /* Returns plan for given problem. */
    static const Plan* plan(const Problem& problem, const Parameters& params,
        bool last_problem);

    /* Cleans up after planning. */
    static void cleanup();

    /* Deletes this plan. */
    ~Plan();

    /* Returns the steps of this plan. */
    const Chain<Step>* steps() const { return steps_; }

    /* Returns the number of unique steps in this plan. */
    int num_steps() const { return num_steps_; }

    /* Returns the links of this plan. */
    const Chain<Link>* links() const { return links_; }

    /* Returns the number of links in this plan. */
    size_t num_links() const { return num_links_; }

    /* Returns the ordering constraints of this plan. */
    const Orderings& orderings() const { return *orderings_; }

    /* Returns the bindings of this plan. */
    const Bindings* bindings() const;

    /* Returns the decomposition links of this plan. */
    const Chain<DecompositionLink>* decomposition_links() const { return decomposition_links_; }

    /* Returns the number of decomposition links in this plan. */
    size_t num_decomposition_links() const { return num_decomposition_links_; }

    /* Returns the potentially threatened links of this plan. */
    const Chain<Unsafe>* unsafes() const { return unsafes_; }

    /* Returns the number of potentially threatened links in this plan. */
    size_t num_unsafes() const { return num_unsafes_; }

    /* Returns the open conditions of this plan. */
    const Chain<OpenCondition>* open_conds() const { return open_conds_; }

    /* Returns the number of open conditions in this plan. */
    size_t num_open_conds() const { return num_open_conds_; }

    /* Returns the unexpanded composite steps of this plan. */
    const Chain<UnexpandedCompositeStep>* unexpanded_steps() const { return unexpanded_steps_; }

    /* Returns the number of unexpanded composite steps of this plan. */
    size_t num_unexpanded_steps() const { return num_unexpanded_steps_; }

    /* Returns the mutex threats of this plan. */
    const Chain<MutexThreat>* mutex_threats() const { return mutex_threats_; }

    /* Checks if this plan is complete. */
    bool complete() const;

    /* Returns the primary rank of this plan, where a lower rank
       signifies a better plan. */
    float primary_rank() const;

    /* Returns the serial number of this plan. */
    size_t serial_no() const;

#ifdef DEBUG
    /* Returns the depth of this plan. */
    size_t depth() const { return depth_; }
#endif

    /* Counts the number of refinements for the given threat, and returns true iff 
       the number of refinements does not exceed the given limit. */
    bool unsafe_refinements(int& refinements, 
        int& separable, int& promotable, int& demotable,
        const Unsafe& unsafe, int limit) const;

    /* Checks if the given threat is separable. */
    int separable(const Unsafe& unsafe) const;

    /* Checks if the given open condition is threatened. */
    bool unsafe_open_condition(const OpenCondition& open_cond) const;

    /* Counts the number of refinements for the given open condition, and returns true iff 
       the number of refinements does not exceed the given limit. */
    bool open_cond_refinements(int& refinements, 
        int& addable, int& reusable,
        const OpenCondition& open_cond, int limit) const;

    /* Counts the number of add-step refinements for the given literal open condition, and returns
       true iff the number of refinements does not exceed the given limit. */
    bool addable_steps(int& refinements, const Literal& literal,
        const OpenCondition& open_cond, int limit) const;

    /* Counts the number of reuse-step refinements for the given literal open condition, and returns 
       true iff the number of refinements does not exceed the given limit. */
    bool reusable_steps(int& refinements, const Literal& literal_open_cond,
        const OpenCondition& open_cond, int limit) const;

    /* Counts the number of refinements for the given unexpanded step, and returns true iff 
       the number of refinements does not exceed the given limit. */
    bool unexpanded_step_refinements(int& refinements, 
        int& expandable, 
        const UnexpandedCompositeStep& unexpanded_step, int limit) const;

private:
    /* List of plans. */
    struct PlanList : public std::vector < const Plan* > {
    };

    /* Chain of steps. */
    const Chain<Step>* steps_;

    /* Number of unique steps in plan. */
    int num_steps_;
    
    /* Chain of causal links. */
    const Chain<Link>* links_;
    
    /* Number of causal links. */
    size_t num_links_;
    
    /* Ordering constraints of this plan. */
    const Orderings* orderings_;
    
    /* Binding constraints of this plan. */
    const Bindings* bindings_;

    /* Chain of decomposition links. */
    const Chain<DecompositionLink>* decomposition_links_;

    /* Number of decomposition links. */
    size_t num_decomposition_links_; 
    
    /* Chain of potentially threatened links. */
    const Chain<Unsafe>* unsafes_;
    
    /* Number of potentially threatened links. */
    size_t num_unsafes_;
    
    /* Chain of open conditions. */
    const Chain<OpenCondition>* open_conds_;
    
    /* Number of open conditions. */
    const size_t num_open_conds_;
    
    /* Chain of mutex threats. */
    const Chain<MutexThreat>* mutex_threats_;

    /* Chain of unexpanded composite steps. */
    const Chain<UnexpandedCompositeStep>* unexpanded_steps_;

    /* Number of unexpanded composite steps. */
    const size_t num_unexpanded_steps_;
    
    /* Rank of this plan. */
    mutable std::vector<float> rank_;
    
    /* Plan id (serial number). */
    mutable size_t id_;
#ifdef DEBUG
    /* Depth of this plan in the search space. */
    size_t depth_;
#endif

    /* Returns the initial plan representing the given problem, or NULL
       if goals of problem are inconsistent. */
    static const Plan* make_initial_plan(const Problem& problem);

    /* Constructs a plan. */
    Plan(const Chain<Step>* steps, size_t num_steps,
        const Chain<Link>* links, size_t num_links,
        const Orderings& orderings, const Bindings& bindings,
        const Chain<DecompositionLink>* decomposition_links, size_t num_decomposition_links,
        const Chain<Unsafe>* unsafes, size_t num_unsafes,
        const Chain<OpenCondition>* open_conds, size_t num_open_conds,
        const Chain<UnexpandedCompositeStep>* unexpanded_steps, size_t num_unexpanded_steps,
        const Chain<MutexThreat>* mutex_threats, const Plan* parent);

    /* Returns the next flaw to work on. */
    const Flaw& get_flaw(const FlawSelectionOrder& flaw_order) const;

    /* Returns the refinements for the next flaw to work on. */
    void refinements(PlanList& plans,
        const FlawSelectionOrder& flaw_order) const;

    /* Handles an unsafe link. */
    void handle_unsafe(PlanList& plans, const Unsafe& unsafe) const;

    /* Handles an unsafe link through separation. */
    int separate(PlanList& plans, const Unsafe& unsafe,
        const BindingList& unifier, bool test_only = false) const;

    /* Handles an unsafe link through demotion. */
    int demote(PlanList& plans, const Unsafe& unsafe,
        bool test_only = false) const;

    /* Handles an unsafe link through promotion. */
    int promote(PlanList& plans, const Unsafe& unsasfe,
        bool test_only = false) const;

    /* Adds a plan to the given plan list with an ordering added. */
    void new_ordering(PlanList& plans, size_t before_id, StepTime t1,
        size_t after_id, StepTime t2,
        const Unsafe& unsafe) const;

    /* Handles a mutex threat. */
    void handle_mutex_threat(PlanList& plans,
        const MutexThreat& mutex_threat) const;

    /* Handles a mutex threat through separation. */
    void separate(PlanList& plans, const MutexThreat& mutex_threat,
        const BindingList& unifier) const;

    /* Handles a mutex threat through demotion. */
    void demote(PlanList& plans, const MutexThreat& mutex_threat) const;

    /* Handles a mutex threat through promotion. */
    void promote(PlanList& plans, const MutexThreat& mutex_threat) const;

    /* Adds a plan to the given plan list with an ordering added. */
    void new_ordering(PlanList& plans, size_t before_id, StepTime t1,
        size_t after_id, StepTime t2,
        const MutexThreat& mutex_threat) const;

    /* Handles an open condition. */
    void handle_open_condition(PlanList& plans,
        const OpenCondition& open_cond) const;

    /* Handles a disjunctive open condition. */
    int handle_disjunction(PlanList& plans, const Disjunction& disj,
        const OpenCondition& open_cond,
        bool test_only = false) const;

    /* Handles an inequality open condition. */
    int handle_inequality(PlanList& plans, const Inequality& neq,
        const OpenCondition& open_cond,
        bool test_only = false) const;

    /* Handles a literal open condition by adding a new step. */
    void add_step(PlanList& plans, const Literal& literal,
        const OpenCondition& open_cond,
        const ActionEffectMap& achievers) const;

    /* Handles a literal open condition by reusing an existing step. */
    void reuse_step(PlanList& plans, const Literal& literal,
        const OpenCondition& open_cond,
        const ActionEffectMap& achievers) const;

    /* Adds plans to the given plan list with a link from the given step
       to the given open condition added. */
    int new_link(PlanList& plans, const Step& step, const Effect& effect,
        const Literal& literal, const OpenCondition& open_cond,
        bool test_only = false) const;

    /* Adds plans to the given plan list with a link from the given step
       to the given open condition added using the closed world
       assumption. */
    int new_cw_link(PlanList& plans, const EffectList& effects,
        const Negation& negation, const OpenCondition& open_cond,
        bool test_only = false) const;

    /* Returns a plan with a link added from the given effect to the
       given open condition. */
    int make_link(PlanList& plans, const Step& step, const Effect& effect,
        const Literal& literal, const OpenCondition& open_cond,
        const BindingList& unifier, bool test_only = false) const;

    /* Handles an unexpanded composite step. */
    void handle_unexpanded_composite_step(PlanList& plans,
        const UnexpandedCompositeStep& unexpanded) const;


    friend bool operator<(const Plan& p1, const Plan& p2);
    friend std::ostream& operator<<(std::ostream& os, const Plan& p);
};

/* Less than operator for plans. */
bool operator<(const Plan& p1, const Plan& p2);

/* Output operator for plans. */
std::ostream& operator<<(std::ostream& os, const Plan& p);


#endif /* PLANS_H */
