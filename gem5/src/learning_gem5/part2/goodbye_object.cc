#include "learning_gem5/part2/goodbye_object.hh"

#include "base/trace.hh"
#include "debug/HelloExample.hh"
#include "sim/sim_exit.hh"

namespace gem5 {

// Constructor — runs when gem5 builds your SimObject from the Python config.
GoodbyeObject::GoodbyeObject(const GoodbyeObjectParams &params) :
    SimObject(params),
    event([this]{ processEvent(); }, name() + ".event"),
    bandwidth(params.write_bandwidth),
    bufferSize(params.buffer_size),
    buffer(nullptr),          // start as null - we allocate next
    bufferUsed(0)
{
    // Allocate the buffer dynamically. Must be freed in the destructor.
    buffer = new char[bufferSize];
    DPRINTF(HelloExample, "Created the goodbye object\n");
}

// Destructor — runs when the SimObject is destroyed.
// We allocated `buffer` with `new[]`, so we MUST free it with `delete[]`.
GoodbyeObject::~GoodbyeObject()
{
    delete[] buffer;
}

// Internal event callback — fires when the scheduled event triggers.
// Just delegates to fillBuffer().
void
GoodbyeObject::processEvent()
{
    DPRINTF(HelloExample, "Processing the event!\n");
    fillBuffer();
}

// Public entry point. Called by HelloObject when it's done firing.
// Builds the message and kicks off the bandwidth-limited write.
void
GoodbyeObject::sayGoodbye(std::string other_name)
{
    DPRINTF(HelloExample, "Saying goodbye to %s\n", other_name);
    message = "Goodbye " + other_name + "!! ";
    fillBuffer();
}

// Copies as much of `message` into `buffer` as fits this call.
// The bandwidth limit is modeled by the delay before the NEXT call.
void
GoodbyeObject::fillBuffer()
{
    assert(message.length() > 0);

    int bytes_copied = 0;
    for (auto it = message.begin();
         it < message.end() && bufferUsed < bufferSize - 1;
         it++, bufferUsed++, bytes_copied++) {
        buffer[bufferUsed] = *it;
    }

    if (bufferUsed < bufferSize - 1) {
        DPRINTF(HelloExample,
                "Scheduling another fillBuffer in %d ticks\n",
                bandwidth * bytes_copied);
        schedule(event, curTick() + bandwidth * bytes_copied);
    } else {
        DPRINTF(HelloExample, "Goodbye done copying!\n");
        exitSimLoop(buffer, 0, curTick() + bandwidth * bytes_copied);
    }
}

} // namespace gem5
