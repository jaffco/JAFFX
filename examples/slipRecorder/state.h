#ifndef STATE_H
#define STATE_H

class FSM;

class State {
    public:
        virtual void enter(FSM* recorder) = 0;
        virtual void exit(FSM* recorder) = 0;
        virtual void handle(FSM* recorder) = 0;
        virtual ~State(){};
};

class Sleep : public State {
    public:
        static Sleep& getInstance() {
            static Sleep instance;
            return instance;
        }
        void enter(FSM* recorder) override;
        void exit(FSM* recorder) override;
        void handle(FSM* recorder) override;

    private:
        Sleep() {}
        Sleep(const Sleep&) = delete;
        Sleep& operator=(const Sleep&) = delete;
};

class SD_Check : public State {
    public:
        static SD_Check& getInstance() {
            static SD_Check instance;
            return instance;
        }
        void enter(FSM* recorder) override;
        void exit(FSM* recorder) override;
        void handle(FSM* recorder) override;

    private:
        SD_Check() {}
        SD_Check(const SD_Check&) = delete;
        SD_Check& operator=(const SD_Check&) = delete;
};

class Record : public State {
    public:
        static Record& getInstance() {
            static Record instance;
            return instance;
        }
        void enter(FSM* recorder) override;
        void exit(FSM* recorder) override;
        void handle(FSM* recorder) override;

    private:
        Record() {}
        Record(const Record&) = delete;
        Record& operator=(const Record&) = delete;
};

#endif