/**************************************************************************/
/*  test_gdscript_language_transport.h                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#ifndef GDSCRIPT_NO_LSP

#include "../language_server/gdscript_language_transport.h"

#include "core/os/thread.h"
#include "tests/test_macros.h"

class TestGDScriptLanguageTransport {
public:
	static void open(GDScriptLanguageTransport &p_transport) {
		p_transport.incoming_events.open();
		p_transport.outgoing_responses.open();
		p_transport.accepting.set();
	}

	static bool push_event(GDScriptLanguageTransport &p_transport, const GDScriptLanguageTransport::Event &p_event) {
		return p_transport.incoming_events.try_push(p_event);
	}

	static bool pop_response(GDScriptLanguageTransport &p_transport, int &r_client_id, String &r_message) {
		GDScriptLanguageTransport::Response response;
		if (!p_transport.outgoing_responses.try_pop(response)) {
			return false;
		}
		r_client_id = response.client_id;
		r_message = response.message;
		p_transport.outgoing_responses.complete(response);
		return true;
	}
};

namespace GDScriptTests {

TEST_SUITE("[Modules][GDScript][LSP][Transport]") {
	TEST_CASE("Incoming and outgoing queues enforce count and byte budgets") {
		GDScriptLanguageTransport transport(2, 2, 24, 24);
		TestGDScriptLanguageTransport::open(transport);

		CHECK(TestGDScriptLanguageTransport::push_event(transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_MESSAGE, 1, "one")));
		CHECK(TestGDScriptLanguageTransport::push_event(transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_MESSAGE, 1, "two")));
		CHECK_FALSE(TestGDScriptLanguageTransport::push_event(transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_MESSAGE, 1, "x")));
		CHECK(TestGDScriptLanguageTransport::push_event(transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_DISCONNECTED, 1)));
		CHECK_EQ(transport.get_pending_event_count(), 3);

		GDScriptLanguageTransport::Event event;
		REQUIRE(transport.pop_event(event));
		CHECK_EQ(event.message, "one");
		REQUIRE(transport.pop_event(event));
		CHECK_EQ(event.message, "two");
		REQUIRE(transport.pop_event(event));
		CHECK_EQ(event.type, GDScriptLanguageTransport::EVENT_DISCONNECTED);

		CHECK(transport.queue_response(1, "one"));
		CHECK(transport.queue_response(1, "two"));
		CHECK_FALSE(transport.queue_response(1, "x"));
		CHECK_EQ(transport.get_pending_response_count(), 2);

		int client_id = -1;
		String response;
		REQUIRE(TestGDScriptLanguageTransport::pop_response(transport, client_id, response));
		CHECK_EQ(client_id, 1);
		CHECK_EQ(response, "one");
		REQUIRE(TestGDScriptLanguageTransport::pop_response(transport, client_id, response));
		CHECK_EQ(response, "two");

		GDScriptLanguageTransport byte_limited_transport(4, 4, 8, 8);
		TestGDScriptLanguageTransport::open(byte_limited_transport);
		CHECK_FALSE(TestGDScriptLanguageTransport::push_event(byte_limited_transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_MESSAGE, 1, "abc")));
		CHECK(TestGDScriptLanguageTransport::push_event(byte_limited_transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_CONNECTED, 1)));
		CHECK_FALSE(byte_limited_transport.queue_response(1, "abc"));
	}

	TEST_CASE("Shutdown clears queues and rejects new work") {
		GDScriptLanguageTransport transport(2, 2);
		TestGDScriptLanguageTransport::open(transport);
		CHECK(TestGDScriptLanguageTransport::push_event(transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_MESSAGE, 1, "pending")));
		CHECK(transport.queue_response(1, "pending"));

		transport.begin_shutdown();
		CHECK_FALSE(transport.is_accepting());
		CHECK_EQ(transport.get_pending_event_count(), 0);
		CHECK_EQ(transport.get_pending_response_count(), 0);
		CHECK_FALSE(TestGDScriptLanguageTransport::push_event(transport, GDScriptLanguageTransport::Event(GDScriptLanguageTransport::EVENT_MESSAGE, 1, "late")));
		CHECK_FALSE(transport.queue_response(1, "late"));
	}

#ifdef THREADS_ENABLED
	struct ProducerContext {
		GDScriptLanguageTransport *transport = nullptr;
		int message_count = 0;
		SafeFlag done;
	};

	static void produce_messages(void *p_userdata) {
		ProducerContext *context = static_cast<ProducerContext *>(p_userdata);
		for (int i = 0; i < context->message_count; i++) {
			const GDScriptLanguageTransport::Event event(GDScriptLanguageTransport::EVENT_MESSAGE, 1, itos(i));
			while (!TestGDScriptLanguageTransport::push_event(*context->transport, event)) {
				Thread::yield();
			}
		}
		context->done.set();
	}

	TEST_CASE("Incoming queue safely hands complete messages across threads") {
		GDScriptLanguageTransport transport(8, 8, 1024, 1024);
		TestGDScriptLanguageTransport::open(transport);

		ProducerContext context;
		context.transport = &transport;
		context.message_count = 256;
		Thread producer;
		producer.start(produce_messages, &context);

		int received = 0;
		while (!context.done.is_set() || transport.get_pending_event_count() > 0) {
			GDScriptLanguageTransport::Event event;
			if (transport.pop_event(event)) {
				CHECK_EQ(event.type, GDScriptLanguageTransport::EVENT_MESSAGE);
				CHECK_EQ(event.message, itos(received));
				received++;
			} else {
				Thread::yield();
			}
		}
		producer.wait_to_finish();
		CHECK_EQ(received, context.message_count);
	}
#endif // THREADS_ENABLED
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
