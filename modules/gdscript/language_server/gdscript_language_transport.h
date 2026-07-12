/**************************************************************************/
/*  gdscript_language_transport.h                                         */
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

#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/templates/safe_refcount.h"

class TestGDScriptLanguageTransport;
class TestGDScriptLanguageProtocolInitializer;

/**
 * Owns the TCP transport and transfers complete LSP messages across the
 * network/main-thread boundary through bounded queues.
 */
class GDScriptLanguageTransport {
	friend class TestGDScriptLanguageTransport;
	friend class TestGDScriptLanguageProtocolInitializer;

public:
	static constexpr int MAX_BUFFER_SIZE = 4194304;
	static constexpr int MAX_CLIENTS = 8;
	static constexpr int DEFAULT_PENDING_MESSAGES = 64;
	static constexpr uint64_t DEFAULT_PENDING_MESSAGE_BYTES = 16 * 1024 * 1024;
	static constexpr int DEFAULT_PENDING_RESPONSES = 64;
	static constexpr uint64_t DEFAULT_PENDING_RESPONSE_BYTES = 16 * 1024 * 1024;

	enum EventType {
		EVENT_CONNECTED,
		EVENT_MESSAGE,
		EVENT_DISCONNECTED,
	};

	struct Event {
		EventType type = EVENT_MESSAGE;
		int client_id = -1;
		String message;

		Event() = default;
		Event(EventType p_type, int p_client_id, const String &p_message = String()) :
				type(p_type),
				client_id(p_client_id),
				message(p_message) {}
	};

private:
	struct Response {
		int client_id = -1;
		String message;

		Response() = default;
		Response(int p_client_id, const String &p_message) :
				client_id(p_client_id),
				message(p_message) {}
	};

	class IncomingEventQueue {
		mutable Mutex mutex;
		List<Event> events;
		int message_capacity = 0;
		uint64_t message_byte_capacity = 0;
		int control_capacity = 0;
		int message_count = 0;
		uint64_t message_bytes = 0;
		int control_count = 0;
		bool accepting = false;

	public:
		IncomingEventQueue(int p_message_capacity, uint64_t p_message_byte_capacity);

		void open();
		void close_and_clear();
		bool can_accept_client(int p_active_clients) const;
		bool try_push(const Event &p_event);
		bool try_pop(Event &r_event);
		int size() const;
	};

	class OutgoingResponseQueue {
		mutable Mutex mutex;
		List<Response> responses;
		int capacity = 0;
		uint64_t byte_capacity = 0;
		int response_count = 0;
		uint64_t response_bytes = 0;
		bool accepting = false;

	public:
		OutgoingResponseQueue(int p_capacity, uint64_t p_byte_capacity);

		void open();
		void close_and_clear();
		bool try_push(const Response &p_response);
		bool try_pop(Response &r_response);
		void complete(const Response &p_response);
		int size() const;
	};

	struct NetworkPeer : RefCounted {
		Ref<StreamPeerTCP> connection;

		uint8_t request_buffer[MAX_BUFFER_SIZE];
		int request_position = 0;
		bool has_header = false;
		int content_length = 0;

		struct QueuedResponse {
			Response response;
			CharString encoded;

			QueuedResponse(const Response &p_response) :
					response(p_response),
					encoded(p_response.message.utf8()) {}
		};

		Vector<QueuedResponse> response_queue;
		int response_sent = 0;

		Error read_message(String &r_message, bool &r_complete);
		Error send_data(Vector<Response> &r_completed);
		void queue_response(const Response &p_response);
		void discard_responses(Vector<Response> &r_discarded);
	};

	Ref<TCPServer> server;
	HashMap<int, Ref<NetworkPeer>> peers;
	IncomingEventQueue incoming_events;
	OutgoingResponseQueue outgoing_responses;

	mutable Mutex disconnect_mutex;
	HashSet<int> disconnect_requests;
	SafeFlag accepting;
	int next_client_id = 0;

	Error _accept_connection();
	void _disconnect_peer(int p_client_id);
	void _drain_disconnect_requests(HashSet<int> &r_disconnects);
	void _drain_outgoing_responses();

public:
	explicit GDScriptLanguageTransport(int p_message_capacity = DEFAULT_PENDING_MESSAGES, int p_response_capacity = DEFAULT_PENDING_RESPONSES, uint64_t p_message_byte_capacity = DEFAULT_PENDING_MESSAGE_BYTES, uint64_t p_response_byte_capacity = DEFAULT_PENDING_RESPONSE_BYTES);
	~GDScriptLanguageTransport();

	Error start(int p_port, const IPAddress &p_bind_ip);
	void begin_shutdown();
	void stop();
	void poll_network(int p_limit_usec);

	bool pop_event(Event &r_event);
	bool queue_response(int p_client_id, const String &p_response);
	void request_disconnect(int p_client_id);

	bool is_accepting() const { return accepting.is_set(); }
	int get_pending_event_count() const { return incoming_events.size(); }
	int get_pending_response_count() const { return outgoing_responses.size(); }
};
