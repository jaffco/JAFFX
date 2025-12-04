#ifndef STATE_MACHINE_HPP
#define STATE_MACHINE_HPP

class StateMachine; // forward declaration

// Interface class for all states
class IState {
public:
    virtual void enter(StateMachine*) {}
    virtual void exit(StateMachine*) {}
    virtual void handle(StateMachine*) {}
    virtual ~IState() = default;
};

// CRTP base state class
template <class TState> 
class State : public IState {
public:
  static TState& getInstance() {
		static TState instance;
		return instance;
	}

protected:
	State() = default;
	State(const State&) = delete;
	State& operator=(const State&) = delete;
};

// Sleep state
class Sleep : public State<Sleep> {
public:
	void enter(StateMachine* recorder) override {}
	void exit(StateMachine* recorder) override {}
	void handle(StateMachine* recorder) override {}

private:
	Sleep() = default;
	friend class State<Sleep>;
};

// SD_Check state
class SD_Check : public State<SD_Check> {
public:
	void enter(StateMachine* recorder) override {}
	void exit(StateMachine* recorder) override {}
	void handle(StateMachine* recorder) override {}

private:
	SD_Check() = default;
	friend class State<SD_Check>;
};

// Record state
class Record : public State<Record> {
public:
	void enter(StateMachine* recorder) override {}
	void exit(StateMachine* recorder) override {}
	void handle(StateMachine* recorder) override {}

private:
	Record() = default;
	friend class State<Record>;
};

// State machine class
class StateMachine {
private:
	IState* currentState;

public:
	StateMachine() {
		currentState = &Sleep::getInstance();
	}

	inline IState* getCurrentState() const { 
		return currentState; 
	}
	
	// This will get called by the current state
	void setState(IState& newState) {
		if (currentState) {
			currentState->exit(this);
		}
		currentState = &newState;
		if (currentState) {
			currentState->enter(this);
		}
	}

};

#endif // STATE_MACHINE_HPP