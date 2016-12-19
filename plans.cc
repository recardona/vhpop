/*
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
 * $Id: plans.cc,v 6.17 2003-12-10 03:44:19 lorens Exp $
 */
#include "plans.h"
#include "heuristics.h"
#include "bindings.h"
#include "problems.h"
#include "domains.h"
#include "formulas.h"
#include "requirements.h"
#include "parameters.h"
#include "debug.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <typeinfo>
//#include <sys/time.h>


/* Mapping of predicate names to achievers. */
struct PredicateAchieverMap : public std::map < Predicate, ActionEffectMap > {};

/* Planning parameters. */
static const Parameters* params;

/* Domain of problem currently being solved. */
static const Domain* domain = NULL;

/* Problem currently being solved. */
static const Problem* problem = NULL;

/* Planning graph. */
static const PlanningGraph* planning_graph;

/* The goal action. */
static Action* goal_action;

/* Maps predicates to actions. */
static PredicateAchieverMap achieves_pred;

/* Maps negated predicates to actions. */
static PredicateAchieverMap achieves_neg_pred;

/* Whether last flaw was a static predicate. */
static bool static_pred_flaw;

/* Mapping of composite actions to the decompositions that can realize them. */
struct CompositeActionAchieverMap : public std::multimap < const Action*, const Decomposition* > {};

/* Maps actions to decompositions. */
static CompositeActionAchieverMap achieves_composite;

/* Range of decompositions applicable to one composite action. */
typedef std::pair <CompositeActionAchieverMap::const_iterator, CompositeActionAchieverMap::const_iterator> 
    CompositeExpansionsRange;


/* ====================================================================== */
/* Plan */

/*
 * Less than function object for plan pointers.
 */
namespace std {
    template<>
    struct less<const Plan*>
        : public binary_function < const Plan*, const Plan*, bool > {
        /* Comparison function operator. */
        bool operator()(const Plan* p1, const Plan* p2) const {
            return *p1 < *p2;
        }
    };
}


/*
 * A plan queue.
 */
struct PlanQueue : public std::priority_queue < const Plan* > {
};


/* Id of goal step. */
const int Plan::GOAL_ID = std::numeric_limits<int>::max();


/* Adds goal to chain of open conditions, and returns true if and only
   if the goal is consistent. */
static bool add_goal(
    const Chain<OpenCondition>*& open_conds,
    size_t& num_open_conds, 
    BindingList& new_bindings,
    const Formula& goal, 
    size_t step_id,
    bool test_only = false) 
{
    if (goal.tautology()) {
        return true;
    }
    else if (goal.contradiction()) {
        return false;
    }
    std::vector<const Formula*> goals(1, &goal);
    while (!goals.empty()) {
        const Formula* goal = goals.back();
        goals.pop_back();
        const Literal* l;
        FormulaTime when;
        const TimedLiteral* tl = dynamic_cast<const TimedLiteral*>(goal);
        if (tl != NULL) {
            l = &tl->literal();
            when = tl->when();
        }
        else {
            l = dynamic_cast<const Literal*>(goal);
            when = AT_START;
        }
        if (l != NULL) {
            if (!test_only
                && !(params->strip_static_preconditions()
                && PredicateTable::static_predicate(l->predicate()))) {
                open_conds =
                    new Chain<OpenCondition>(OpenCondition(step_id, *l, when),
                    open_conds);
            }
            num_open_conds++;
        }
        else {
            const Conjunction* conj = dynamic_cast<const Conjunction*>(goal);
            if (conj != NULL) {
                const FormulaList& gs = conj->conjuncts();
                for (FormulaList::const_iterator fi = gs.begin();
                    fi != gs.end(); fi++) {
                    if (params->random_open_conditions) {
                        size_t pos = size_t((goals.size() + 1.0)*rand() / (RAND_MAX + 1.0));
                        if (pos == goals.size()) {
                            goals.push_back(*fi);
                        }
                        else {
                            const Formula* tmp = goals[pos];
                            goals[pos] = *fi;
                            goals.push_back(tmp);
                        }
                    }
                    else {
                        goals.push_back(*fi);
                    }
                }
            }
            else {
                const Disjunction* disj = dynamic_cast<const Disjunction*>(goal);
                if (disj != NULL) {
                    if (!test_only) {
                        open_conds =
                            new Chain<OpenCondition>(OpenCondition(step_id, *disj),
                            open_conds);
                    }
                    num_open_conds++;
                }
                else {
                    const BindingLiteral* bl = dynamic_cast<const BindingLiteral*>(goal);
                    if (bl != NULL) {
                        bool is_eq = (typeid(*bl) == typeid(Equality));
                        new_bindings.push_back(Binding(bl->variable(),
                            bl->step_id1(step_id),
                            bl->term(),
                            bl->step_id2(step_id), is_eq));
#ifdef BRANCH_ON_INEQUALITY
                        const Inequality* neq = dynamic_cast<const Inequality*>(bl);
                        if (params->domain_constraints
                            && neq != NULL && bl.term().variable()) {
                            /* Both terms are variables, so handle specially. */
                            if (!test_only) {
                                open_conds =
                                    new Chain<OpenCondition>(OpenCondition(step_id, *neq),
                                    open_conds);
                            }
                            num_open_conds++;
                            new_bindings.pop_back();
                        }
#endif
                    }
                    else {
                        const Exists* exists = dynamic_cast<const Exists*>(goal);
                        if (exists != NULL) {
                            if (params->random_open_conditions) {
                                size_t pos =
                                    size_t((goals.size() + 1.0)*rand() / (RAND_MAX + 1.0));
                                if (pos == goals.size()) {
                                    goals.push_back(&exists->body());
                                }
                                else {
                                    const Formula* tmp = goals[pos];
                                    goals[pos] = &exists->body();
                                    goals.push_back(tmp);
                                }
                            }
                            else {
                                goals.push_back(&exists->body());
                            }
                        }
                        else {
                            const Forall* forall = dynamic_cast<const Forall*>(goal);
                            if (forall != NULL) {
                                const Formula& g = forall->universal_base(SubstitutionMap(),
                                    *problem);
                                if (params->random_open_conditions) {
                                    size_t pos =
                                        size_t((goals.size() + 1.0)*rand() / (RAND_MAX + 1.0));
                                    if (pos == goals.size()) {
                                        goals.push_back(&g);
                                    }
                                    else {
                                        const Formula* tmp = goals[pos];
                                        goals[pos] = &g;
                                        goals.push_back(tmp);
                                    }
                                }
                                else {
                                    goals.push_back(&g);
                                }
                            }
                            else {
                                throw std::logic_error("unknown kind of goal");
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}


/* Returns a set of achievers for the given literal. */
static const ActionEffectMap* literal_achievers(const Literal& literal) {
    if (params->ground_actions) {
        return planning_graph->literal_achievers(literal);
    }
    else if (typeid(literal) == typeid(Atom)) {
        PredicateAchieverMap::const_iterator pai =
            achieves_pred.find(literal.predicate());
        return (pai != achieves_pred.end()) ? &(*pai).second : NULL;
    }
    else {
        PredicateAchieverMap::const_iterator pai =
            achieves_neg_pred.find(literal.predicate());
        return (pai != achieves_neg_pred.end()) ? &(*pai).second : NULL;
    }
}


/* Finds threats to the given link. */
static void link_threats(const Chain<Unsafe>*& unsafes, size_t& num_unsafes,
    const Link& link, const Chain<Step>* steps,
    const Orderings& orderings,
    const Bindings& bindings) {
    StepTime lt1 = link.effect_time();
    StepTime lt2 = end_time(link.condition_time());
    for (const Chain<Step>* sc = steps; sc != NULL; sc = sc->tail) {
        const Step& s = sc->head;
        if (orderings.possibly_not_after(link.from_id(), lt1,
            s.id(), StepTime::AT_END)
            && orderings.possibly_not_before(link.to_id(), lt2,
            s.id(), StepTime::AT_START)) {
            const EffectList& effects = s.action().effects();
            for (EffectList::const_iterator ei = effects.begin();
                ei != effects.end(); ei++) {
                const Effect& e = **ei;
                if (!domain->requirements.durative_actions
                    && e.link_condition().contradiction()) {
                    continue;
                }
                StepTime et = end_time(e);
                if (!(s.id() == link.to_id() && et >= lt2)
                    && orderings.possibly_not_after(link.from_id(), lt1, s.id(), et)
                    && orderings.possibly_not_before(link.to_id(), lt2, s.id(), et)) {
                    if (typeid(link.condition()) == typeid(Negation)
                        || !(link.from_id() == s.id() && lt1 == et)) {
                        if (bindings.affects(e.literal(), s.id(),
                            link.condition(), link.to_id())) {
                            unsafes = new Chain<Unsafe>(Unsafe(link, s.id(), e), unsafes);
                            num_unsafes++;
                        }
                    }
                }
            }
        }
    }
}


/* Finds the threatened links by the given step. */
static void step_threats(const Chain<Unsafe>*& unsafes, size_t& num_unsafes,
    const Step& step, const Chain<Link>* links,
    const Orderings& orderings,
    const Bindings& bindings) {
    const EffectList& effects = step.action().effects();
    for (const Chain<Link>* lc = links; lc != NULL; lc = lc->tail) {
        const Link& l = lc->head;
        StepTime lt1 = l.effect_time();
        StepTime lt2 = end_time(l.condition_time());
        if (orderings.possibly_not_after(l.from_id(), lt1,
            step.id(), StepTime::AT_END)
            && orderings.possibly_not_before(l.to_id(), lt2,
            step.id(), StepTime::AT_START)) {
            for (EffectList::const_iterator ei = effects.begin();
                ei != effects.end(); ei++) {
                const Effect& e = **ei;
                if (!domain->requirements.durative_actions
                    && e.link_condition().contradiction()) {
                    continue;
                }
                StepTime et = end_time(e);
                if (!(step.id() == l.to_id() && et >= lt2)
                    && orderings.possibly_not_after(l.from_id(), lt1, step.id(), et)
                    && orderings.possibly_not_before(l.to_id(), lt2, step.id(), et)) {
                    if (typeid(l.condition()) == typeid(Negation)
                        || !(l.from_id() == step.id() && lt1 == et)) {
                        if (bindings.affects(e.literal(), step.id(),
                            l.condition(), l.to_id())) {
                            unsafes = new Chain<Unsafe>(Unsafe(l, step.id(), e), unsafes);
                            num_unsafes++;
                        }
                    }
                }
            }
        }
    }
}


/* Finds the mutex threats by the given step. */
static void mutex_threats(const Chain<MutexThreat>*& mutex_threats,
    const Step& step, const Chain<Step>* steps,
    const Orderings& orderings,
    const Bindings& bindings) {
    const EffectList& effects = step.action().effects();
    for (const Chain<Step>* sc = steps; sc != NULL; sc = sc->tail) {
        const Step& s = sc->head;
        bool ss, se, es, ee;
        if (orderings.possibly_concurrent(step.id(), s.id(), ss, se, es, ee)) {
            const EffectList& effects2 = s.action().effects();
            for (EffectList::const_iterator ei = effects.begin();
                ei != effects.end(); ei++) {
                const Effect& e = **ei;
                if (e.when() == Effect::AT_START) {
                    if (!ss && !se) {
                        continue;
                    }
                }
                else if (!es && !ee) {
                    continue;
                }
                for (EffectList::const_iterator ej = effects2.begin();
                    ej != effects2.end(); ej++) {
                    const Effect& e2 = **ej;
                    if (e.when() == Effect::AT_START) {
                        if (e2.when() == Effect::AT_START) {
                            if (!ss) {
                                continue;
                            }
                        }
                        else if (!se) {
                            continue;
                        }
                    }
                    else {
                        if (e2.when() == Effect::AT_START) {
                            if (!es) {
                                continue;
                            }
                        }
                        else if (!ee) {
                            continue;
                        }
                    }
                    if (bindings.unify(e.literal().atom(), step.id(),
                        e2.literal().atom(), s.id())) {
                        mutex_threats = new Chain<MutexThreat>(MutexThreat(step.id(), e,
                            s.id(), e2),
                            mutex_threats);
                    }
                }
            }
        }
    }
}


/* Returns binding constraints that make the given steps fully
   instantiated, or NULL if no consistent binding constraints can be
   found. */
static const Bindings* step_instantiation(const Chain<Step>* steps, size_t n,
    const Bindings& bindings) {
    if (steps == NULL) {
        return &bindings;
    }
    else {
        const Step& step = steps->head;
        const ActionSchema* as = dynamic_cast<const ActionSchema*>(&step.action());
        if (as == NULL || as->parameters().size() <= n) {
            return step_instantiation(steps->tail, 0, bindings);
        }
        else {
            const Variable& v = as->parameters()[n];
            if (v != bindings.binding(v, step.id())) {
                return step_instantiation(steps, n + 1, bindings);
            }
            else {
                const Type& t = TermTable::type(v);
                const ObjectList& arguments = problem->terms().compatible_objects(t);
                for (ObjectList::const_iterator oi = arguments.begin();
                    oi != arguments.end(); oi++) {
                    BindingList bl;
                    bl.push_back(Binding(v, step.id(), *oi, 0, true));
                    const Bindings* new_bindings = bindings.add(bl);
                    if (new_bindings != NULL) {
                        const Bindings* result = step_instantiation(steps, n + 1,
                            *new_bindings);
                        if (result != new_bindings) {
                            delete new_bindings;
                        }
                        if (result != NULL) {
                            return result;
                        }
                    }
                }
                return NULL;
            }
        }
    }
}


/* Returns the initial plan representing the given problem, or NULL
   if initial conditions or goals of the problem are inconsistent. */
const Plan* Plan::make_initial_plan(const Problem& problem) 
{
    // Create goal of problem
    if (params->ground_actions) 
    {
        goal_action = new GroundAction("", false, false);
        const Formula& goal_formula = problem.goal().instantiation(SubstitutionMap(), problem);
        goal_action->set_condition(goal_formula);
    }

    else 
    {
        goal_action = new ActionSchema("", false, false);
        goal_action->set_condition(problem.goal());
    }

    // Chain and number of open conditions
    const Chain<OpenCondition>* open_conds = NULL;
    size_t num_open_conds = 0;
    
    // Bindings introduced by goal
    BindingList new_bindings;
    
    // Add goals as open conditions
    if (!add_goal(open_conds, num_open_conds, new_bindings, goal_action->condition(), GOAL_ID)) 
    {
        // Goals are inconsistent
        RCObject::ref(open_conds);
        RCObject::destructive_deref(open_conds);
        return NULL;
    }
    
    // Make chain of mutex threat place holder
    const Chain<MutexThreat>* mutex_threats = new Chain<MutexThreat>(MutexThreat(), NULL);

    // Make chain of initial steps
    const Chain<Step>* steps = 
        new Chain<Step>(Step(0, problem.init_action()),
        new Chain<Step>(Step(GOAL_ID, *goal_action), NULL));
    size_t num_steps = 0;
    
    // Variable bindings
    const Bindings* bindings = &Bindings::EMPTY;
    
    // Step orderings
    const Orderings* orderings;
    
    if (domain->requirements.durative_actions) 
    {    
        const TemporalOrderings* to = new TemporalOrderings();
        
        // Add steps for timed initial literals
        for (TimedActionTable::const_iterator ai = problem.timed_actions().begin();
             ai != problem.timed_actions().end(); 
             ai++) 
        {
            num_steps++;
            steps = new Chain<Step>(Step(num_steps, *(*ai).second), steps);
            const TemporalOrderings* tmp = to->refine((*ai).first, steps->head);
            delete to;

            if (tmp == NULL) 
            {
                RCObject::ref(open_conds);
                RCObject::destructive_deref(open_conds);
                RCObject::ref(steps);
                RCObject::destructive_deref(steps);
                return NULL;
            }
            to = tmp;
        }
        orderings = to;
    }
    else {
        orderings = new BinaryOrderings();
    }

    // Make chain of initial decomposition steps;

    // Return initial plan.
    return new Plan(
        steps, num_steps, 
        NULL, 0, // -> links, num_links
        *orderings, *bindings,
        NULL, 0, // -> decomposition frames, num decomposition frames
        NULL, 0, // -> decomposition links, num decomposition links
        NULL, 0, // -> unsafes, num_unsafes
        open_conds, num_open_conds, 
        NULL, 0, // -> unexpanded_steps, num_unexpanded_steps
        mutex_threats, 
        NULL); // -> parent
}


/* Returns plan for given problem. */
const Plan* Plan::plan(const Problem& problem, const Parameters& p, bool last_problem) 
{
    /* ---------------------------------------------------------------- */
    /* Setup */


    // Set planning parameters.
    params = &p;

    // Set current domain and problem.
    domain = &problem.domain();
    ::problem = &problem;


    /* ---------------------------------------------------------------- */
    /* Planning Graph Pre-processing */


    /* Check if we need to use a planning graph at all. */
    bool need_pg = (params->ground_actions || 
                    params->domain_constraints || 
                    params->heuristic.needs_planning_graph());

    for (size_t i = 0; !need_pg && i < params->flaw_orders.size(); i++) {
        if (params->flaw_orders[i].needs_planning_graph()) {
            need_pg = true;
        }
    }

    /* If so, initialize the planning graph. */
    if (need_pg) {
        planning_graph = new PlanningGraph(problem, *params);
    }
    else {
        planning_graph = NULL;
    }

    /*
     * Initialize the <predicate, action> map. 
     * This dictionary maps predicates to the actions that achieve them.
     */
    if (!params->ground_actions) 
    {
        achieves_pred.clear();
        achieves_neg_pred.clear();

        // For each action schema in the domain...
        for (ActionSchemaMap::const_iterator ai = domain->actions().begin();
             ai != domain->actions().end(); 
             ai++) 
        {
            const ActionSchema* as = (*ai).second;

            // ...for each effect of the action schema...
            for (EffectList::const_iterator ei = as->effects().begin();
                 ei != as->effects().end(); 
                 ei++) 
            {

                // Get the effect as a Literal,
                const Literal& literal = (*ei)->literal();

                // And see if it's an Atom or its negation.
                // If the former, it belongs in the "achieves_pred" map.
                // If the latter, it belongs in the "achieves_neg_pred" map.
                if (typeid(literal) == typeid(Atom)) {
                    achieves_pred[literal.predicate()].insert(std::make_pair(as, *ei));
                }

                else {
                    achieves_neg_pred[literal.predicate()].insert(std::make_pair(as, *ei));
                }
            }
        }

        // We also need to add the initial dummy action to the map.
        const GroundAction& ia = problem.init_action();
        for (EffectList::const_iterator ei = ia.effects().begin();
             ei != ia.effects().end(); 
             ei++) 
        {
            const Literal& literal = (*ei)->literal();
            achieves_pred[literal.predicate()].insert(std::make_pair(&ia, *ei));
        }

        // And because we're also dealing with timed actions, we repeat the process
        // for these as well.
        for (TimedActionTable::const_iterator ai = problem.timed_actions().begin();
             ai != problem.timed_actions().end(); 
             ai++) 
        {
            const GroundAction& action = *(*ai).second;
            for (EffectList::const_iterator ei = action.effects().begin();
                 ei != action.effects().end(); 
                 ei++) 
            {
                const Literal& literal = (*ei)->literal();
                
                if (typeid(literal) == typeid(Atom)) {
                    achieves_pred[literal.predicate()].insert(std::make_pair(&action, *ei));
                }

                else {
                    achieves_neg_pred[literal.predicate()].insert(std::make_pair(&action, *ei));
                }
            }
        }
    }


    /*
    * Initialize the <composite-action, decomposition> map, if necessary.
    * This dictionary maps composite actions to the decompositions that realize them.
    */
    if (problem.domain().requirements.decompositions)
    {
        achieves_composite.clear();


        // For each decomposition schema in the domain...
        for (DecompositionSchemaMap::const_iterator di = domain->decompositions().begin();
             di != domain->decompositions().end();
             di++)
        {
            // Get the name of the action this decomposition applies to.
            std::pair<std::string, std::string> composite_decomp_names = di->first;
            std::string composite_action_name = composite_decomp_names.first;

            // Attempt to find the action.
            const ActionSchema* action = domain->find_action(composite_action_name);

            // If it exists and it is composite, add it to the multimap.
            if (action != NULL && action->composite()) {
                achieves_composite.insert(std::make_pair(action, di->second));
            }

        }
    }

    static_pred_flaw = false;

    /* Number of visited plans. */
    size_t num_visited_plans = 0;

    /* Number of generated plans. */
    size_t num_generated_plans = 0;
    
    /* Number of static preconditions encountered. */
    size_t num_static = 0;
    
    /* Number of dead ends encountered. */
    size_t num_dead_ends = 0;

    /* Generated plans for different flaw selection orders. */
    std::vector<size_t> generated_plans(params->flaw_orders.size(), 0);
    
    /* Queues of pending plans. */
    std::vector<PlanQueue> plans(params->flaw_orders.size(), PlanQueue());
    
    /* Dead plan queues. */
    std::vector<PlanQueue*> dead_queues;

    /* Variable for progress bar (number of generated plans). */
    size_t last_dot = 0;

    /* Variable for progress bar (time). */
    size_t last_hash = 0;

    /* ---------------------------------------------------------------- */
    /* Searching for Complete Plan */

    /* 
     * Flaw repair strategies are used in a round-robin fashion. Here, we
     * initialize some auxiliary variables to track which strategy we're
     * using, as well as the number of expansions to allow before switching
     * between strategies.
     */
    size_t current_flaw_order = 0;
    size_t flaw_orders_left = params->flaw_orders.size();
    size_t next_switch = 1000;

    /* Construct the initial plan. */
    const Plan* initial_plan = make_initial_plan(problem);
    if (initial_plan != NULL) {
        initial_plan->id_ = 0;
    }

    /* Set the current plan under consideration to the initial. */
    const Plan* current_plan = initial_plan;
    generated_plans[current_flaw_order]++;
    num_generated_plans++;
    

    if (verbosity > 1) {
        std::cerr << "using flaw order " << current_flaw_order << std::endl;
    }
    
    /* 
     * In the case of IDA_STAR, we need an additional 
     * parameter: the upper limit for A_STAR search. 
     */
    float f_limit;
    if (current_plan != NULL && 
        params->search_algorithm == Parameters::IDA_STAR) {
        f_limit = current_plan->primary_rank();
    }
    
    else {
        f_limit = std::numeric_limits<float>::infinity();
    }

    /* Begin the search. */
    // BEGIN DO-WHILE
    do {

        float next_f_limit = std::numeric_limits<float>::infinity();
        
        while (current_plan != NULL && !current_plan->complete()) 
        {
            /* Do a little amortized cleanup of dead queues. */
            for (size_t dq = 0; dq < 4 && !dead_queues.empty(); dq++) 
            {
                PlanQueue& dead_queue = *dead_queues.back();
                delete dead_queue.top();
               
                dead_queue.pop();
                if (dead_queue.empty()) {
                    dead_queues.pop_back();
                }
            }

            // TODO: ADD THIS BACK IN
            //      struct itimerval timer;
            //#ifdef PROFILING
            //      getitimer(ITIMER_VIRTUAL, &timer);
            //#else
            //      getitimer(ITIMER_PROF, &timer);
            //#endif
            //      double t = 1000000.9
            //	- (timer.it_value.tv_sec + timer.it_value.tv_usec*1e-6);
            //      if (t >= 60.0*params->time_limit) {
            //	/* Time limit exceeded. */
            //	break;
            //      }

            /* Visiting a new plan. */
            num_visited_plans++;
            
            if (verbosity == 1) {
                while (num_generated_plans - num_static - last_dot >= 1000) {
                    std::cerr << '.';
                    last_dot += 1000;
                }
                /*while (t - 60.0*last_hash >= 60.0) {
                  std::cerr << '#';
                  last_hash++;
                  }*/
            }

            if (verbosity > 1) {
                std::cerr << std::endl << (num_visited_plans - num_static) << ": "
                    << "!!!!CURRENT PLAN (id " << current_plan->id_ << ")"
                    << " with rank (" << current_plan->primary_rank();
                for (size_t ri = 1; ri < current_plan->rank_.size(); ri++) {
                    std::cerr << ',' << current_plan->rank_[ri];
                }
                std::cerr << ")" << std::endl << *current_plan << std::endl;
            }

            /* List of children to current plan. */
            PlanList refinements;
            
            /* Get plan refinements. */
            current_plan->refinements(refinements, params->flaw_orders[current_flaw_order]);
            
            /* Add children to queue of pending plans. */
            bool added = false;

            for (PlanList::const_iterator pi = refinements.begin(); pi != refinements.end(); pi++) 
            {
                const Plan& new_plan = **pi;
                
                /* N.B. Must set id before computing rank, because it may be used. */
                new_plan.id_ = num_generated_plans;
                
                if (new_plan.primary_rank() != std::numeric_limits<float>::infinity()
                    && (generated_plans[current_flaw_order] < params->search_limits[current_flaw_order])) 
                {
                    if (params->search_algorithm == Parameters::IDA_STAR && new_plan.primary_rank() > f_limit) 
                    {
                        next_f_limit = std::min(next_f_limit, new_plan.primary_rank());
                        delete &new_plan;
                        continue;
                    }

                    if (!added && static_pred_flaw) {
                        num_static++;
                    }

                    added = true;
                    plans[current_flaw_order].push(&new_plan);
                    generated_plans[current_flaw_order]++;
                    num_generated_plans++;
                    
                    if (verbosity > 2) 
                    {
                        std::cerr << std::endl << "####CHILD (id " << new_plan.id_ << ")"
                            << " with rank (" << new_plan.primary_rank();

                        for (size_t ri = 1; ri < new_plan.rank_.size(); ri++) {
                            std::cerr << ',' << new_plan.rank_[ri];
                        }

                        std::cerr << "):" << std::endl << new_plan << std::endl;
                    }
                }

                else {
                    delete &new_plan;
                }
            }

            if (!added) {
                num_dead_ends++;
            }

            /* Process next plan. */
            bool limit_reached = false;
            if ((limit_reached = (generated_plans[current_flaw_order] >= params->search_limits[current_flaw_order]))
                || generated_plans[current_flaw_order] >= next_switch) 
            {
                if (verbosity > 1) 
                {
                    std::cerr << "time to switch ("
                        << generated_plans[current_flaw_order] << ")" << std::endl;
                }

                if (limit_reached) 
                {
                    flaw_orders_left--;
                
                    /* Discard the rest of the plan queue. */
                    dead_queues.push_back(&plans[current_flaw_order]);
                }

                if (flaw_orders_left > 0) 
                {
                    do {
                        current_flaw_order++;
                        if (verbosity > 1) {
                            std::cerr << "use flaw order "
                                << current_flaw_order << "?" << std::endl;
                        }
                        if (current_flaw_order >= params->flaw_orders.size()) {
                            current_flaw_order = 0;
                            next_switch *= 2;
                        }
                    } while ((generated_plans[current_flaw_order]
                        >= params->search_limits[current_flaw_order]));
                    if (verbosity > 1) {
                        std::cerr << "using flaw order " << current_flaw_order
                            << std::endl;
                    }
                }
            }

            if (flaw_orders_left > 0) {
                if (generated_plans[current_flaw_order] == 0) {
                    current_plan = initial_plan;
                    generated_plans[current_flaw_order]++;
                    num_generated_plans++;
                }
                else {
                    if (current_plan != initial_plan) {
                        delete current_plan;
                    }
                    
                    /* Problem lacks solution. */
                    if (plans[current_flaw_order].empty()) {
                        current_plan = NULL;
                    }

                    /* Get the next plan off the fringe for the current flaw order. */
                    else 
                    {
                        current_plan = plans[current_flaw_order].top();
                        plans[current_flaw_order].pop();
                    }
                }

                /* Instantiate all actions if the plan is otherwise complete. */
                bool instantiated = params->ground_actions;
                while (current_plan != NULL && current_plan->complete() && !instantiated) 
                {
                    const Bindings* new_bindings = step_instantiation(current_plan->steps(), 0, *current_plan->bindings_);
                    
                    if (new_bindings != NULL) 
                    {
                        instantiated = true;
                        if (new_bindings != current_plan->bindings_) 
                        {
                            // TODO: Fix the unexpanded composite step handling.
                            const Plan* inst_plan =
                                new Plan(current_plan->steps(), current_plan->num_steps(),
                                current_plan->links(), current_plan->num_links(),
                                current_plan->orderings(), *new_bindings,
                                current_plan->decomposition_frames(), current_plan->num_decomposition_frames(),
                                current_plan->decomposition_links(), current_plan->num_decomposition_links(),
                                NULL, 0, 
                                NULL, 0, 
                                NULL, 0, /* <- unexpanded composite step chain, and number */
                                NULL, 
                                current_plan);

                            delete current_plan;
                            current_plan = inst_plan;
                        }
                    }

                    /* Problem lacks solution. */
                    else if (plans[current_flaw_order].empty()) {
                        current_plan = NULL;
                    }

                    else {
                        current_plan = plans[current_flaw_order].top();
                        plans[current_flaw_order].pop();
                    }
                }
            }

            else 
            {   
                if (next_f_limit != std::numeric_limits<float>::infinity()) {
                    current_plan = NULL;
                }

                break;
            }
        }

        if (current_plan != NULL && current_plan->complete()) {
            break;
        }

        f_limit = next_f_limit;
        
        if (f_limit != std::numeric_limits<float>::infinity()) 
        {
            /* Restart search. */
            if (current_plan != NULL && current_plan != initial_plan) {
                delete current_plan;
            }

            current_plan = initial_plan;
        }

    } while (f_limit != std::numeric_limits<float>::infinity());
    // END DO-WHILE


    /* Print statistics if the verbosity level calls for it. */
    if (verbosity > 0) {

        std::cerr << std::endl << "Plans generated: " << num_generated_plans;

        if (num_static > 0) {
            std::cerr << " [" << (num_generated_plans - num_static) << "]";
        }

        std::cerr << std::endl << "Plans visited: " << num_visited_plans;

        if (num_static > 0) {
            std::cerr << " [" << (num_visited_plans - num_static) << "]";
        }

        std::cerr << std::endl << "Dead ends encountered: " << num_dead_ends << std::endl;
    }

    /*
     * Discard the rest of the plan queue and some other things, unless
     * this is the last problem in which case we can save time by just
     * letting the operating system reclaim the memory for us.
     */
    if (!last_problem) {
        if (current_plan != initial_plan) {
            delete initial_plan;
        }
        for (size_t i = 0; i < plans.size(); i++) {
            while (!plans[i].empty()) {
                delete plans[i].top();
                plans[i].pop();
            }
        }
    }
    /* Return last plan, or NULL if problem does not have a solution. */
    return current_plan;
}


/* Cleans up after planning. */
void Plan::cleanup() {
    if (planning_graph != NULL) {
        delete planning_graph;
        planning_graph = NULL;
    }
    if (goal_action != NULL) {
        delete goal_action;
        goal_action = NULL;
    }
}


/* Constructs a plan. */
Plan::Plan(
    const Chain<Step>* steps, size_t num_steps,
    const Chain<Link>* links, size_t num_links,
    const Orderings& orderings, const Bindings& bindings,
    const Chain<DecompositionFrame>* decomposition_frames, size_t num_decomposition_frames,
    const Chain<DecompositionLink>* decomposition_links, size_t num_decomposition_links,
    const Chain<Unsafe>* unsafes, size_t num_unsafes,
    const Chain<OpenCondition>* open_conds, size_t num_open_conds,
    const Chain<UnexpandedCompositeStep>* unexpanded_steps, size_t num_unexpanded_steps,
    const Chain<MutexThreat>* mutex_threats, const Plan* parent)
    :
    steps_(steps), num_steps_(num_steps),
    links_(links), num_links_(num_links),
    orderings_(&orderings), bindings_(&bindings),
    decomposition_frames_(decomposition_frames), num_decomposition_frames_(num_decomposition_frames),
    decomposition_links_(decomposition_links), num_decomposition_links_(num_decomposition_links),
    unsafes_(unsafes), num_unsafes_(num_unsafes),
    open_conds_(open_conds), num_open_conds_(num_open_conds),
    unexpanded_steps_(unexpanded_steps), num_unexpanded_steps_(num_unexpanded_steps),
    mutex_threats_(mutex_threats)
{
    RCObject::ref(steps);
    RCObject::ref(links);
    Orderings::register_use(&orderings);
    Bindings::register_use(&bindings);
    RCObject::ref(decomposition_frames);
    RCObject::ref(decomposition_links);
    RCObject::ref(unsafes);
    RCObject::ref(open_conds);
    RCObject::ref(unexpanded_steps);
    RCObject::ref(mutex_threats);
#ifdef DEBUG_MEMORY
    created_plans++;
#endif
#ifdef DEBUG
    depth_ = (parent != NULL) ? parent->depth() + 1 : 0;
#endif
}


/* Deletes this plan. */
Plan::~Plan() {
#ifdef DEBUG_MEMORY
    deleted_plans++;
#endif
    RCObject::destructive_deref(steps_);
    RCObject::destructive_deref(links_);
    Orderings::unregister_use(orderings_);
    Bindings::unregister_use(bindings_);
    RCObject::destructive_deref(decomposition_frames_);
    RCObject::destructive_deref(decomposition_links_);
    RCObject::destructive_deref(unsafes_);
    RCObject::destructive_deref(open_conds_);
    RCObject::destructive_deref(unexpanded_steps_);
    RCObject::destructive_deref(mutex_threats_);
}


/* Returns the bindings of this plan. */
const Bindings* Plan::bindings() const {
    return params->ground_actions ? NULL : bindings_;
}


/* Checks if this plan is complete. */
bool Plan::complete() const {
    return unsafes() == NULL 
        && open_conds() == NULL 
        && mutex_threats() == NULL 
        && unexpanded_steps() == NULL;
}


/* Returns the primary rank of this plan, where a lower rank
   signifies a better plan. */
float Plan::primary_rank() const {
    if (rank_.empty()) {
        params->heuristic.plan_rank(rank_, *this, params->weight, *domain,
            planning_graph);
    }

    return rank_[0];
}


/* Returns the serial number of this plan. */
size_t Plan::serial_no() const {
    return id_;
}


/* Returns the next flaw to work on. */
const Flaw& Plan::get_flaw(const FlawSelectionOrder& flaw_order) const {
    const Flaw& flaw = flaw_order.select(*this, *problem, planning_graph);
    if (!params->ground_actions) {
        const OpenCondition* open_cond = dynamic_cast<const OpenCondition*>(&flaw);
        static_pred_flaw = (open_cond != NULL && open_cond->is_static());
    }
    return flaw;
}


/* Returns the refinements for the next flaw to work on. */
void Plan::refinements(PlanList& plans, const FlawSelectionOrder& flaw_order) const 
{
    /* Identify the next flaw to work on. */
    const Flaw& flaw = get_flaw(flaw_order);

    if (verbosity > 1) {
        std::cerr << std::endl << "handle ";
        flaw.print(std::cerr, *bindings_);
        std::cerr << std::endl;
    }

    /* Flaw Repair Strategies! */

    // Flaw Type 1: Threatened Causal Link
    const Unsafe* unsafe = dynamic_cast<const Unsafe*>(&flaw);
    if (unsafe != NULL) {
        handle_unsafe(plans, *unsafe);
    }

    else 
    {
        // Flaw Type 2: Open Precondition Flaw
        const OpenCondition* open_cond = dynamic_cast<const OpenCondition*>(&flaw);
        if (open_cond != NULL) {
            handle_open_condition(plans, *open_cond);
        }

        else 
        {
            // Flaw Type 3: Unexpanded Composite Step Flaw
            const UnexpandedCompositeStep* unexpanded = 
                dynamic_cast<const UnexpandedCompositeStep*>(&flaw);

            if (unexpanded != NULL) {
                handle_unexpanded_composite_step(plans, *unexpanded);
            }

            else 
            {
                // Flaw Type 4: Mutex Threat Flaw
                const MutexThreat* mutex_threat = dynamic_cast<const MutexThreat*>(&flaw);
                if (mutex_threat != NULL) {
                    handle_mutex_threat(plans, *mutex_threat);
                }

                else {
                    throw std::logic_error("unknown kind of flaw");
                }
            }            
        }
    }
}


/* Counts the number of refinements for the given threat, and returns
   true iff the number of refinements does not exceed the given
   limit. */
bool Plan::unsafe_refinements(int& refinements, int& separable,
    int& promotable, int& demotable,
    const Unsafe& unsafe, int limit) const {
    if (refinements >= 0) {
        return refinements <= limit;
    }
    else {
        int ref = 0;
        BindingList unifier;
        const Link& link = unsafe.link();
        StepTime lt1 = link.effect_time();
        StepTime lt2 = end_time(link.condition_time());
        StepTime et = end_time(unsafe.effect());
        if (orderings().possibly_not_after(link.from_id(), lt1,
            unsafe.step_id(), et)
            && orderings().possibly_not_before(link.to_id(), lt2,
            unsafe.step_id(), et)
            && bindings_->affects(unifier, unsafe.effect().literal(),
            unsafe.step_id(),
            link.condition(), link.to_id())) {
            PlanList dummy;
            if (separable < 0) {
                separable = separate(dummy, unsafe, unifier, true);
            }
            ref += separable;
            if (ref <= limit) {
                if (promotable < 0) {
                    promotable = promote(dummy, unsafe, true);
                }
                ref += promotable;
                if (ref <= limit) {
                    if (demotable < 0) {
                        demotable = demote(dummy, unsafe, true);
                    }
                    refinements = ref + demotable;
                    return refinements <= limit;
                }
            }
            return false;
        }
        else {
            separable = promotable = demotable = 0;
            refinements = 1;
            return refinements <= limit;
        }
    }
}


/* ====================================================================== */
/* Unsafe Flaw Handling */


/* Handles an unsafe link. */
void Plan::handle_unsafe(PlanList& plans, const Unsafe& unsafe) const {
    BindingList unifier;
    const Link& link = unsafe.link();
    StepTime lt1 = link.effect_time();
    StepTime lt2 = end_time(link.condition_time());
    StepTime et = end_time(unsafe.effect());
    if (orderings().possibly_not_after(link.from_id(), lt1,
        unsafe.step_id(), et)
        && orderings().possibly_not_before(link.to_id(), lt2,
        unsafe.step_id(), et)
        && bindings_->affects(unifier, unsafe.effect().literal(),
        unsafe.step_id(),
        link.condition(), link.to_id())) {
        separate(plans, unsafe, unifier);
        promote(plans, unsafe);
        demote(plans, unsafe);
    }
    else {
        /* bogus flaw */
        plans.push_back(new Plan(steps(), num_steps(), links(), num_links(),
            orderings(), *bindings_,
            decomposition_frames(), num_decomposition_frames(),
            decomposition_links(), num_decomposition_links(),
            unsafes()->remove(unsafe), num_unsafes() - 1,
            open_conds(), num_open_conds(),
            unexpanded_steps(), num_unexpanded_steps(),
            mutex_threats(), this));
    }
}


/* Checks if the given threat is separable. */
int Plan::separable(const Unsafe& unsafe) const {
    BindingList unifier;
    const Link& link = unsafe.link();
    StepTime lt1 = link.effect_time();
    StepTime lt2 = end_time(link.condition_time());
    StepTime et = end_time(unsafe.effect());
    if (orderings().possibly_not_after(link.from_id(), lt1,
        unsafe.step_id(), et)
        && orderings().possibly_not_before(link.to_id(), lt2,
        unsafe.step_id(), et)
        && bindings_->affects(unifier,
        unsafe.effect().literal(),
        unsafe.step_id(),
        link.condition(), link.to_id())) {
        PlanList dummy;
        return separate(dummy, unsafe, unifier, true);
    }
    else {
        return 0;
    }
}


/* Handles an unsafe link through separation. */
int Plan::separate(PlanList& plans, const Unsafe& unsafe,
    const BindingList& unifier, bool test_only) const {
    const Formula* goal = &Formula::FALSE;
    for (BindingList::const_iterator si = unifier.begin();
        si != unifier.end(); si++) {
        const Binding& subst = *si;
        if (!unsafe.effect().quantifies(subst.var())) {
            const Formula& g = Inequality::make(subst.var(), subst.var_id(),
                subst.term(), subst.term_id());
            const Inequality* neq = dynamic_cast<const Inequality*>(&g);
            if (neq == 0 || bindings_->consistent_with(*neq, 0)) {
                goal = &(*goal || g);
            }
            else {
                Formula::register_use(&g);
                Formula::unregister_use(&g);
            }
        }
    }
    const Formula& effect_cond = unsafe.effect().condition();
    if (!effect_cond.tautology()) {
        size_t n = unsafe.effect().arity();
        if (n > 0) {
            Forall* forall = new Forall();
            SubstitutionMap forall_subst;
            for (size_t i = 0; i < n; i++) {
                Variable vi = unsafe.effect().parameter(i);
                Variable v =
                    test_only ? vi : TermTable::add_variable(TermTable::type(vi));
                forall->add_parameter(v);
                if (!test_only) {
                    forall_subst.insert(std::make_pair(vi, v));
                }
            }
            if (test_only) {
                forall->set_body(!effect_cond);
            }
            else {
                forall->set_body(!effect_cond.substitution(forall_subst));
            }
            const Formula* forall_cond;
            if (forall->body().tautology() || forall->body().contradiction()) {
                forall_cond = &forall->body();
                delete forall;
            }
            else {
                forall_cond = forall;
            }
            goal = &(*goal || *forall_cond);
        }
        else {
            goal = &(*goal || !effect_cond);
        }
    }
    const Chain<OpenCondition>* new_open_conds = test_only ? NULL : open_conds();
    size_t new_num_open_conds = test_only ? 0 : num_open_conds();
    BindingList new_bindings;
    bool added = add_goal(new_open_conds, new_num_open_conds, new_bindings,
        *goal, unsafe.step_id(), test_only);
    if (!test_only) {
        RCObject::ref(new_open_conds);
    }
    int count = 0;
    if (added) {
        const Bindings* bindings = bindings_->add(new_bindings, test_only);
        if (bindings != NULL) {
            if (!test_only) {
                const Orderings* new_orderings = orderings_;
                if (!goal->tautology() && planning_graph != NULL) {
                    const TemporalOrderings* to =
                        dynamic_cast<const TemporalOrderings*>(new_orderings);
                    if (to != NULL) {
                        HeuristicValue h, hs;
                        goal->heuristic_value(h, hs, *planning_graph, unsafe.step_id(),
                            params->ground_actions ? NULL : bindings);
                        new_orderings = to->refine(unsafe.step_id(),
                            hs.makespan(), h.makespan());
                    }
                }
                if (new_orderings != NULL) {
                    plans.push_back(new Plan(
                        steps(), num_steps(), 
                        links(), num_links(),
                        *new_orderings, *bindings,
                        decomposition_frames(), num_decomposition_frames(),
                        decomposition_links(), num_decomposition_links(),
                        unsafes()->remove(unsafe),
                        num_unsafes() - 1,
                        new_open_conds, new_num_open_conds,
                        unexpanded_steps(), num_unexpanded_steps(), // NULL, 0, /* TODO: Fix the unexpanded composite step handling */
                        mutex_threats(), this));
                }
                else {
                    Bindings::register_use(bindings);
                    Bindings::unregister_use(bindings);
                }
            }
            count++;
        }
    }
    if (!test_only) {
        RCObject::destructive_deref(new_open_conds);
    }
    Formula::register_use(goal);
    Formula::unregister_use(goal);
    return count;
}


/* Handles an unsafe link through demotion. */
int Plan::demote(PlanList& plans, const Unsafe& unsafe,
    bool test_only) const {
    const Link& link = unsafe.link();
    StepTime lt1 = link.effect_time();
    StepTime et = end_time(unsafe.effect());
    if (orderings().possibly_before(unsafe.step_id(), et, link.from_id(), lt1)) {
        if (!test_only) {
            new_ordering(plans, unsafe.step_id(), et, link.from_id(), lt1, unsafe);
        }
        return 1;
    }
    else {
        return 0;
    }
}


/* Handles an unsafe link through promotion. */
int Plan::promote(PlanList& plans, const Unsafe& unsafe,
    bool test_only) const {
    const Link& link = unsafe.link();
    StepTime lt2 = end_time(link.condition_time());
    StepTime et = end_time(unsafe.effect());
    if (orderings().possibly_before(link.to_id(), lt2, unsafe.step_id(), et)) {
        if (!test_only) {
            new_ordering(plans, link.to_id(), lt2, unsafe.step_id(), et, unsafe);
        }
        return 1;
    }
    else {
        return 0;
    }
}


/* Adds a plan to the given plan list with an ordering added. */
void Plan::new_ordering(PlanList& plans, size_t before_id, StepTime t1,
    size_t after_id, StepTime t2,
    const Unsafe& unsafe) const {
    const Orderings* new_orderings =
        orderings().refine(Ordering(before_id, t1, after_id, t2));
    if (new_orderings != NULL) {
        plans.push_back(
            new Plan(
            steps(), num_steps(), 
            links(), num_links(),
            *new_orderings, *bindings_,
            decomposition_frames(), num_decomposition_frames(),
            decomposition_links(), num_decomposition_links(),
            unsafes()->remove(unsafe), num_unsafes() - 1,
            open_conds(), num_open_conds(),
            unexpanded_steps(), num_unexpanded_steps(),
            mutex_threats(), this)
        );
    }
}



/* ====================================================================== */
/* Mutex Threat Flaw Handling */


/* Handles a mutex threat. */
void Plan::handle_mutex_threat(PlanList& plans,
    const MutexThreat& mutex_threat) const {
    if (mutex_threat.step_id1() == 0) {
        const Chain<MutexThreat>* new_mutex_threats = NULL;
        for (const Chain<Step>* sc = steps(); sc != NULL; sc = sc->tail) {
            const Step& s = sc->head;
            ::mutex_threats(new_mutex_threats, s, steps(), orderings(), *bindings_);
        }
        plans.push_back(new Plan(steps(), num_steps(), links(), num_links(),
            orderings(), *bindings_, 
            decomposition_frames(), num_decomposition_frames(),
            decomposition_links(), num_decomposition_links(),
            unsafes(), num_unsafes(),
            open_conds(), num_open_conds(),
            unexpanded_steps(), num_unexpanded_steps(),
            new_mutex_threats, this));
        return;
    }
    BindingList unifier;
    size_t id1 = mutex_threat.step_id1();
    StepTime et1 = end_time(mutex_threat.effect1());
    size_t id2 = mutex_threat.step_id2();
    StepTime et2 = end_time(mutex_threat.effect2());
    if (orderings().possibly_not_before(id1, et1, id2, et2)
        && orderings().possibly_not_after(id1, et1, id2, et2)
        && bindings_->unify(unifier,
        mutex_threat.effect1().literal().atom(), id1,
        mutex_threat.effect2().literal().atom(), id2)) {
        separate(plans, mutex_threat, unifier);
        promote(plans, mutex_threat);
        demote(plans, mutex_threat);
    }
    else {
        /* bogus flaw */
        plans.push_back(new Plan(steps(), num_steps(), links(), num_links(),
            orderings(), *bindings_, 
            decomposition_frames(), num_decomposition_frames(),
            decomposition_links(), num_decomposition_links(),
            unsafes(), num_unsafes(),
            open_conds(), num_open_conds(),
            unexpanded_steps(), num_unexpanded_steps(),
            mutex_threats()->remove(mutex_threat), this));
    }
}


/* Handles a mutex threat through separation. */
void Plan::separate(PlanList& plans, const MutexThreat& mutex_threat,
    const BindingList& unifier) const {
    if (!unifier.empty()) {
        const Formula* goal = &Formula::FALSE;
        for (BindingList::const_iterator si = unifier.begin();
            si != unifier.end(); si++) {
            const Binding& subst = *si;
            if (!mutex_threat.effect1().quantifies(subst.var())
                && !mutex_threat.effect2().quantifies(subst.var())) {
                const Formula& g = Inequality::make(subst.var(), subst.var_id(),
                    subst.term(), subst.term_id());
                const Inequality* neq = dynamic_cast<const Inequality*>(&g);
                if (neq == 0 || bindings_->consistent_with(*neq, 0)) {
                    goal = &(*goal || g);
                }
                else {
                    Formula::register_use(&g);
                    Formula::unregister_use(&g);
                }
            }
        }
        const Chain<OpenCondition>* new_open_conds = open_conds();
        size_t new_num_open_conds = num_open_conds();
        BindingList new_bindings;
        bool added = add_goal(new_open_conds, new_num_open_conds, new_bindings,
            *goal, 0);
        RCObject::ref(new_open_conds);
        if (added) {
            const Bindings* bindings = bindings_->add(new_bindings);
            if (bindings != NULL) {
                plans.push_back(new Plan(steps(), num_steps(), links(),
                    num_links(), orderings(), *bindings,
                    decomposition_frames(), num_decomposition_frames(),
                    decomposition_links(), num_decomposition_links(),
                    unsafes(), num_unsafes(),
                    new_open_conds, new_num_open_conds,
                    unexpanded_steps(), num_unexpanded_steps(), // NULL, 0, /* TODO: Fix the unexpanded composite step handling */
                    mutex_threats()->remove(mutex_threat), this));
            }
            else {
                Bindings::register_use(bindings);
                Bindings::unregister_use(bindings);
            }
        }
        RCObject::destructive_deref(new_open_conds);
        Formula::register_use(goal);
        Formula::unregister_use(goal);
    }
    for (size_t i = 1; i <= 2; i++) {
        size_t step_id = ((i == 1) ? mutex_threat.step_id1()
            : mutex_threat.step_id2());
        const Effect& effect = ((i == 1) ? mutex_threat.effect1()
            : mutex_threat.effect2());
        const Formula& effect_cond = effect.condition();
        const Formula* goal;
        if (!effect_cond.tautology()) {
            size_t n = effect.arity();
            if (n > 0) {
                Forall* forall = new Forall();
                SubstitutionMap forall_subst;
                for (size_t i = 0; i < n; i++) {
                    Variable vi = effect.parameter(i);
                    Variable v = TermTable::add_variable(TermTable::type(vi));
                    forall->add_parameter(v);
                    forall_subst.insert(std::make_pair(vi, v));
                }
                forall->set_body(!effect_cond.substitution(forall_subst));
                if (forall->body().tautology() || forall->body().contradiction()) {
                    goal = &forall->body();
                    delete forall;
                }
                else {
                    goal = forall;
                }
            }
            else {
                goal = &!effect_cond;
            }
            const Chain<OpenCondition>* new_open_conds = open_conds();
            size_t new_num_open_conds = num_open_conds();
            BindingList new_bindings;
            bool added = add_goal(new_open_conds, new_num_open_conds, new_bindings,
                *goal, step_id);
            RCObject::ref(new_open_conds);
            if (added) {
                const Bindings* bindings = bindings_->add(new_bindings);
                if (bindings != NULL) {
                    const Orderings* new_orderings = orderings_;
                    if (!goal->tautology() && planning_graph != NULL) {
                        const TemporalOrderings* to =
                            dynamic_cast<const TemporalOrderings*>(new_orderings);
                        if (to != NULL) {
                            HeuristicValue h, hs;
                            goal->heuristic_value(h, hs, *planning_graph, step_id,
                                params->ground_actions ? NULL : bindings);
                            new_orderings = to->refine(step_id, hs.makespan(), h.makespan());
                        }
                    }
                    if (new_orderings != NULL) {
                        plans.push_back(new Plan(steps(), num_steps(), links(),
                            num_links(), *new_orderings, *bindings,
                            decomposition_frames(), num_decomposition_frames(),
                            decomposition_links(), num_decomposition_links(),
                            unsafes(), num_unsafes(),
                            new_open_conds, new_num_open_conds,
                            unexpanded_steps(), num_unexpanded_steps(),  // NULL, 0, /* TODO: Fix the unexpanded composite step handling */
                            mutex_threats()->remove(mutex_threat),
                            this));
                    }
                    else {
                        Bindings::register_use(bindings);
                        Bindings::unregister_use(bindings);
                    }
                }
            }
            RCObject::destructive_deref(new_open_conds);
            Formula::register_use(goal);
            Formula::unregister_use(goal);
        }
    }
}


/* Handles a mutex threat through demotion. */
void Plan::demote(PlanList& plans, const MutexThreat& mutex_threat) const {
    size_t id1 = mutex_threat.step_id1();
    StepTime et1 = end_time(mutex_threat.effect1());
    size_t id2 = mutex_threat.step_id2();
    StepTime et2 = end_time(mutex_threat.effect2());
    if (orderings().possibly_before(id1, et1, id2, et2)) {
        new_ordering(plans, id1, et1, id2, et2, mutex_threat);
    }
}


/* Handles a mutex threat through promotion. */
void Plan::promote(PlanList& plans, const MutexThreat& mutex_threat) const {
    size_t id1 = mutex_threat.step_id1();
    StepTime et1 = end_time(mutex_threat.effect1());
    size_t id2 = mutex_threat.step_id2();
    StepTime et2 = end_time(mutex_threat.effect2());
    if (orderings().possibly_before(id2, et2, id1, et1)) {
        new_ordering(plans, id2, et2, id1, et1, mutex_threat);
    }
}


/* Adds a plan to the given plan list with an ordering added. */
void Plan::new_ordering(PlanList& plans, size_t before_id, StepTime t1,
    size_t after_id, StepTime t2,
    const MutexThreat& mutex_threat) const {
    const Orderings* new_orderings =
        orderings().refine(Ordering(before_id, t1, after_id, t2));
    if (new_orderings != NULL) {
        plans.push_back(new Plan(steps(), num_steps(), links(), num_links(),
            *new_orderings, *bindings_,
            decomposition_frames(), num_decomposition_frames(),
            decomposition_links(), num_decomposition_links(),
            unsafes(), num_unsafes(),
            open_conds(), num_open_conds(),
            unexpanded_steps(), num_unexpanded_steps(), // NULL, 0, /* TODO: Fix the unexpanded composite step handling */
            mutex_threats()->remove(mutex_threat), this));
    }
}


/* ====================================================================== */
/* Open Condition Flaw Handling */


/* Checks if the given open condition is threatened. */
bool Plan::unsafe_open_condition(const OpenCondition& open_cond) const {
    const Literal* literal = open_cond.literal();
    if (literal != NULL) {
        const Literal& goal = *literal;
        StepTime gt = end_time(open_cond.when());
        for (const Chain<Step>* sc = steps(); sc != NULL; sc = sc->tail) {
            const Step& s = sc->head;
            if (orderings().possibly_not_before(open_cond.step_id(), gt,
                s.id(), StepTime::AT_START)) {
                const EffectList& effects = s.action().effects();
                for (EffectList::const_iterator ei = effects.begin();
                    ei != effects.end(); ei++) {
                    const Effect& e = **ei;
                    StepTime et = end_time(e);
                    if (orderings().possibly_not_before(open_cond.step_id(), gt,
                        s.id(), et)
                        && bindings_->affects(e.literal(), s.id(),
                        goal, open_cond.step_id())) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}


/* Counts the number of refinements for the given open condition, and
   returns true iff the number of refinements does not exceed the
   given limit. */
bool Plan::open_cond_refinements(int& refinements, int& addable, int& reusable,
    const OpenCondition& open_cond,
    int limit) const {
    if (refinements >= 0) {
        return refinements <= limit;
    }
    else {
        const Literal* literal = open_cond.literal();
        if (literal != NULL) {
            int ref = 0;
            if (addable < 0) {
                if (!addable_steps(addable, *literal, open_cond, limit)) {
                    return false;
                }
            }
            ref += addable;
            if (ref <= limit) {
                if (reusable < 0) {
                    if (!reusable_steps(reusable, *literal, open_cond, limit)) {
                        return false;
                    }
                }
                refinements = ref + reusable;
                return refinements <= limit;
            }
        }
        else {
            PlanList dummy;
            const Disjunction* disj = open_cond.disjunction();
            if (disj != NULL) {
                refinements = handle_disjunction(dummy, *disj, open_cond, true);
                return refinements <= limit;
            }
            else {
                const Inequality* neq = open_cond.inequality();
                if (neq != NULL) {
                    refinements = handle_inequality(dummy, *neq, open_cond, true);
                }
                else {
                    throw std::logic_error("unknown kind of open condition");
                }
            }
        }
    }
    return false;
}


/* Handles an open condition. */
void Plan::handle_open_condition(PlanList& plans, const OpenCondition& open_cond) const 
{
    const Literal* literal = open_cond.literal();

    if (literal != NULL) 
    {
        const ActionEffectMap* achievers = literal_achievers(*literal);
        
        if (achievers != NULL) 
        {
            add_step(plans, *literal, open_cond, *achievers);
            reuse_step(plans, *literal, open_cond, *achievers);
        }

        const Negation* negation = dynamic_cast<const Negation*>(literal); 
        if (negation != NULL) 
        {
            new_cw_link(plans, problem->init_action().effects(), *negation, open_cond);
        }
    }

    else 
    {
        const Disjunction* disj = open_cond.disjunction();
        if (disj != NULL) {
            handle_disjunction(plans, *disj, open_cond);
        }
        
        else 
        {
            const Inequality* neq = open_cond.inequality();
            if (neq != NULL) {
                handle_inequality(plans, *neq, open_cond);
            }
        
            else {
                throw std::logic_error("unknown kind of open condition");
            }
        }
    }
}


/* Handles a disjunctive open condition. */
int Plan::handle_disjunction(PlanList& plans, const Disjunction& disj, const OpenCondition& open_cond,
    bool test_only) const 
{
    int count = 0;
    const FormulaList& disjuncts = disj.disjuncts();
    
    for (FormulaList::const_iterator fi = disjuncts.begin(); fi != disjuncts.end(); fi++) 
    {
        BindingList new_bindings;
        const Chain<OpenCondition>* new_open_conds =
            test_only ? NULL : open_conds()->remove(open_cond);
        
        size_t new_num_open_conds = test_only ? 0 : num_open_conds() - 1;
        
        bool added = add_goal(new_open_conds, new_num_open_conds, new_bindings,
            **fi, open_cond.step_id(), test_only);
        
        if (!test_only) {
            RCObject::ref(new_open_conds);
        }
        
        if (added) 
        {
            const Bindings* bindings = bindings_->add(new_bindings, test_only);
            if (bindings != NULL) 
            {
                if (!test_only) {
                    plans.push_back(new Plan(steps(), num_steps(), links(), num_links(),
                        orderings(), *bindings,
                        decomposition_frames(), num_decomposition_frames(),
                        decomposition_links(), num_decomposition_links(),
                        unsafes(), num_unsafes(),
                        new_open_conds, new_num_open_conds,
                        unexpanded_steps(), num_unexpanded_steps(), 
                        mutex_threats(), this));
                }
                count++;
            }
        }
        if (!test_only) {
            RCObject::destructive_deref(new_open_conds);
        }
    }
    return count;
}


/* Handles inequality open condition. */
int Plan::handle_inequality(PlanList& plans, const Inequality& neq,
    const OpenCondition& open_cond,
    bool test_only) const {
    int count = 0;
    size_t step_id = open_cond.step_id();
    Variable variable2 = neq.term().as_variable();
    const NameSet& d1 = bindings_->domain(neq.variable(), neq.step_id1(step_id),
        *problem);
    const NameSet& d2 = bindings_->domain(variable2, neq.step_id2(step_id),
        *problem);

    /*
     * Branch on the variable with the smallest domain.
     */
    const Variable& var1 = (d1.size() < d2.size()) ? neq.variable() : variable2;
    size_t id1 =
        (d1.size() < d2.size()) ? neq.step_id1(step_id) : neq.step_id2(step_id);
    const Variable& var2 = (d1.size() < d2.size()) ? variable2 : neq.variable();
    size_t id2 =
        (d1.size() < d2.size()) ? neq.step_id2(step_id) : neq.step_id1(step_id);
    const NameSet& var_domain = (d1.size() < d2.size()) ? d1 : d2;
    for (NameSet::const_iterator ni = var_domain.begin();
        ni != var_domain.end(); ni++) {
        Object name = *ni;
        BindingList new_bindings;
        new_bindings.push_back(Binding(var1, id1, name, 0, true));
        new_bindings.push_back(Binding(var2, id2, name, 0, false));
        const Bindings* bindings = bindings_->add(new_bindings, test_only);
        if (bindings != NULL) {
            if (!test_only) {
                plans.push_back(new Plan(steps(), num_steps(), links(), num_links(),
                    orderings(), *bindings,
                    decomposition_frames(), num_decomposition_frames(),
                    decomposition_links(), num_decomposition_links(),
                    unsafes(), num_unsafes(),
                    open_conds()->remove(open_cond), num_open_conds() - 1,
                    unexpanded_steps(), num_unexpanded_steps(), // NULL, 0, /* TODO: Fix the unexpanded composite step handling */
                    mutex_threats(), this));
            }
            count++;
        }
    }
    if (planning_graph == NULL) {
        delete &d1;
        delete &d2;
    }
    return count;
}


/* Counts the number of add-step refinements for the given literal
   open condition, and returns true iff the number of refinements
   does not exceed the given limit. */
bool Plan::addable_steps(int& refinements, const Literal& literal,
    const OpenCondition& open_cond, int limit) const {
    int count = 0;
    PlanList dummy;
    const ActionEffectMap* achievers = literal_achievers(literal);
    if (achievers != NULL) {
        for (ActionEffectMap::const_iterator ai = achievers->begin();
            ai != achievers->end(); ai++) {
            const Action& action = *(*ai).first;
            if (action.name().substr(0, 1) != "<") {
                const Effect& effect = *(*ai).second;
                count += new_link(dummy, Step(num_steps() + 1, action), effect,
                    literal, open_cond, true);
                if (count > limit) {
                    return false;
                }
            }
        }
    }
    refinements = count;
    return count <= limit;
}


/* Handles a literal open condition by adding a new step. */
void Plan::add_step(PlanList& plans, const Literal& literal, const OpenCondition& open_cond, 
    const ActionEffectMap& achievers) const 
{
    // For every action that could achieve the open condition,
    for (ActionEffectMap::const_iterator ai = achievers.begin(); ai != achievers.end();  ai++) 
    {
        // Get the action
        const Action& action = *(*ai).first;
        
        // Check that it's not a dummy action, which by convention begins with "<"
        if (action.name().substr(0, 1) != "<") 
        {
            // Get the effect of the action needed to satisfy the open condition,
            const Effect& effect = *(*ai).second;



            // Add a new causal link
            new_link(plans, Step(num_steps() + 1, action), effect, literal, open_cond);
        }
    }
}


/* Counts the number of reuse-step refinements for the given literal
   open condition, and returns true iff the number of refinements
   does not exceed the given limit. */
bool Plan::reusable_steps(int& refinements, const Literal& literal,
    const OpenCondition& open_cond, int limit) const {
    int count = 0;
    PlanList dummy;
    const ActionEffectMap* achievers = literal_achievers(literal);
    if (achievers != NULL) {
        StepTime gt = start_time(open_cond.when());
        for (const Chain<Step>* sc = steps(); sc != NULL; sc = sc->tail) {
            const Step& step = sc->head;
            if (orderings().possibly_before(step.id(), StepTime::AT_START,
                open_cond.step_id(), gt)) {
                std::pair<ActionEffectMap::const_iterator,
                    ActionEffectMap::const_iterator> b =
                    achievers->equal_range(&step.action());
                for (ActionEffectMap::const_iterator ei = b.first;
                    ei != b.second; ei++) {
                    const Effect& effect = *(*ei).second;
                    StepTime et = end_time(effect);
                    if (orderings().possibly_before(step.id(), et,
                        open_cond.step_id(), gt)) {
                        count += new_link(dummy, step, effect, literal, open_cond, true);
                        if (count > limit) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    const Negation* negation = dynamic_cast<const Negation*>(&literal);
    if (negation != NULL) {
        count += new_cw_link(dummy, problem->init_action().effects(),
            *negation, open_cond, true);
    }
    refinements = count;
    return count <= limit;
}


/* Handles a literal open condition by reusing an existing step. */
void Plan::reuse_step(PlanList& plans, const Literal& literal,
    const OpenCondition& open_cond,
    const ActionEffectMap& achievers) const {
    StepTime gt = start_time(open_cond.when());
    for (const Chain<Step>* sc = steps(); sc != NULL; sc = sc->tail) {
        const Step& step = sc->head;
        if (orderings().possibly_before(step.id(), StepTime::AT_START,
            open_cond.step_id(), gt)) {
            std::pair<ActionEffectMap::const_iterator,
                ActionEffectMap::const_iterator> b =
                achievers.equal_range(&step.action());
            for (ActionEffectMap::const_iterator ei = b.first;
                ei != b.second; ei++) {
                const Effect& effect = *(*ei).second;
                StepTime et = end_time(effect);
                if (orderings().possibly_before(step.id(), et,
                    open_cond.step_id(), gt)) {
                    new_link(plans, step, effect, literal, open_cond);
                }
            }
        }
    }
}


/* Adds plans to the given plan list with a link from the given step
   to the given open condition added. */
int Plan::new_link(
    PlanList& plans, 
    const Step& step, const Effect& effect, 
    const Literal& literal, const OpenCondition& open_cond, 
    bool test_only) const 
{
    BindingList mgu;

    // If we can find consistent bindings that unify the effect of the step and the open literal,
    if (bindings_->unify(mgu, effect.literal(), step.id(), literal, open_cond.step_id())) 
    {
        // Create the link with the new set of bindings
        return make_link(plans, step, effect, literal, open_cond, mgu, test_only);
    }
    
    else  {
        return 0;
    }
}


/* Adds plans to the given plan list with a link from the given step
   to the given open condition added using the closed world
   assumption. */
int Plan::new_cw_link(PlanList& plans, const EffectList& effects,
    const Negation& negation, const OpenCondition& open_cond,
    bool test_only) const {
    const Atom& goal = negation.atom();
    const Formula* goals = &Formula::TRUE;
    for (EffectList::const_iterator ei = effects.begin();
        ei != effects.end(); ei++) {
        const Effect& effect = **ei;
        BindingList mgu;
        if (bindings_->unify(mgu, effect.literal(), 0,
            goal, open_cond.step_id())) {
            if (mgu.empty()) {
                /* Impossible to separate goal and initial condition. */
                return 0;
            }
            const Formula* binds = &Formula::FALSE;
            for (BindingList::const_iterator si = mgu.begin();
                si != mgu.end(); si++) {
                const Binding& subst = *si;
                binds = &(*binds || Inequality::make(subst.var(), subst.var_id(),
                    subst.term(), subst.term_id()));
            }
            goals = &(*goals && *binds);
        }
    }
    BindingList new_bindings;
    const Chain<OpenCondition>* new_open_conds =
        test_only ? NULL : open_conds()->remove(open_cond);
    size_t new_num_open_conds = test_only ? 0 : num_open_conds() - 1;
    bool added = add_goal(new_open_conds, new_num_open_conds, new_bindings,
        *goals, 0, test_only);
    Formula::register_use(goals);
    Formula::unregister_use(goals);
    if (!test_only) {
        RCObject::ref(new_open_conds);
    }
    int count = 0;
    if (added) {
        const Bindings* bindings = bindings_->add(new_bindings, test_only);
        if (bindings != NULL) {
            if (!test_only) {
                const Chain<Unsafe>* new_unsafes = unsafes();
                size_t new_num_unsafes = num_unsafes();
                
                const Chain<Link>* new_links =
                    new Chain<Link>(Link(0, StepTime::AT_END, open_cond), links());
                
                link_threats(new_unsafes, new_num_unsafes, new_links->head, steps(), orderings(), *bindings);

                plans.push_back(new Plan(steps(), num_steps(),
                    new_links, num_links() + 1,
                    orderings(), *bindings,
                    decomposition_frames(), num_decomposition_frames(),
                    decomposition_links(), num_decomposition_links(), // TODO: Fix unexpanded composite step handling
                    new_unsafes, new_num_unsafes,
                    new_open_conds, new_num_open_conds,
                    unexpanded_steps(), num_unexpanded_steps(), // NULL, 0, // TODO: Fix the unexpanded composite step handling 
                    mutex_threats(), this));
            }
            count++;
        }
    }
    if (!test_only) {
        RCObject::destructive_deref(new_open_conds);
    }
    return count;
}


/* Returns a plan with a link added from the given effect to the
   given open condition. */
int Plan::make_link(
    PlanList& plans, 
    const Step& step, const Effect& effect,
    const Literal& literal, const OpenCondition& open_cond,
    const BindingList& unifier, 
    bool test_only) const 
{
    
    // Add bindings needed to unify effect and goal.
    BindingList new_bindings;
    SubstitutionMap forall_subst;
    if (test_only) {
        new_bindings = unifier;
    }
    else {
        for (BindingList::const_iterator si = unifier.begin();
            si != unifier.end(); si++) {
            const Binding& subst = *si;
            if (effect.quantifies(subst.var())) {
                Variable v = TermTable::add_variable(TermTable::type(subst.var()));
                forall_subst.insert(std::make_pair(subst.var(), v));
                new_bindings.push_back(Binding(v, subst.var_id(),
                    subst.term(), subst.term_id(), true));
            }
            else {
                new_bindings.push_back(subst);
            }
        }
    }

    // If the effect is conditional, add condition as goal.
    const Chain<OpenCondition>* new_open_conds =
        test_only ? NULL : open_conds()->remove(open_cond);
    size_t new_num_open_conds = test_only ? 0 : num_open_conds() - 1;
    const Formula* cond_goal = &(effect.condition() && effect.link_condition());
    if (!cond_goal->tautology()) 
    {
        if (!test_only) 
        {
            size_t n = effect.arity();
            if (n > 0) 
            {
                for (size_t i = 0; i < n; i++) 
                {
                    Variable vi = effect.parameter(i);
                    if (forall_subst.find(vi) == forall_subst.end()) 
                    {
                        Variable v = TermTable::add_variable(TermTable::type(vi));
                        forall_subst.insert(std::make_pair(vi, v));
                    }
                }

                const Formula* old_cond_goal = cond_goal;
                cond_goal = &cond_goal->substitution(forall_subst);
                
                if (old_cond_goal != cond_goal) 
                {
                    Formula::register_use(old_cond_goal);
                    Formula::unregister_use(old_cond_goal);
                }
            }
        }

        bool added = add_goal(new_open_conds, new_num_open_conds, new_bindings,
            *cond_goal, step.id(), test_only);
        
        Formula::register_use(cond_goal);
        Formula::unregister_use(cond_goal);
        
        if (!added) 
        {
            if (!test_only) 
            {
                RCObject::ref(new_open_conds);
                RCObject::destructive_deref(new_open_conds);
            }

            return 0;
        }
    }

    // See if this is a new step.
    const Bindings* bindings = bindings_;
    const Chain<Step>* new_steps = test_only ? NULL : steps();
    int new_num_steps = test_only ? 0 : num_steps();
    if (step.id() > num_steps()) 
    {
        if (!add_goal(new_open_conds, new_num_open_conds, new_bindings, 
                step.action().condition(), step.id(), test_only)) 
        {
            if (!test_only) 
            {
                RCObject::ref(new_open_conds);
                RCObject::destructive_deref(new_open_conds);
            }

            return 0;
        }

        if (params->domain_constraints) 
        {
            bindings = bindings->add(step.id(), step.action(), *planning_graph);
            
            if (bindings == NULL) 
            {
                if (!test_only) 
                {
                    RCObject::ref(new_open_conds);
                    RCObject::destructive_deref(new_open_conds);
                }

                return 0;
            }
        }

        if (!test_only) 
        {
            new_steps = new Chain<Step>(step, new_steps);
            new_num_steps++;
        }
    }

    const Bindings* tmp_bindings = bindings->add(new_bindings, test_only);
    
    if ((test_only || tmp_bindings != bindings) && bindings != bindings_) {
        delete bindings;
    }

    if (tmp_bindings == NULL) 
    {
        if (!test_only) {
            RCObject::ref(new_open_conds);
            RCObject::destructive_deref(new_open_conds);
            RCObject::ref(new_steps);
            RCObject::destructive_deref(new_steps);
            return 0;
        }
    }

    if (!test_only) 
    {
        bindings = tmp_bindings;
        StepTime et = end_time(effect);
        StepTime gt = start_time(open_cond.when());
        
        const Orderings* new_orderings =
            orderings().refine(Ordering(step.id(), et, open_cond.step_id(), gt),
                                step, planning_graph,
            params->ground_actions ? NULL : bindings);

        if (new_orderings != NULL && !cond_goal->tautology() && planning_graph != NULL) 
        {
            const TemporalOrderings* to = dynamic_cast<const TemporalOrderings*>(new_orderings);
            
            if (to != NULL) {
                HeuristicValue h, hs;
                cond_goal->heuristic_value(h, hs, *planning_graph, step.id(), params->ground_actions ? NULL : bindings);
                const Orderings* tmp_orderings = to->refine(step.id(), hs.makespan(), h.makespan());
                
                if (tmp_orderings != new_orderings) {
                    delete new_orderings;
                    new_orderings = tmp_orderings;
                }
            }
        }

        if (new_orderings == NULL) 
        {
            if (bindings != bindings_) {
                delete bindings;
            }
        
            RCObject::ref(new_open_conds);
            RCObject::destructive_deref(new_open_conds);
            RCObject::ref(new_steps);
            RCObject::destructive_deref(new_steps);
            return 0;
        }

        // Add a new link.
        const Chain<Link>* new_links =
            new Chain<Link>(Link(step.id(), end_time(effect), open_cond), links());

        // Find any threats to the newly established link.
        const Chain<Unsafe>* new_unsafes = unsafes();
        size_t new_num_unsafes = num_unsafes();
        link_threats(new_unsafes, new_num_unsafes, new_links->head, new_steps, *new_orderings, *bindings);

        // If this is a new step, find links it threatens.
        const Chain<MutexThreat>* new_mutex_threats = mutex_threats();
        if (step.id() > num_steps()) {
            step_threats(new_unsafes, new_num_unsafes, step, links(), *new_orderings, *bindings);
        }

        // If this is a new composite step, register an unexpanded composite step flaw
        const Chain<UnexpandedCompositeStep>* new_unexpanded_steps = nullptr;
        size_t new_num_unexpanded_steps = num_unexpanded_steps();
        if (step.id() > num_steps()) 
        {
            if (step.action().composite())
            {
                new_unexpanded_steps = new Chain<UnexpandedCompositeStep>(UnexpandedCompositeStep(&step), unexpanded_steps());
                new_num_unexpanded_steps++;
            }

            else
            {
                new_unexpanded_steps = unexpanded_steps();
            }
        }

        // Adds the new plan.
        plans.push_back(new Plan(
            new_steps, new_num_steps, 
            new_links, num_links() + 1, 
            *new_orderings, *bindings,
            decomposition_frames(), num_decomposition_frames(),
            decomposition_links(), num_decomposition_links(), 
            new_unsafes, new_num_unsafes,
            new_open_conds, new_num_open_conds,
            new_unexpanded_steps, new_num_unexpanded_steps,
            new_mutex_threats, 
            this));
    }

    return 1;
}


/* ====================================================================== */
/* Unexpanded Composite Step Flaw Handling */

/* Counts the number of refinements for the given unexpanded step, and returns true iff
   the number of refinements does not exceed the given limit. */
bool Plan::unexpanded_step_refinements(int& refinements,
    int& expandable,
    const UnexpandedCompositeStep& unexpanded_step, int limit) const
{
    // TODO
    return false;
}


/* Handles an unexpanded composite step. */
void Plan::handle_unexpanded_composite_step(PlanList& plans, const UnexpandedCompositeStep& unexpanded) const
{
    // Create a new plan that resolves the unexpanded composite step.

    // Find every applicable decomposition 
    const Action* composite_action = &(unexpanded.step_action());
    CompositeExpansionsRange decomposition_range = achieves_composite.equal_range(composite_action);

    // If we found at least one element, process it.
    // Otherwise, there's nothing to do.
    if (decomposition_range.first != decomposition_range.second)
    {
        // Iterate over all applicable decompositions
        CompositeActionAchieverMap::const_iterator di;
        for (di = decomposition_range.first; di != decomposition_range.second; ++di)
        {
            // At this point, we're dealing with one applicable decomposition.
            // Each decomposition leads to the creation of another plan on the fringe.
            const Decomposition* applicable_decomposition = (*di).second;
            add_decomposition_frame(plans, unexpanded, applicable_decomposition);
        }
    }
}


/* Handles an unexpanded composite step by adding a new decomposition frame. */
int Plan::add_decomposition_frame(PlanList& plans, const UnexpandedCompositeStep& unexpanded,
    const Decomposition* expansion) const
{
    // Expanding a composite step through a decomposition involves the expansion of 
    // several plan-related chains. Here, I store references to non-decomposition
    // related plan constructs.

    // We're adding more steps
    int new_num_steps = num_steps();
    const Chain<Step>* new_steps = steps();

    // We're adding new causal links
    int new_num_links = num_links();
    const Chain<Link>* new_links = links();

    // We're refining bindings
    const Bindings* new_bindings = bindings_;

    // We're refining orderings
    const Orderings* new_orderings = orderings_;

    // We can also potentially add several new flaws    
    size_t new_num_open_conds = num_open_conds();
    const Chain<OpenCondition>* new_open_conds = open_conds();

    size_t new_num_unsafes = num_unsafes();
    const Chain<Unsafe>* new_unsafes = unsafes();

    size_t new_num_unexpanded_steps = num_unexpanded_steps();
    const Chain<UnexpandedCompositeStep>* new_unexpanded_steps = unexpanded_steps();

    const Chain<MutexThreat>* new_mutex_threats = mutex_threats();

    // --------------------------------------------------------------------------------------------
    // Instantiate the Decomposition
    DecompositionFrame instance(*expansion);

    // Create a decomposition link from composite step id to decomposition step dummy initial and final steps
    const Chain<DecompositionLink>* new_decomposition_links = decomposition_links();
    new_decomposition_links = new Chain<DecompositionLink>(DecompositionLink(unexpanded.step_id(), instance), new_decomposition_links);
    size_t new_num_decomposition_links = num_decomposition_links() + 1;

    // Create a new Decomposition Frame chain
    const Chain<DecompositionFrame>* new_decomposition_frames = decomposition_frames();
    new_decomposition_frames = new Chain<DecompositionFrame>(instance, new_decomposition_frames);
    int new_num_decomposition_frames = num_decomposition_frames() + 1;

    // --------------------------------------------------------------------------------------------
    // Steps


    // The are two primary options to add steps:
    // 1. Attempt to re-use existing plan steps.
    // We're ignoring this option for now. See issue [#11]. TODO


    // 2. Instantiate all pseudo-steps as wholly new steps.
    // For each pseudo-step, create a new Step.
    for (std::vector<Step>::size_type si = 0; si < instance.steps().size(); ++si)
    {
        Step pseudo_step = instance.steps()[si];
        Step new_step = Step(num_steps() + 1 + si, pseudo_step.action());
        instance.swap_steps(pseudo_step, new_step); // replace and update references
        new_steps = new Chain<Step>(new_step, new_steps);
        new_num_steps++;

        // If we're adding an unexpanded composite step, we need to register it as a flaw.
        if (new_step.action().composite()) {
            new_unexpanded_steps = new Chain<UnexpandedCompositeStep>(UnexpandedCompositeStep(&new_step), new_unexpanded_steps);
            new_num_unexpanded_steps++;
        }

        // Detect and register OpenCondition flaws.
        BindingList open_condition_bindings;

        bool goal_is_consistent = add_goal(new_open_conds, new_num_open_conds, open_condition_bindings,
            new_step.action().condition(), new_step.id(), false);

        if (!goal_is_consistent) {
            goto fail;
        }

        
        // Attempt to add bindings to new bindings
        new_bindings = new_bindings->add(open_condition_bindings, false);

        if (new_bindings == NULL) {
            goto fail;
        }


    }

    // --------------------------------------------------------------------------------------------
    // Bindings

    // Update bindings with bindings taken from the decomposition itself.
    new_bindings = new_bindings->add(instance.binding_list(), false);

    if (new_bindings == NULL) {
        goto fail; // If the bindings are inconsistent, fail! 
    }

    // --------------------------------------------------------------------------------------------
    // Orderings
    {
        // N.b.: I introduced a scope here to be able to use 'goto' without the compiler giving
        // the error: "initialization is skipped by goto"

        // I. Attempt to add causal-link related orderings for steps.
        // First, add orderings for dummy goal step.

        // Find all the steps the unexpanded composite step contributes to in the current plan.
        std::vector<int> step_ids_parent_contributes_to;
        for (const Chain<Link>* ci = links(); ci != 0; ci = ci->tail)
        {
            const Link link = ci->head;
            if (link.from_id() == unexpanded.step_id()) {
                step_ids_parent_contributes_to.push_back(link.to_id());
            }
        }

        // Order dummy goal prior to all steps the parent contributes to.
        const Step dummy_goal_step = instance.steps()[0];
        for (std::vector<int>::size_type i = 0; i < step_ids_parent_contributes_to.size(); ++i)
        {
            int id = step_ids_parent_contributes_to[i];
            const Orderings* tmp_orderings = (*new_orderings).refine(
                Ordering(instance.dummy_final_step_id(), StepTime::AT_END, id, StepTime::AT_START),
                dummy_goal_step,
                planning_graph,
                params->ground_actions ? NULL : new_bindings
                );

            if (tmp_orderings == NULL) {
                goto fail;
            }

            else if (tmp_orderings != new_orderings) // needed because .refine() could return the same thing
            {
                delete new_orderings;
                new_orderings = tmp_orderings;
            }

        }

        // Second, add orderings for steps in order of causal link ancestry.
        for (StepList::size_type si = 0; si < instance.steps().size(); ++si)
        {
            // Get the step
            Step step = instance.steps()[si];

            // Find this step's incoming links
            LinkList incoming_links = instance.link_list().incoming_links(step.id());

            // For each incoming link:
            for (LinkList::size_type li = 0; li < incoming_links.size(); ++li)
            {
                // Get the step's ancestor:
                Link incoming_link = incoming_links[li];
                int ancestor_id = incoming_link.from_id();
                StepList::size_type ancestor_index = (ancestor_id - dummy_goal_step.id());
                Step ancestor_step = instance.steps()[ancestor_index];

                // Create an ordering that places the ancestor before the step
                const Orderings* tmp_orderings = (*new_orderings).refine(
                    Ordering(ancestor_id, StepTime::AT_END, step.id(), StepTime::AT_START),
                    ancestor_step,
                    planning_graph,
                    params->ground_actions ? NULL : new_bindings
                    );

                if (tmp_orderings == NULL) {
                    goto fail;
                }

                else if (tmp_orderings != new_orderings) // needed because .refine() could return the same thing
                {
                    delete new_orderings;
                    new_orderings = tmp_orderings;
                }
            }
        }

        // II. Add other extra orderings that were explicitly stated in the decomposition.
        for (OrderingList::size_type oi = 0; oi < instance.ordering_list().size(); ++oi)
        {
            Ordering ordering = instance.ordering_list()[oi];
            const Orderings* tmp_orderings = (*new_orderings).refine(ordering);

            if (tmp_orderings == NULL) {
                goto fail;
            }

            // needed because .refine() could return the same thing
            else if (tmp_orderings != new_orderings) // needed because .refine() could return the same thing
            {
                delete new_orderings;
                new_orderings = tmp_orderings;
            }
        }
    }

    // --------------------------------------------------------------------------------------------
    // Links

    for (LinkList::size_type li = 0; li < instance.link_list().size(); ++li)
    {
        Link link = instance.link_list()[li];
        new_links = new Chain<Link>(link, new_links);

        // Detect and register Unsafe flaws: threats to this new link.
        // TODO

        new_num_links++;
    }



    // --------------------------------------------------------------------------------------------
    // Links


    // --------------------------------------------------------------------------------------------
    // Flaws

    // Detection and registration of OpenCondition, Unsafe, and UnexpandedCompositeStep flaws have
    // already been done.  Here, detect and register MutexThreats.
    // TODO

    // Before finishing, remove the unexpanded composite step flaw.
    new_unexpanded_steps = new_unexpanded_steps->remove(unexpanded);
    new_num_unexpanded_steps = new_num_unexpanded_steps - 1;

    plans.push_back(new Plan(
        new_steps, new_num_steps,
        new_links, new_num_links,
        *new_orderings, *new_bindings,
        new_decomposition_frames, new_num_decomposition_frames,
        new_decomposition_links, new_num_decomposition_links,
        new_unsafes, new_num_unsafes,
        new_open_conds, new_num_open_conds,
        new_unexpanded_steps, new_num_unexpanded_steps,
        mutex_threats(),
        this));

    return 0;


//    Do cleanup of new Chains.
fail:

    // Bindings and Orderings
    delete new_bindings;
    delete new_orderings;

    // Steps
    RCObject::suggest_cleanup(new_steps);
    
    // Links
    RCObject::suggest_cleanup(new_links);

    // Decomposition Links
    RCObject::suggest_cleanup(new_decomposition_links);
   
    // Decomposition Frames
    RCObject::suggest_cleanup(new_decomposition_frames);
    
    // Flaws
    RCObject::suggest_cleanup(new_open_conds);
    RCObject::suggest_cleanup(new_unsafes);
    RCObject::suggest_cleanup(new_mutex_threats);
    RCObject::suggest_cleanup(new_unexpanded_steps);

    return errno;
}























/* Less than operator for plans. */
bool operator<(const Plan& p1, const Plan& p2) {
    float diff = p1.primary_rank() - p2.primary_rank();
    for (size_t i = 1; i < p1.rank_.size() && diff == 0.0; i++) {
        diff = p1.rank_[i] - p2.rank_[i];
    }
    return diff > 0.0;
}


/*
 * Sorting of steps based on distance from initial conditions.
 */
struct StepSorter {
    StepSorter(std::map<size_t, float>& dist)
        : dist(dist) {}

    bool operator()(const Step* s1, const Step* s2) const {
        return dist[s1->id()] < dist[s2->id()];
    }

    std::map<size_t, float>& dist;
};

#if 0
/* Find interfering steps. */
static const Orderings&
disable_interference(const std::vector<const Step*>& ordered_steps,
const Chain<Link>* links,
const BinaryOrderings& orderings,
const Bindings& bindings) {
    const BinaryOrderings* new_orderings = &orderings;
    size_t n = ordered_steps.size();
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; ) {
            const Step& si = *ordered_steps[i];
            const Step& sj = *ordered_steps[j];
            if (!new_orderings->possibly_concurrent(si.id(), sj.id())) {
                j = n;
            } else {
                bool interference = false;
                for (const Chain<Link>* lc = links;
                    lc != NULL && !interference; lc = lc->tail) {
                    const Link& l = lc->head;
                    if (l.to_id() == sj.id()) {
                        // is effect of si interfering with link condition?
                        const EffectList& effects = si.action().effects();
                        for (EffectList::const_iterator ei = effects.begin();
                            ei != effects.end() && !interference; ei++) {
                            const Effect& e = **ei;
                            if (e.link_condition().contradiction()) {
                                // effect could interfere with condition
                                if (bindings.affects(e.literal(), si.id(),
                                    l.condition(), l.to_id())) {
                                    interference = true;
                                }
                            }
                        }
                    } else if (l.to_id() == si.id()) {
                        // is effect of sj interfering with link condition?
                        const EffectList& effects = sj.action().effects();
                        for (EffectList::const_iterator ei = effects.begin();
                            ei != effects.end() && !interference; ei++) {
                            const Effect& e = **ei;
                            if (e.link_condition().contradiction()) {
                                // effect could interfere with condition
                                if (bindings.affects(e.literal(), sj.id(),
                                    l.condition(), l.to_id())) {
                                    interference = true;
                                }
                            }
                        }
                    }
                }
                if (interference) {
                    if (verbosity > 1) {
                        std::cerr << si.id() << " and " << sj.id() << " interfering"
                            << std::endl;
                    }
                    const Orderings* old_orderings = new_orderings;
                    new_orderings = new_orderings->refine(Ordering(si.id(),
                        StepTime::AT_START,
                        sj.id(),
                        StepTime::AT_START));
                    if (old_orderings != new_orderings) {
                        Orderings::register_use(old_orderings);
                        Orderings::unregister_use(old_orderings);
                    }
                }
                j++;
            }
        }
    }
    return *new_orderings;
}
#endif

/* Output operator for plans. */
std::ostream& operator<<(std::ostream& os, const Plan& p) {
    const Step* init = NULL;
    const Step* goal = NULL;
    const Bindings* bindings = p.bindings_;
    std::vector<const Step*> ordered_steps;
    for (const Chain<Step>* sc = p.steps(); sc != NULL; sc = sc->tail) {
        const Step& step = sc->head;
        if (step.id() == 0) {
            init = &step;
        }
        else if (step.id() == Plan::GOAL_ID) {
            goal = &step;
        }
        else {
            ordered_steps.push_back(&step);
        }
    }
    std::map<size_t, float> start_times;
    std::map<size_t, float> end_times;
    float makespan = p.orderings().schedule(start_times, end_times);
    sort(ordered_steps.begin(), ordered_steps.end(), StepSorter(start_times));
#if 0
    /*
     * Now make sure that nothing scheduled at the same time is
     * interfering.  This can only occur if there are link conditions on
     * effects, and we can always resolve the threats by arbitrarily
     * ordering the interfering steps.  Note, however, that this is not
     * necessarily the if there are durative actions.  Because of this
     * we never add link conditions when the `:durative-actions'
     * requirement flag is present in the domain description, so we
     * don't have to worry about it here.
     */
    const BinaryOrderings* orderings =
        dynamic_cast<const BinaryOrderings*>(&p.orderings());
    if (p.complete() && orderings != NULL) {
        const Orderings& new_orderings =
            disable_interference(ordered_steps, p.links(),
            *orderings, *p.bindings_);
        if (&new_orderings != &p.orderings()) {
            start_times.clear();
            end_times.clear();
            makespan = new_orderings.schedule(start_times, end_times);
            sort(ordered_steps.begin(), ordered_steps.end(),
                StepSorter(start_times));
            Orderings::register_use(&new_orderings);
            Orderings::unregister_use(&new_orderings);
        }
    }
#endif
    if (verbosity < 2) {
        std::cerr << "Makespan: " << makespan << std::endl;
        bool first = true;
        for (std::vector<const Step*>::const_iterator si = ordered_steps.begin();
            si != ordered_steps.end(); si++) {
            const Step& s = **si;
            if (s.action().name().substr(0, 1) != "<") {
                if (verbosity > 0 || !first) {
                    os << std::endl;
                }
                first = false;
                os << start_times[s.id()] << ':';
                s.action().print(os, s.id(), *bindings);
                if (s.action().durative()) {
                    os << '[' << (end_times[s.id()] - start_times[s.id()]) << ']';
                }
            }
        }
    }
    else {
        os << "Initial  :";
        const EffectList& effects = init->action().effects();
        for (EffectList::const_iterator ei = effects.begin();
            ei != effects.end(); ei++) {
            os << ' ';
            (*ei)->literal().print(os, 0, *bindings);
        }
        ordered_steps.push_back(goal);
        for (std::vector<const Step*>::const_iterator si = ordered_steps.begin();
            si != ordered_steps.end(); si++) {
            const Step& step = **si;
            if (step.id() == Plan::GOAL_ID) {
                os << std::endl << std::endl << "Goal     : ";
            }
            else {
                os << std::endl << std::endl << "Step " << step.id();
                if (step.id() < 100) {
                    if (step.id() < 10) {
                        os << ' ';
                    }
                    os << ' ';
                }
                os << " : ";
                step.action().print(os, step.id(), *bindings);
                for (const Chain<MutexThreat>* mc = p.mutex_threats();
                    mc != NULL; mc = mc->tail) {
                    const MutexThreat& mt = mc->head;
                    if (mt.step_id1() == step.id()) {
                        os << " <" << mt.step_id2() << '>';
                    }
                    else if (mt.step_id2() == step.id()) {
                        os << " <" << mt.step_id1() << '>';
                    }
                }
            }
            for (const Chain<Link>* lc = p.links(); lc != NULL; lc = lc->tail) {
                const Link& link = lc->head;
                if (link.to_id() == step.id()) {
                    os << std::endl << "          " << link.from_id();
                    if (link.from_id() < 100) {
                        if (link.from_id() < 10) {
                            os << ' ';
                        }
                        os << ' ';
                    }
                    os << " -> ";
                    link.condition().print(os, link.to_id(), *bindings);
                    for (const Chain<Unsafe>* uc = p.unsafes();
                        uc != NULL; uc = uc->tail) {
                        const Unsafe& unsafe = uc->head;
                        if (unsafe.link() == link) {
                            os << " <" << unsafe.step_id() << '>';
                        }
                    }
                }
            }
            for (const Chain<OpenCondition>* occ = p.open_conds();
                occ != NULL; occ = occ->tail) {
                const OpenCondition& open_cond = occ->head;
                if (open_cond.step_id() == step.id()) {
                    os << std::endl << "           ?? -> ";
                    open_cond.condition().print(os, open_cond.step_id(), *bindings);
                }
            }
        }
        os << std::endl << "orderings = " << p.orderings();
        if (p.bindings() != NULL) {
            os << std::endl << "bindings = ";
            bindings->print(os);
        }
    }
    return os;
}
