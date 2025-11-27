#ifndef STATE_H
#define STATE_H

#include "fsm.h"
class FSM;

class State {
    public:
        virtual void enter(FSM* recorder) = 0;
        virtual void exit(FSM* recorder) = 0;
        virtual void handle(FSM* recorder) = 0;
        virtual ~State(){};
};

#endif