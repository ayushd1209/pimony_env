#include "learning_gem5/part2/simple_memobj.hh"

#include "base/trace.hh"
#include "debug/SimpleMemobj.hh"

namespace gem5
{

SimpleMemobj::SimpleMemobj(const SimpleMemobjParams &params) :
    SimObject(params),
    instPort(params.name + ".inst_port", this),
    dataPort(params.name + ".data_port", this),
    memPort(params.name + ".mem_side", this),
    blocked(false)
{
}

Port &
SimpleMemobj::getPort(const std::string &if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");
    if (if_name == "inst_port") {
        return instPort;
    } else if (if_name == "data_port") {
        return dataPort;
    } else if (if_name == "mem_side") {
        return memPort;
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

// --- CPUSidePort ---

void
SimpleMemobj::CPUSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    if(!sendTimingResp(pkt)) {
        DPRINTF(SimpleMemobj, "Couldn't send response for addr %#x, blocking\n",
                pkt->getAddr());
        blockedPacket = pkt;
        }

}

AddrRangeList
SimpleMemobj::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
SimpleMemobj::CPUSidePort::trySendRetry()
{
    if (needRetry==true && blockedPacket == nullptr) {
        DPRINTF(SimpleMemobj, "Trying to send retry\n");
        needRetry = false;
        sendRetryReq();
    }
}

void
SimpleMemobj::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    return owner->handleFunctional(pkt);
}

bool
SimpleMemobj::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    if(!owner->handleRequest(pkt)){
        needRetry = true;
        return false;
    }
    return true;
}

void
SimpleMemobj::CPUSidePort::recvRespRetry()
{
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    DPRINTF(SimpleMemobj, "Retrying to send response for addr %#x\n",
            pkt->getAddr());
    sendPacket(pkt);
}

// --- MemSidePort ---

void
SimpleMemobj::MemSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    if(!sendTimingReq(pkt)) {
        DPRINTF(SimpleMemobj, "Couldn't send request for addr %#x, blocking\n",
                pkt->getAddr());
        blockedPacket = pkt;
    }
}

bool
SimpleMemobj::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    return owner->handleResponse(pkt);
}

void
SimpleMemobj::MemSidePort::recvReqRetry()
{
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    DPRINTF(SimpleMemobj, "Retrying to send request for addr %#x\n",
            pkt->getAddr());    
    sendPacket(pkt);
}

void
SimpleMemobj::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

// --- SimpleMemobj ---

bool
SimpleMemobj::handleRequest(PacketPtr pkt)
{   
    if (blocked) {
        DPRINTF(SimpleMemobj, "Currently blocked, can't handle request for addr %#x\n",
                pkt->getAddr());       
        return false;
    }
    DPRINTF(SimpleMemobj, "Got request for addr %#x\n", pkt->getAddr());
    blocked = true;
    memPort.sendPacket(pkt);
    return true;
}

bool
SimpleMemobj::handleResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(SimpleMemobj, "Got response for addr %#x\n", pkt->getAddr());
    blocked = false;
    if(pkt->req->isInstFetch()) {
        instPort.sendPacket(pkt);
    } else {
        dataPort.sendPacket(pkt);
    }
    instPort.trySendRetry();
    dataPort.trySendRetry();
    return true;
}

void
SimpleMemobj::handleFunctional(PacketPtr pkt)
{
    DPRINTF(SimpleMemobj, "Got functional request for addr %#x\n", pkt->getAddr());
    memPort.sendFunctional(pkt);
}

AddrRangeList
SimpleMemobj::getAddrRanges() const
{
    DPRINTF(SimpleMemobj, "Sending new ranges\n");
    return memPort.getAddrRanges();
}

void
SimpleMemobj::sendRangeChange()
{
    DPRINTF(SimpleMemobj, "Sending range change\n");
    instPort.sendRangeChange();
    dataPort.sendRangeChange();
}

} // namespace gem5
