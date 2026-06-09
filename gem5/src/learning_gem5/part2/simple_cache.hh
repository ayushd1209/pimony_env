#ifndef __LEARNING_GEM5_SIMPLE_CACHE_SIMPLE_CACHE_HH__
#define __LEARNING_GEM5_SIMPLE_CACHE_SIMPLE_CACHE_HH__

#include <unordered_map>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/SimpleCache.hh"
#include "sim/clocked_object.hh"

namespace gem5
{

// SimpleCache inherits from ClockedObject (not SimObject).
// ClockedObject gives you a clock domain — you can schedule events after
// N cycles using clockEdge(latency) and schedule().
class SimpleCache : public ClockedObject
{
  private:

    // CPUSidePort is the same pattern as SimpleMemobj, but now it's a
    // VECTOR port — one port handles both inst and data accesses, indexed
    // by `id`. The id is used to remember which port a request came from
    // so we can send the response back to the right one.
    class CPUSidePort : public ResponsePort
    {
      private:
        int id;                   // index into cpuPorts vector
        SimpleCache *owner;
        bool needRetry;
        PacketPtr blockedPacket;

      public:
        CPUSidePort(const std::string& name, int id, SimpleCache *owner) :
            ResponsePort(name), id(id), owner(owner), needRetry(false),
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

    // MemSidePort — identical pattern to SimpleMemobj.
    class MemSidePort : public RequestPort
    {
      private:
        SimpleCache *owner;
        PacketPtr blockedPacket;

      public:
        MemSidePort(const std::string& name, SimpleCache *owner) :
            RequestPort(name), owner(owner), blockedPacket(nullptr)
        { }

        void sendPacket(PacketPtr pkt);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    // handleRequest now takes a port_id so we know which cpuPort to reply to.
    bool handleRequest(PacketPtr pkt, int port_id);
    bool handleResponse(PacketPtr pkt);

    // sendResponse: unblocks the cache and sends the response to the correct
    // cpuPort. Separated from handleResponse because it's also called on a hit.
    void sendResponse(PacketPtr pkt);

    void handleFunctional(PacketPtr pkt);

    // accessTiming: scheduled after `latency` cycles. Does the actual cache
    // lookup and either responds (hit) or forwards to memory (miss).
    void accessTiming(PacketPtr pkt);

    // accessFunctional: the actual cache read/write logic. Used by both
    // timing and functional paths. Returns true on hit, false on miss.
    bool accessFunctional(PacketPtr pkt);

    // insert: add a block to the cache. Evicts a random entry if full,
    // writing it back to memory first (writeback policy).
    void insert(PacketPtr pkt);

    AddrRangeList getAddrRanges() const;
    void sendRangeChange() const;

    const Cycles latency;        // hit/miss latency in cycles
    const unsigned blockSize;    // cache line size (from system)
    const unsigned capacity;     // number of blocks (size / blockSize)

    // Vector of CPU-side ports — one per connected CPU port (inst + data)
    std::vector<CPUSidePort> cpuPorts;
    MemSidePort memPort;

    bool blocked;
    PacketPtr originalPacket;  // saved when we upgrade a small pkt to block size
    int waitingPortId;         // which cpuPort is waiting for the response
    Tick missTime;             // when the miss started (for latency stats)

    // The actual cache storage: maps block-aligned address → data bytes
    std::unordered_map<Addr, uint8_t*> cacheStore;

  protected:
    // Statistics — declared in a nested struct, registered with gem5's stats framework
    struct SimpleCacheStats : public statistics::Group
    {
        SimpleCacheStats(statistics::Group *parent);
        statistics::Scalar hits;
        statistics::Scalar misses;
        statistics::Histogram missLatency;
        statistics::Formula hitRatio;
    } stats;

  public:
    SimpleCache(const SimpleCacheParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
};

} // namespace gem5

#endif // __LEARNING_GEM5_SIMPLE_CACHE_SIMPLE_CACHE_HH__
