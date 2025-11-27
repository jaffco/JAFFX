#include "fsm.h"

FSM::FSM()
{
    currentState = SleepState::getInstance();
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