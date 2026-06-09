#ifndef __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__
#define __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__

#include "mem/port.hh"
#include "params/SimpleMemobj.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class SimpleMemobj : public SimObject
{
  private:

    // CPUSidePort — the "inbox" on the CPU-facing side.
    // Inherits ResponsePort: it receives requests from the CPU (a requestor)
    // and sends responses back. One instance for inst fetches, one for data.
    class CPUSidePort : public ResponsePort
    {
      private:
        SimpleMemobj *owner;
        bool needRetry;
        PacketPtr blockedPacket;

      public:
        CPUSidePort(const std::string& name, SimpleMemobj *owner) :
            ResponsePort(name), owner(owner), needRetry(false),
            blockedPacket(nullptr)
        { }

        void sendPacket(PacketPtr pkt);
        AddrRangeList getAddrRanges() const override;
        void trySendRetry();

      protected:
        Tick recvAtomic(PacketPtr pkt) override
        { panic("recvAtomic unimpl."); }

        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    // MemSidePort — the "outbox" on the memory-facing side.
    // Inherits RequestPort: it sends requests toward memory (a responder)
    // and receives responses back.
    class MemSidePort : public RequestPort
    {
      private:
        SimpleMemobj *owner;
        PacketPtr blockedPacket;

      public:
        MemSidePort(const std::string& name, SimpleMemobj *owner) :
            RequestPort(name), owner(owner), blockedPacket(nullptr)
        { }

        void sendPacket(PacketPtr pkt);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    AddrRangeList getAddrRanges() const;
    void sendRangeChange();

    CPUSidePort instPort;
    CPUSidePort dataPort;
    MemSidePort memPort;
    bool blocked;

  public:
    SimpleMemobj(const SimpleMemobjParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
};

} // namespace gem5

#endif // __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__
