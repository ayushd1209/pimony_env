#include "learning_gem5/part2/hello_object.hh"
#include "base/trace.hh"
#include "debug/HelloExample.hh"

namespace gem5 {

    HelloObject::HelloObject(const HelloObjectParams &params) :
    SimObject(params),
    event([this]{ processEvent(); }, name() + ".event"),
    latency(params.time_to_wait),
    timesLeft(params.number_of_fires),
    goodbye(params.goodbye_object)
    {
        DPRINTF(HelloExample, "Created the hello object with the name %s\n", name());
        panic_if(!goodbye, "HelloObject must have a non-null GoodbyeObject");
    }
    void HelloObject::processEvent()
    {   
        timesLeft--;
        DPRINTF(HelloExample, "Hello world! Processing the event!\n");

        if (timesLeft <= 0) {
            DPRINTF(HelloExample, "Done firing!\n");
            goodbye->sayGoodbye(name());
        } else {
            schedule(event, curTick() + latency);
        }
    }
    void HelloObject::startup()
    {
        schedule(event, latency);
    }
}