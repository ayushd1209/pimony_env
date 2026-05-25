#ifndef __LEARNING_GEM5_HELLO_OBJECT_HH__
#define __LEARNING_GEM5_HELLO_OBJECT_HH__

#include "learning_gem5/part2/goodbye_object.hh"

#include "params/HelloObject.hh"
#include "sim/sim_object.hh"
#include "sim/eventq.hh"

namespace gem5 {

    class HelloObject : public SimObject 
    {
        private:
        void processEvent();
        EventFunctionWrapper event;
        const Tick latency;
        int timesLeft;

        GoodbyeObject *goodbye; /// simobjects are referenced as pointers throughout gem5 and hello_object doesnt own it just refers to it

        public:
        HelloObject(const HelloObjectParams &p);
        void startup() override;
    };
}

#endif