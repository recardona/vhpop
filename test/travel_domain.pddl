(define (domain travel)
    (:requirements :strips)
    (:types  plane car - transport
	         person transport place - object)
	(:constants rental - car
				rdu lax - place
	)
    (:predicates
        (at			?thing - object		?place - place)
        (in			?person - person	?vehicle - transport)
        (has		?person - person	?tickets))
		
	;; A person travels from one place to another using some transport. 
	(:action travel
        :parameters		(?person - person ?from - place ?to - place ?means - transport)

        :precondition	(at ?person ?from)
        
		:effect			(and	(not (at ?person ?from))
								(at ?person ?to))

		:composite		t)

	;; A person gets into a car at a place.
	(:action get-in-car
        :parameters		(?person - person ?car - car ?place - place)

        :precondition	(and	(at ?person ?place)
								(at ?car ?place))

        :effect			(and	(not (at ?person ?place))
								(in ?person ?car)))
   
   	;; A person drives a car from one place to another place.
    (:action drive
        :parameters		(?person - person ?car - car ?from - place ?to - place)
        
        :precondition	(and	(at ?car ?from)
								(in ?person ?car))
        :effect			(and	(not (at ?car ?from))
								(at ?car ?to)))
     
	;; A person gets out of a car at a place.
    (:action get-out-of-car
        :parameters		(?person - person ?car - car ?place - place)
        
        :precondition	(and	(at ?car ?place)
								(in ?person ?car))

        :effect			(and	(not (in ?person ?car))
								(at ?person ?place)))


	;; Potential travel decomposition: driving
	;; One way to travel from one location to another is to drive there.
    (:decomposition travel
        :name drive
        :parameters		(?person - person ?from - place ?to - place ?car - car)
        :steps			(
						 (step1 (get-in-car ?person rental rdu))
						 (step2 (drive ?person ?car ?from ?to))
						 (step3 (get-out-of-car ?person ?car ?from))
						)

        :links		    (
		                 (init  (at ?person rdu)  step1) ;; -1 to -3
						 (step1 (in ?person ?car) step2) ;; -3 to -4
						 (step1 (in ?person ?car) step3) ;; -3 to -5
						 (step2 (at ?car ?to)     step3) ;; -4 to -5
						 (step3 (at ?person ?to)   goal) ;; -5 to -2
						)

		:orderings		( 
						 (step1 step2)
						)
	)

)