/**************************************************************************/
/*  gdscript_language_transport.cpp                                       */
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

#include "gdscript_language_transport.h"

#include "core/os/os.h"

GDScriptLanguageTransport::IncomingEventQueue::IncomingEventQueue(int p_message_capacity, uint64_t p_message_byte_capacity) {
	message_capacity = MAX(1, p_message_capacity);
	message_byte_capacity = p_message_byte_capacity > 0 ? p_message_byte_capacity : 1;
	control_capacity = MAX_CLIENTS * 2;
}

void GDScriptLanguageTransport::IncomingEventQueue::open() {
	MutexLock lock(mutex);
	events.clear();
	message_count = 0;
	message_bytes = 0;
	control_count = 0;
	accepting = true;
}

void GDScriptLanguageTransport::IncomingEventQueue::close_and_clear() {
	MutexLock lock(mutex);
	accepting = false;
	events.clear();
	message_count = 0;
	message_bytes = 0;
	control_count = 0;
}

bool GDScriptLanguageTransport::IncomingEventQueue::can_accept_client(int p_active_clients) const {
	MutexLock lock(mutex);
	return accepting && control_count + p_active_clients + 2 <= control_capacity;
}

bool GDScriptLanguageTransport::IncomingEventQueue::try_push(const Event &p_event) {
	MutexLock lock(mutex);
	if (!accepting) {
		return false;
	}

	if (p_event.type == EVENT_MESSAGE) {
		const uint64_t event_bytes = p_event.message.length() * sizeof(char32_t);
		if (message_count >= message_capacity || event_bytes > message_byte_capacity - message_bytes) {
			return false;
		}
		message_count++;
		message_bytes += event_bytes;
	} else {
		if (control_count >= control_capacity) {
			return false;
		}
		control_count++;
	}

	events.push_back(p_event);
	return true;
}

bool GDScriptLanguageTransport::IncomingEventQueue::try_pop(Event &r_event) {
	MutexLock lock(mutex);
	if (events.is_empty()) {
		return false;
	}

	r_event = events.front()->get();
	events.pop_front();
	if (r_event.type == EVENT_MESSAGE) {
		message_count--;
		message_bytes -= r_event.message.length() * sizeof(char32_t);
	} else {
		control_count--;
	}
	return true;
}

int GDScriptLanguageTransport::IncomingEventQueue::size() const {
	MutexLock lock(mutex);
	return events.size();
}

GDScriptLanguageTransport::OutgoingResponseQueue::OutgoingResponseQueue(int p_capacity, uint64_t p_byte_capacity) {
	capacity = MAX(1, p_capacity);
	byte_capacity = p_byte_capacity > 0 ? p_byte_capacity : 1;
}

void GDScriptLanguageTransport::OutgoingResponseQueue::open() {
	MutexLock lock(mutex);
	responses.clear();
	response_count = 0;
	response_bytes = 0;
	accepting = true;
}

void GDScriptLanguageTransport::OutgoingResponseQueue::close_and_clear() {
	MutexLock lock(mutex);
	accepting = false;
	responses.clear();
	response_count = 0;
	response_bytes = 0;
}

bool GDScriptLanguageTransport::OutgoingResponseQueue::try_push(const Response &p_response) {
	MutexLock lock(mutex);
	const uint64_t response_size = p_response.message.length() * sizeof(char32_t);
	if (!accepting || response_count >= capacity || response_size > byte_capacity - response_bytes) {
		return false;
	}
	responses.push_back(p_response);
	response_count++;
	response_bytes += response_size;
	return true;
}

bool GDScriptLanguageTransport::OutgoingResponseQueue::try_pop(Response &r_response) {
	MutexLock lock(mutex);
	if (responses.is_empty()) {
		return false;
	}
	r_response = responses.front()->get();
	responses.pop_front();
	return true;
}

void GDScriptLanguageTransport::OutgoingResponseQueue::complete(const Response &p_response) {
	MutexLock lock(mutex);
	const uint64_t response_size = p_response.message.length() * sizeof(char32_t);
	if (response_count > 0 && response_bytes >= response_size) {
		response_count--;
		response_bytes -= response_size;
	}
}

int GDScriptLanguageTransport::OutgoingResponseQueue::size() const {
	MutexLock lock(mutex);
	return response_count;
}

Error GDScriptLanguageTransport::NetworkPeer::read_message(String &r_message, bool &r_complete) {
	r_message = String();
	r_complete = false;
	int read = 0;

	if (!has_header) {
		while (true) {
			ERR_FAIL_COND_V_MSG(request_position >= MAX_BUFFER_SIZE, ERR_OUT_OF_MEMORY, "LSP request header is too large.");
			const Error error = connection->get_partial_data(&request_buffer[request_position], 1, read);
			if (error != OK) {
				return error;
			}
			if (read != 1) {
				return ERR_BUSY;
			}

			char *header_data = reinterpret_cast<char *>(request_buffer);
			const int header_position = request_position;
			if (header_position > 3 && header_data[header_position] == '\n' && header_data[header_position - 1] == '\r' && header_data[header_position - 2] == '\n' && header_data[header_position - 3] == '\r') {
				header_data[header_position - 3] = '\0';
				const String header = String::utf8(header_data);
				content_length = header.substr(16).to_int();
				ERR_FAIL_COND_V_MSG(content_length < 0 || content_length > MAX_BUFFER_SIZE, ERR_OUT_OF_MEMORY, "LSP request content is too large.");
				has_header = true;
				request_position = 0;
				break;
			}
			request_position++;
		}
	}

	while (request_position < content_length) {
		const Error error = connection->get_partial_data(&request_buffer[request_position], 1, read);
		if (error != OK) {
			return error;
		}
		if (read != 1) {
			return ERR_BUSY;
		}
		request_position++;
	}

	r_message = String::utf8(reinterpret_cast<const char *>(request_buffer), request_position);
	r_complete = true;
	request_position = 0;
	has_header = false;
	content_length = 0;
	return OK;
}

Error GDScriptLanguageTransport::NetworkPeer::send_data(Vector<Response> &r_completed) {
	int sent = 0;
	while (!response_queue.is_empty()) {
		const QueuedResponse &queued_response = response_queue[0];
		if (response_sent < queued_response.encoded.size() - 1) {
			const Error error = connection->put_partial_data(reinterpret_cast<const uint8_t *>(queued_response.encoded.get_data()) + response_sent, queued_response.encoded.size() - response_sent - 1, sent);
			if (error != OK) {
				return error;
			}
			if (sent == 0) {
				return ERR_BUSY;
			}
			response_sent += sent;
		}

		if (response_sent >= queued_response.encoded.size() - 1) {
			response_sent = 0;
			r_completed.push_back(queued_response.response);
			response_queue.remove_at(0);
		}
	}
	return OK;
}

void GDScriptLanguageTransport::NetworkPeer::queue_response(const Response &p_response) {
	response_queue.push_back(QueuedResponse(p_response));
}

void GDScriptLanguageTransport::NetworkPeer::discard_responses(Vector<Response> &r_discarded) {
	for (const QueuedResponse &queued_response : response_queue) {
		r_discarded.push_back(queued_response.response);
	}
	response_queue.clear();
	response_sent = 0;
}

GDScriptLanguageTransport::GDScriptLanguageTransport(int p_message_capacity, int p_response_capacity, uint64_t p_message_byte_capacity, uint64_t p_response_byte_capacity) :
		incoming_events(p_message_capacity, p_message_byte_capacity),
		outgoing_responses(p_response_capacity, p_response_byte_capacity) {
	server.instantiate();
}

GDScriptLanguageTransport::~GDScriptLanguageTransport() {
	stop();
}

Error GDScriptLanguageTransport::start(int p_port, const IPAddress &p_bind_ip) {
	ERR_FAIL_COND_V(server->is_listening(), ERR_ALREADY_IN_USE);
	const Error error = server->listen(p_port, p_bind_ip);
	if (error != OK) {
		return error;
	}

	next_client_id = 0;
	incoming_events.open();
	outgoing_responses.open();
	accepting.set();
	return OK;
}

void GDScriptLanguageTransport::begin_shutdown() {
	accepting.clear();
	incoming_events.close_and_clear();
	outgoing_responses.close_and_clear();
	MutexLock lock(disconnect_mutex);
	disconnect_requests.clear();
}

void GDScriptLanguageTransport::stop() {
	begin_shutdown();
	for (const KeyValue<int, Ref<NetworkPeer>> &entry : peers) {
		if (entry.value.is_valid() && entry.value->connection.is_valid()) {
			entry.value->connection->disconnect_from_host();
		}
	}
	peers.clear();
	server->stop();
}

Error GDScriptLanguageTransport::_accept_connection() {
	Ref<StreamPeerTCP> connection = server->take_connection();
	ERR_FAIL_COND_V(connection.is_null(), ERR_CONNECTION_ERROR);
	if (!accepting.is_set() || peers.size() >= MAX_CLIENTS || !incoming_events.can_accept_client(peers.size())) {
		connection->disconnect_from_host();
		return ERR_BUSY;
	}

	const int client_id = next_client_id++;
	Ref<NetworkPeer> peer = memnew(NetworkPeer);
	peer->connection = connection;
	if (!incoming_events.try_push(Event(EVENT_CONNECTED, client_id))) {
		connection->disconnect_from_host();
		return ERR_BUSY;
	}
	peers.insert(client_id, peer);
	return OK;
}

void GDScriptLanguageTransport::_disconnect_peer(int p_client_id) {
	Ref<NetworkPeer> *peer = peers.getptr(p_client_id);
	if (!peer) {
		return;
	}
	if ((*peer).is_valid() && (*peer)->connection.is_valid()) {
		(*peer)->connection->disconnect_from_host();
	}
	Vector<Response> discarded_responses;
	if ((*peer).is_valid()) {
		(*peer)->discard_responses(discarded_responses);
	}
	for (const Response &response : discarded_responses) {
		outgoing_responses.complete(response);
	}
	if (accepting.is_set() && !incoming_events.try_push(Event(EVENT_DISCONNECTED, p_client_id))) {
		ERR_PRINT("LSP transport control queue capacity invariant was violated.");
	}
	peers.erase(p_client_id);
}

void GDScriptLanguageTransport::_drain_disconnect_requests(HashSet<int> &r_disconnects) {
	MutexLock lock(disconnect_mutex);
	for (const int client_id : disconnect_requests) {
		r_disconnects.insert(client_id);
	}
	disconnect_requests.clear();
}

void GDScriptLanguageTransport::_drain_outgoing_responses() {
	Response response;
	while (outgoing_responses.try_pop(response)) {
		Ref<NetworkPeer> *peer = peers.getptr(response.client_id);
		if (peer && (*peer).is_valid()) {
			(*peer)->queue_response(response);
		} else {
			outgoing_responses.complete(response);
		}
	}
}

void GDScriptLanguageTransport::poll_network(int p_limit_usec) {
	if (!accepting.is_set()) {
		return;
	}

	const uint64_t target_ticks = OS::get_singleton()->get_ticks_usec() + MAX(0, p_limit_usec);
	if (server->is_listening() && server->is_connection_available()) {
		_accept_connection();
	}

	HashSet<int> disconnects;
	_drain_disconnect_requests(disconnects);
	_drain_outgoing_responses();

	for (const KeyValue<int, Ref<NetworkPeer>> &entry : peers) {
		const int client_id = entry.key;
		Ref<NetworkPeer> peer = entry.value;
		if (disconnects.has(client_id)) {
			continue;
		}

		peer->connection->poll();
		const StreamPeerTCP::Status status = peer->connection->get_status();
		if (status == StreamPeerTCP::STATUS_NONE || status == StreamPeerTCP::STATUS_ERROR) {
			disconnects.insert(client_id);
			continue;
		}

		Error error = OK;
		while (peer->connection->get_available_bytes() > 0) {
			String message;
			bool complete = false;
			error = peer->read_message(message, complete);
			if (complete && !incoming_events.try_push(Event(EVENT_MESSAGE, client_id, message))) {
				disconnects.insert(client_id);
				break;
			}
			if (error != OK || OS::get_singleton()->get_ticks_usec() >= target_ticks) {
				break;
			}
		}

		if (error != OK && error != ERR_BUSY) {
			disconnects.insert(client_id);
			continue;
		}

		Vector<Response> completed_responses;
		error = peer->send_data(completed_responses);
		for (const Response &response : completed_responses) {
			outgoing_responses.complete(response);
		}
		if (error != OK && error != ERR_BUSY) {
			disconnects.insert(client_id);
		}
	}

	for (const int client_id : disconnects) {
		_disconnect_peer(client_id);
	}
}

bool GDScriptLanguageTransport::pop_event(Event &r_event) {
	return incoming_events.try_pop(r_event);
}

bool GDScriptLanguageTransport::queue_response(int p_client_id, const String &p_response) {
	return outgoing_responses.try_push(Response(p_client_id, p_response));
}

void GDScriptLanguageTransport::request_disconnect(int p_client_id) {
	MutexLock lock(disconnect_mutex);
	if (!accepting.is_set()) {
		return;
	}
	disconnect_requests.insert(p_client_id);
}
