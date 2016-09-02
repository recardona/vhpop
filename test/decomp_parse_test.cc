#include "stdafx.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/* The parse function. */
extern int yyparse();

/* File to parse. */
extern FILE* yyin;

/* Name of current file. */
std::string curr;

extern int yydebug;

/* Parses the given file, and returns true on success. */
static bool read_file(const char* name)
{
    yyin = fopen(name, "r");
    if (yyin == NULL)
    {
        std::cerr << "ERROR IN OPENING FILE" << std::endl;
        return false;
    }

    else
    {
        curr = name;
        bool success = (yyparse() == 0);        
        fclose(yyin);
        return success;
    }
}

namespace test
{
	TEST_CLASS(DecompositionParseTest)
	{
	public:

        TEST_METHOD(ParseCompositeAction)
        {
            read_file("..\\test\\composite_action.pddl");
            const Domain* parsed = Domain::find("composite_action");
            Assert::IsTrue(parsed->requirements.decompositions, L"Decompositions were implicitly specified due to an action being marked with a composite property.");

            const ActionSchema* pick_up = parsed->find_action("pick-up");
            Assert::IsTrue(pick_up->composite(), L"pick-up should have been parsed as a composite action.");

            const ActionSchema* put_down = parsed->find_action("put-down");
            Assert::IsTrue(put_down->composite(), L"put-down should have been parsed as a composite action.");

            const ActionSchema* stack = parsed->find_action("stack");
            Assert::IsFalse(stack->composite(), L"stack should have been parsed as a non-composite action");
        }
		

        TEST_METHOD(ParseDecompositionSchema)
        {
            read_file("..\\test\\travel_domain.pddl");
            const Domain* parsed = Domain::find("travel");

            Assert::IsTrue(parsed->requirements.decompositions, L"Decompositions were implicitly specified due to an action being marked with a composite property.");
            Assert::IsTrue(parsed->requirements.typing, L"Typing was implicitly specified due to parsing a 'types' list.");

            const DecompositionSchema* travel_drive = parsed->find_decomposition("travel", "drive");
            Assert::IsNotNull(travel_drive, L"The travel-drive decomposition should exist as a not-null, parsed thing.");
            Assert::AreEqual((size_t) 4, travel_drive->parameters().size(), L"The travel-drive decomposition should have been parsed with four parameters.");

            // Check the dummy initial and final
            Step decomp_dummy_initial_step = travel_drive->pseudo_steps()->tail->tail->head;
            Assert::AreEqual((size_t) 1, decomp_dummy_initial_step.action().effects().size(), L"Decomposition dummy initial step should have 1 effect");
            Assert::IsTrue(decomp_dummy_initial_step.action().condition().tautology(), L"Decomposition dummy initial step should have no preconditions.");

            Step decomp_dummy_final_step = travel_drive->pseudo_steps()->tail->head;
            Assert::AreEqual((size_t) 0, decomp_dummy_final_step.action().effects().size(), L"Decomposition dummy final step should have no effects.");
            Assert::IsTrue(typeid(decomp_dummy_final_step.action().condition()) == typeid(Conjunction), L"Decomposition dummy final step precondition Formula should be a Conjunction.");

            const Conjunction& conj = dynamic_cast<const Conjunction&>(decomp_dummy_final_step.action().condition());
            Assert::AreEqual((size_t) 2, conj.conjuncts().size(), L"Decomposition dummy final step should have 2 preconditions.");


        }

        TEST_METHOD(ParseDecompositionsRequirement)
        {
            read_file("..\\test\\decompositions_requirement.pddl");
            const Domain* parsed = Domain::find("decompositions_requirement");
            Assert::IsTrue(parsed->requirements.decompositions, L"Decompositions were specified as part of the domain definition requirements.");
        }


	};
}