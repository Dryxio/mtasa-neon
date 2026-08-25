/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Tests/client/DebugEvent_Tests.cpp
 *  PURPOSE:     Structured diagnostic codec and store tests
 *
 *****************************************************************************/

#include <gtest/gtest.h>

#include "MockBitStream.h"
#include <net/SDebugEvent.h>
#include "../../Client/core/CDebugEventStore.h"

namespace
{
    SDebugEvent MakeEvent(std::string message = "boom")
    {
        SDebugEvent event;
        event.sequence = 42;
        event.timestamp = 1000;
        event.side = EDebugEventSide::SERVER;
        event.severity = EDebugEventSeverity::ERROR_MESSAGE;
        event.line = 91;
        event.red = 240;
        event.green = 20;
        event.blue = 10;
        event.category = "script";
        event.context = "event handler: inventory:take";
        event.resource = "inventory";
        event.file = "server/items.lua";
        event.message = std::move(message);
        event.correlationId = "request-7";
        return event;
    }
}

TEST(DebugEventCodec, RoundTripsEveryField)
{
    MockBitStream stream;
    const auto    input = MakeEvent("échec مرحبا");
    WriteDebugEvent(stream, input);
    stream.ResetReadPointer();

    SDebugEvent output;
    ASSERT_TRUE(ReadDebugEvent(stream, output));
    EXPECT_EQ(output.sequence, input.sequence);
    EXPECT_EQ(output.timestamp, input.timestamp);
    EXPECT_EQ(output.side, input.side);
    EXPECT_EQ(output.severity, input.severity);
    EXPECT_EQ(output.line, input.line);
    EXPECT_EQ(output.context, input.context);
    EXPECT_EQ(output.resource, input.resource);
    EXPECT_EQ(output.file, input.file);
    EXPECT_EQ(output.message, input.message);
    EXPECT_EQ(output.correlationId, input.correlationId);
}

TEST(DebugEventCodec, ClampsOversizedStringsOnWrite)
{
    MockBitStream stream;
    auto          input = MakeEvent(std::string(SDebugEvent::MESSAGE_BYTE_LIMIT + 100, 'x'));
    WriteDebugEvent(stream, input);
    stream.ResetReadPointer();

    SDebugEvent output;
    ASSERT_TRUE(ReadDebugEvent(stream, output));
    EXPECT_EQ(output.message.size(), SDebugEvent::MESSAGE_BYTE_LIMIT);
}

TEST(DebugEventCodec, RejectsUnknownWireVersionWithoutMutatingOutput)
{
    MockBitStream stream;
    stream.Write(static_cast<unsigned char>(SDebugEvent::WIRE_VERSION + 1));
    stream.ResetReadPointer();
    SDebugEvent output = MakeEvent("unchanged");
    EXPECT_FALSE(ReadDebugEvent(stream, output));
    EXPECT_EQ(output.message, "unchanged");
}

TEST(DebugEventCodec, RejectsTruncatedPayload)
{
    MockBitStream stream;
    stream.Write(SDebugEvent::WIRE_VERSION);
    stream.Write(static_cast<unsigned int>(4));
    stream.ResetReadPointer();
    SDebugEvent output;
    EXPECT_FALSE(ReadDebugEvent(stream, output));
}

TEST(DebugEventStore, CoalescesOnlyAdjacentIdenticalEvents)
{
    CDebugEventStore store;
    auto             first = MakeEvent();
    store.Add(first);
    first.timestamp = 1010;
    store.Add(first);
    ASSERT_EQ(store.GetEvents().size(), 1u);
    EXPECT_EQ(store.GetEvents().front().repeatCount, 2u);
    EXPECT_EQ(store.GetEvents().front().firstSeen, 1000u);
    EXPECT_EQ(store.GetEvents().front().lastSeen, 1010u);

    auto client = first;
    client.side = EDebugEventSide::CLIENT;
    store.Add(client);
    EXPECT_EQ(store.GetEvents().size(), 2u);
}

TEST(DebugEventStore, CapacityIsBoundedAndReported)
{
    CDebugEventStore store(2);
    for (unsigned int i = 0; i < 3; ++i)
    {
        auto event = MakeEvent(std::to_string(i));
        event.sequence = i;
        store.Add(event);
    }
    ASSERT_EQ(store.GetEvents().size(), 2u);
    EXPECT_EQ(store.GetEvents().front().message, "1");
    EXPECT_EQ(store.GetDroppedCount(), 1u);
}

TEST(DebugEventStore, EntryIdentitySurvivesProducerSequenceReuse)
{
    CDebugEventStore store;
    auto             first = MakeEvent("before reconnect");
    store.Add(first);
    const auto firstEntryId = store.GetEvents().back().entryId;

    auto afterReconnect = MakeEvent("after reconnect");
    afterReconnect.sequence = first.sequence;
    store.Add(afterReconnect);
    const auto secondEntryId = store.GetEvents().back().entryId;
    EXPECT_NE(secondEntryId, firstEntryId);

    store.Clear();
    store.Add(first);
    EXPECT_GT(store.GetEvents().back().entryId, secondEntryId);
}

TEST(DebugEventStore, CaptureIncludesThirtySecondPreroll)
{
    CDebugEventStore store;
    auto             oldEvent = MakeEvent("old");
    oldEvent.timestamp = 1000;
    store.Add(oldEvent);
    auto recentEvent = MakeEvent("recent");
    recentEvent.timestamp = 35000;
    store.Add(recentEvent);

    ASSERT_TRUE(store.StartCapture(40000));
    EXPECT_TRUE(store.HasCapture());
    ASSERT_EQ(store.GetCapture().size(), 1u);
    EXPECT_EQ(store.GetCapture().front().message, "recent");

    auto liveEvent = MakeEvent("live");
    liveEvent.timestamp = 41000;
    store.Add(liveEvent);
    ASSERT_TRUE(store.StopCapture(42000));
    ASSERT_EQ(store.GetCapture().size(), 2u);
    EXPECT_EQ(store.GetCapture().back().message, "live");
}

TEST(DebugEventStore, ClearAlsoDiscardsRecordedCapture)
{
    CDebugEventStore store;
    store.Add(MakeEvent());
    ASSERT_TRUE(store.StartCapture(1000));
    ASSERT_TRUE(store.HasCapture());

    store.Clear();

    EXPECT_TRUE(store.GetEvents().empty());
    EXPECT_TRUE(store.GetCapture().empty());
    EXPECT_FALSE(store.HasCapture());
    EXPECT_FALSE(store.IsCaptureActive());
}
