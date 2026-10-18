// src/ctrl_send.h: the HID control channel's send queue (upstream d4af34e's
// "feature report get busy when init", without its wait).
#include "ctrl_send.h"
#include "test.h"
#include <vector>

namespace {
// A fake l2cap_send: answers from a script (Sent when the script runs out)
// and records every frame that went out.
struct FakeLink {
    std::vector<CtrlSendResult> script;
    size_t next = 0;
    std::vector<std::vector<uint8_t>> sent;
    unsigned calls = 0;
    CtrlSendResult operator()(const uint8_t *d, uint16_t n) {
        calls++;
        const CtrlSendResult r = next < script.size() ? script[next++] : CtrlSendResult::Sent;
        if (r == CtrlSendResult::Sent) sent.emplace_back(d, d + n);
        return r;
    }
};
template <typename Q>
CtrlSubmit get(Q &q, FakeLink &link, uint8_t id) {
    const uint8_t f[] = {0x43, id};
    return q.submit(f, sizeof(f), [&](const uint8_t *d, uint16_t n) { return link(d, n); });
}
template <typename Q>
bool drain(Q &q, FakeLink &link, unsigned *dropped = nullptr) {
    return q.drain([&](const uint8_t *d, uint16_t n) { return link(d, n); }, dropped);
}
}  // namespace

TEST(ctrl_send_free_link_sends_at_once) {
    CtrlSendQueue<8, 66> q;
    FakeLink link;
    CHECK(get(q, link, 0x09) == CtrlSubmit::Sent);
    CHECK(!q.pending());
    CHECK_EQ(link.sent.size(), 1u);
    CHECK_EQ(link.sent[0][1], 0x09);
}

// init_feature(): 0x09, 0x22, 0x05, then 0x20 (whose reply connects USB). The
// stack is busy from the second GET on: nothing is lost and 0x20 stays last.
TEST(ctrl_send_busy_burst_keeps_every_get_in_order) {
    CtrlSendQueue<8, 66> q;
    FakeLink link;
    link.script = {CtrlSendResult::Sent, CtrlSendResult::Busy};
    CHECK(get(q, link, 0x09) == CtrlSubmit::Sent);
    CHECK(get(q, link, 0x22) == CtrlSubmit::Queued);
    // Queued behind 0x22 without trying the link (order is the protocol).
    const unsigned calls = link.calls;
    CHECK(get(q, link, 0x05) == CtrlSubmit::Queued);
    CHECK(get(q, link, 0x20) == CtrlSubmit::Queued);
    CHECK_EQ(link.calls, calls);
    CHECK_EQ(q.size(), 3u);
    // CAN_SEND_NOW: one buffer free, then busy again.
    link.script.push_back(CtrlSendResult::Sent);
    link.script.push_back(CtrlSendResult::Busy);
    CHECK(drain(q, link));  // frames left: ask for CAN_SEND_NOW again
    CHECK_EQ(q.size(), 2u);
    CHECK(!drain(q, link));  // the rest go out
    CHECK(!q.pending());
    CHECK_EQ(link.sent.size(), 4u);
    const uint8_t order[] = {0x09, 0x22, 0x05, 0x20};
    for (size_t i = 0; i < 4; i++) CHECK_EQ(link.sent[i][1], order[i]);
}

// The Edge unlock: SET 0x65 (63 bytes) then SET 0x80 (61): payload intact.
TEST(ctrl_send_queued_set_keeps_its_bytes) {
    CtrlSendQueue<8, 66> q;
    FakeLink link;
    link.script = {CtrlSendResult::Busy};
    uint8_t set[63];
    for (int i = 0; i < 63; i++) set[i] = static_cast<uint8_t>(i * 7);
    CHECK(q.submit(set, sizeof(set), [&](const uint8_t *d, uint16_t n) { return link(d, n); }) == CtrlSubmit::Queued);
    CHECK(!drain(q, link));
    CHECK_EQ(link.sent.size(), 1u);
    CHECK_EQ(link.sent[0].size(), 63u);
    bool same = true;
    for (int i = 0; i < 63; i++) same = same && link.sent[0][i] == set[i];
    CHECK(same);
}

// A send error (channel closing, frame over the MTU) drops that frame and
// never wedges the ones behind it.
TEST(ctrl_send_failed_frame_is_dropped_not_retried) {
    CtrlSendQueue<8, 66> q;
    FakeLink link;
    link.script = {CtrlSendResult::Failed};
    CHECK(get(q, link, 0x81) == CtrlSubmit::Dropped);
    CHECK(!q.pending());
    link.script = {CtrlSendResult::Busy, CtrlSendResult::Failed, CtrlSendResult::Sent};
    link.next = 0;
    CHECK(get(q, link, 0x70) == CtrlSubmit::Queued);
    CHECK(get(q, link, 0x71) == CtrlSubmit::Queued);
    unsigned dropped = 0;
    CHECK(!drain(q, link, &dropped));
    CHECK_EQ(dropped, 1u);
    CHECK_EQ(link.sent.size(), 1u);
    CHECK_EQ(link.sent[0][1], 0x71);
}

TEST(ctrl_send_full_queue_drops_the_newest) {
    CtrlSendQueue<4, 66> q;
    FakeLink link;
    link.script = {CtrlSendResult::Busy};
    for (uint8_t id = 0x70; id < 0x74; id++) CHECK(get(q, link, id) == CtrlSubmit::Queued);
    CHECK(get(q, link, 0x74) == CtrlSubmit::Dropped);
    CHECK_EQ(q.size(), 4u);
    CHECK(!drain(q, link));
    CHECK_EQ(link.sent.size(), 4u);
    CHECK_EQ(link.sent[3][1], 0x73);
    // The ring wraps: the next burst still comes out in order.
    link.script = {CtrlSendResult::Sent, CtrlSendResult::Busy};
    link.next = 0;
    link.sent.clear();
    for (uint8_t id = 0x10; id < 0x14; id++) get(q, link, id);
    CHECK(!drain(q, link));
    CHECK_EQ(link.sent.size(), 4u);
    for (size_t i = 0; i < 4; i++) CHECK_EQ(link.sent[i][1], 0x10 + i);
}

TEST(ctrl_send_rejects_empty_and_oversized_frames) {
    CtrlSendQueue<8, 66> q;
    FakeLink link;
    uint8_t big[67]{};
    CHECK(q.submit(big, 0, [&](const uint8_t *d, uint16_t n) { return link(d, n); }) == CtrlSubmit::Dropped);
    CHECK(q.submit(big, 67, [&](const uint8_t *d, uint16_t n) { return link(d, n); }) == CtrlSubmit::Dropped);
    CHECK(q.submit(big, 66, [&](const uint8_t *d, uint16_t n) { return link(d, n); }) == CtrlSubmit::Sent);
    CHECK_EQ(link.calls, 1u);
}

// A disconnect clears the queue: nothing from the old link reaches the next.
TEST(ctrl_send_clear_forgets_the_old_link) {
    CtrlSendQueue<8, 66> q;
    FakeLink link;
    link.script = {CtrlSendResult::Busy};
    get(q, link, 0x20);
    get(q, link, 0x81);
    q.clear();
    CHECK(!q.pending());
    CHECK(get(q, link, 0x09) == CtrlSubmit::Sent);
    CHECK_EQ(link.sent.size(), 1u);
    CHECK_EQ(link.sent[0][1], 0x09);
}
