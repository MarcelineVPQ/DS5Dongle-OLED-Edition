//
// The HID control channel's send queue (bt.cpp). Every frame the dongle sends
// the controller on the control channel (GET 0x43 <id>, SET 0x53 <id> ...)
// goes through it: l2cap_send() refuses a frame with BTSTACK_ACL_BUFFERS_FULL
// while the controller still holds every ACL buffer (back-to-back requests at
// connect, an Edge unlock's two SETs, a test command while the speaker
// streams), and the frame used to be lost. The firmware report GET 0x20, the
// last one at connect, is what connects USB, so losing it left the controller
// unusable until the 10 s connect watchdog started over.
//
// A frame that can't go out now is queued and sent, in order, from the
// channel's L2CAP_EVENT_CAN_SEND_NOW; while anything is queued, new frames
// queue behind it, because the controller answers in order (init_feature's
// GET 0x20 must stay last). Upstream awalol d4af34e ("feature report get busy
// when init") fixes the same loss with a 100 ms wait that polls the BT stack;
// a queue needs no wait inside a BTstack or TinyUSB callback.
//
// Pure, no Pico SDK, for the host tests. The send function returns
// CtrlSendResult: Sent, Busy (keep it and retry on CAN_SEND_NOW) or Failed
// (any other L2CAP error, e.g. the channel closed: drop it, never wedge the
// queue on a frame that can't ever go out).
//

#ifndef DS5_BRIDGE_CTRL_SEND_H
#define DS5_BRIDGE_CTRL_SEND_H

#include <cstddef>
#include <cstdint>
#include <cstring>

enum class CtrlSendResult : uint8_t { Sent, Busy, Failed };

enum class CtrlSubmit : uint8_t {
    Sent,     // on the air now
    Queued,   // waits for CAN_SEND_NOW: the caller asks for that event
    Dropped,  // queue full, frame too long or the send failed: lost (logged)
};

template <size_t Depth, size_t MaxLen>
class CtrlSendQueue {
public:
    // A frame for the control channel. Never blocks.
    template <typename Send>
    CtrlSubmit submit(const uint8_t *data, uint16_t len, Send send) {
        // error: NOT-WORTH — the caller logs a Dropped frame; nothing sends longer than MaxLen (static_assert in bt.cpp)
        if (len == 0 || len > MaxLen) return CtrlSubmit::Dropped;
        if (count_ == 0) {
            const CtrlSendResult r = send(data, len);
            if (r == CtrlSendResult::Sent) return CtrlSubmit::Sent;
            if (r == CtrlSendResult::Failed) return CtrlSubmit::Dropped;
        }
        // error: NOT-WORTH — the caller logs a Dropped frame; Depth covers every burst the firmware sends
        if (count_ == Depth) return CtrlSubmit::Dropped;
        Frame &f = frames_[(head_ + count_) % Depth];
        f.len = len;
        memcpy(f.data, data, len);
        count_++;
        return CtrlSubmit::Queued;
    }

    // CAN_SEND_NOW for the control channel: sends queued frames in order until
    // the stack is busy again. True when frames are left (ask for the event
    // again). `dropped` counts frames a Failed send removed.
    template <typename Send>
    bool drain(Send send, unsigned *dropped = nullptr) {
        // loop: NOT-WORTH — at most Depth frames per CAN_SEND_NOW event, stops at the first Busy
        while (count_ > 0) {
            const Frame &f = frames_[head_];
            const CtrlSendResult r = send(f.data, f.len);
            if (r == CtrlSendResult::Busy) break;
            if (r == CtrlSendResult::Failed && dropped) (*dropped)++;
            head_ = (head_ + 1) % Depth;
            count_--;
        }
        return count_ > 0;
    }

    bool pending() const { return count_ > 0; }
    size_t size() const { return count_; }
    // The channel closed: queued frames belong to the old link.
    void clear() { head_ = count_ = 0; }

private:
    struct Frame {
        uint16_t len;
        uint8_t data[MaxLen];
    };
    Frame frames_[Depth]{};
    size_t head_ = 0, count_ = 0;
};

#endif  // DS5_BRIDGE_CTRL_SEND_H
