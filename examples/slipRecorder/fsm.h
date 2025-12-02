#ifndef FSM_H
#define FSM_H

class State;

class FSM
{
public:
	FSM();
	inline State* getCurrentState() const { return currentState; }
	// This will get called by the current state
	void setState(State& newState);
private:
	State* currentState;
};

#endif