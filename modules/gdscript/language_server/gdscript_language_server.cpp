/**************************************************************************/
/*  gdscript_language_server.cpp                                          */
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

#include "gdscript_language_server.h"

#include "gdscript_language_protocol.h"

#include "core/os/os.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"

int GDScriptLanguageServer::port_override = -1;

static void _log_server_message(const String &p_message) {
	EditorNode *editor_node = EditorNode::get_singleton();
	if (!editor_node || !editor_node->is_editor_ready()) {
		return;
	}
	EditorLog *editor_log = EditorNode::get_log();
	if (editor_log) {
		editor_log->add_message(p_message, EditorLog::MSG_TYPE_EDITOR);
	}
}

GDScriptLanguageServer::GDScriptLanguageServer() {
	set_process_internal(true);
}

void GDScriptLanguageServer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_EXIT_TREE: {
			stop();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!start_attempted && EditorNode::get_singleton()->is_editor_ready()) {
				start_attempted = true;
				start();
			}

			if (started) {
				GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
				if (!use_thread) {
					protocol->poll_network(poll_limit_usec);
				}
				protocol->poll_main_thread(poll_limit_usec);
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!EditorSettings::get_singleton()->check_changed_settings_in_group("network/language_server")) {
				break;
			}

			String remote_host = String(_EDITOR_GET("network/language_server/remote_host"));
			int remote_port = (GDScriptLanguageServer::port_override > -1) ? GDScriptLanguageServer::port_override : (int)_EDITOR_GET("network/language_server/remote_port");
			bool remote_use_thread = (bool)_EDITOR_GET("network/language_server/use_thread");
			int remote_poll_limit = (int)_EDITOR_GET("network/language_server/poll_limit_usec");
			if (remote_host != host || remote_port != port || remote_use_thread != use_thread || remote_poll_limit != poll_limit_usec) {
				stop();
				start();
			}
		} break;
	}
}

void GDScriptLanguageServer::thread_main(void *p_userdata) {
	GDScriptLanguageServer *self = static_cast<GDScriptLanguageServer *>(p_userdata);
	while (self->thread_running.is_set()) {
		// Poll 20 times per second
		GDScriptLanguageProtocol::get_singleton()->poll_network(self->poll_limit_usec);
		OS::get_singleton()->delay_usec(50000);
	}
}

void GDScriptLanguageServer::start() {
	host = String(_EDITOR_GET("network/language_server/remote_host"));
	port = (GDScriptLanguageServer::port_override > -1) ? GDScriptLanguageServer::port_override : (int)_EDITOR_GET("network/language_server/remote_port");
	use_thread = (bool)_EDITOR_GET("network/language_server/use_thread");
	poll_limit_usec = (int)_EDITOR_GET("network/language_server/poll_limit_usec");
	const Error status = GDScriptLanguageProtocol::get_singleton()->start(port, IPAddress(host));
	if (status != OK) {
		_log_server_message("--- Failed to start GDScript language server on port " + itos(port) + ": " + error_names[status] + " ---");
		return;
	}
	_log_server_message("--- GDScript language server started on port " + itos(port) + " ---");
	if (use_thread) {
		thread_running.set();
		thread.start(GDScriptLanguageServer::thread_main, this);
	}
	set_process_internal(true);
	started = true;
}

void GDScriptLanguageServer::stop() {
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	protocol->begin_shutdown();
	thread_running.clear();
	if (thread.is_started()) {
		thread.wait_to_finish();
	}
	protocol->stop();
	started = false;
	set_process_internal(true);
	_log_server_message("--- GDScript language server stopped ---");
}

void register_lsp_types() {
	GDREGISTER_CLASS(GDScriptLanguageProtocol);
	GDREGISTER_CLASS(GDScriptTextDocument);
	GDREGISTER_CLASS(GDScriptWorkspace);
}
