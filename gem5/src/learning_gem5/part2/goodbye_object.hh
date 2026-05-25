#ifndef __LEARNING_GEM5_GOODBYE_OBJECT_HH__
#define __LEARNING_GEM5_GOODBYE_OBJECT_HH__


#include <string>

#include "params/GoodbyeObject.hh"
#include "sim/sim_object.hh"
#include "sim/eventq.hh"

namespace gem5 {

class GoodbyeObject : public SimObject
{
private:

void processEvent();
EventFunctionWrapper event;
 
void fillBuffer(); ///Fills the buffer for one iteration. If the buffer isn't full, this function will enqueue another event to continue filling.
int bufferSize; /// The size of the buffer we are going to fill.
float bandwidth; /// The bytes processed per tick.
std::string message; /// The message to put into the buffer.
char *buffer; /// The buffer we are putting our message in.
int bufferUsed; /// The amount of the buffer we've used so far.

public:

void sayGoodbye(std::string name) ;
GoodbyeObject(const GoodbyeObjectParams &p);
~GoodbyeObject();
};

}

#endif