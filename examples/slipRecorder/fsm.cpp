#include "fsm.h"
#include "state.h"

FSM::FSM()
{
    currentState = &Sleep::getInstance();
}

void FSM::setState(State& newState)
{
    if (currentState)
    {
        currentState->exit(this);
    }
    currentState = &newState;
    if (currentState)
    {
        currentState->enter(this);
    }
}