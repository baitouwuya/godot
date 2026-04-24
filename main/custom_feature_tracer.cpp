/**************************************************************************/
/*  custom_feature_tracer.cpp                                             */
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

#include "custom_feature_tracer.h"

#include "core/io/json.h"
#include "core/object/script_backtrace.h"
#include "core/os/os.h"

static CustomFeatureTracer *custom_feature_tracer_singleton = nullptr;
static bool custom_feature_trace_logger_installed = false;
static thread_local String custom_feature_trace_tls_feature;
static thread_local String custom_feature_trace_tls_correlation_id;

class CustomFeatureTraceLogger : public Logger {
public:
	virtual void logv(const char *p_format, va_list p_list, bool p_err) override {
	}

	virtual void log_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, bool p_editor_notify, ErrorType p_type, const Vector<Ref<ScriptBacktrace>> &p_script_backtraces) override {
		CustomFeatureTracer *tracer = CustomFeatureTracer::get_singleton();
		if (!tracer) {
			return;
		}
		tracer->capture_logger_error(p_function, p_file, p_line, p_code, p_rationale, p_type, p_script_backtraces);
	}
};

CustomFeatureTracer *CustomFeatureTracer::get_singleton() {
	return custom_feature_tracer_singleton;
}

Error CustomFeatureTracer::initialize_singleton(const StartupOptions &p_options, String &r_error) {
	r_error = String();

	if (custom_feature_tracer_singleton) {
		update_process_context(p_options.project_path, p_options.process_mode);
		return OK;
	}

	custom_feature_tracer_singleton = memnew(CustomFeatureTracer);

	CustomFeatureTraceWriter::Options writer_options;
	writer_options.base_dir_override = p_options.base_dir_override;

	CustomFeatureTraceWriter::SessionInfo session_info;
	session_info.binary_path = p_options.binary_path;
	session_info.cwd = p_options.cwd;
	session_info.project_path = p_options.project_path;
	session_info.process_mode = p_options.process_mode;
	session_info.pid = p_options.pid;

	const Error err = custom_feature_tracer_singleton->writer.initialize(session_info, writer_options, r_error);
	if (err != OK) {
		memdelete(custom_feature_tracer_singleton);
		custom_feature_tracer_singleton = nullptr;
		return err;
	}

	if (!custom_feature_trace_logger_installed) {
		OS::get_singleton()->add_logger(memnew(CustomFeatureTraceLogger));
		custom_feature_trace_logger_installed = true;
	}

	return OK;
}

void CustomFeatureTracer::shutdown_singleton() {
	if (!custom_feature_tracer_singleton) {
		return;
	}

	custom_feature_tracer_singleton->writer.finalize();
	memdelete(custom_feature_tracer_singleton);
	custom_feature_tracer_singleton = nullptr;
	custom_feature_trace_tls_feature = String();
	custom_feature_trace_tls_correlation_id = String();
}

bool CustomFeatureTracer::has_singleton() {
	return custom_feature_tracer_singleton != nullptr;
}

String CustomFeatureTracer::get_current_feature() {
	return _get_thread_feature();
}

String CustomFeatureTracer::get_current_correlation_id() {
	return _get_thread_correlation_id();
}

void CustomFeatureTracer::update_process_context(const String &p_project_path, const String &p_process_mode) {
	if (!custom_feature_tracer_singleton) {
		return;
	}
	custom_feature_tracer_singleton->writer.set_project_path(p_project_path);
	custom_feature_tracer_singleton->writer.set_process_mode(p_process_mode);
}

String CustomFeatureTracer::next_correlation_id(const String &p_feature) {
	MutexLock lock(mutex);
	return vformat("%s-%d", p_feature, correlation_sequence++);
}

String CustomFeatureTracer::correlation_id_from_external_id(const String &p_feature, const Variant &p_external_id) {
	if (p_external_id.get_type() != Variant::NIL) {
		if (p_external_id.get_type() == Variant::STRING) {
			return p_external_id;
		}
		return JSON::stringify(p_external_id);
	}
	return next_correlation_id(p_feature);
}

void CustomFeatureTracer::record_event(const String &p_feature, const String &p_event, const String &p_severity, const String &p_correlation_id, const Dictionary &p_data, const Dictionary &p_payloads, const Dictionary &p_error) {
	MutexLock lock(mutex);
	CustomFeatureTraceWriter::EventRecord event;
	event.feature = p_feature;
	event.event = p_event;
	event.severity = p_severity;
	event.correlation_id = p_correlation_id;
	event.data = p_data;
	event.payloads = p_payloads;
	event.error = p_error;
	String write_error;
	writer.write_event(event, write_error);
}

void CustomFeatureTracer::record_error_event(const String &p_feature, const String &p_event, const String &p_correlation_id, const String &p_code, const String &p_message, const Dictionary &p_data, const Dictionary &p_payloads) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	record_event(p_feature, p_event, "error", p_correlation_id, p_data, p_payloads, error);
}

bool CustomFeatureTracer::add_text_payload(Dictionary &r_payloads, const String &p_name, const String &p_mime, const String &p_text) {
	MutexLock lock(mutex);
	CustomFeatureTraceWriter::PayloadDescriptor descriptor;
	String error;
	if (writer.make_text_payload(p_name, p_mime, p_text, descriptor, error) != OK) {
		return false;
	}
	r_payloads[p_name] = descriptor.to_dictionary();
	return true;
}

bool CustomFeatureTracer::add_json_payload(Dictionary &r_payloads, const String &p_name, const Variant &p_value) {
	MutexLock lock(mutex);
	CustomFeatureTraceWriter::PayloadDescriptor descriptor;
	String error;
	if (writer.make_json_payload(p_name, p_value, descriptor, error) != OK) {
		return false;
	}
	r_payloads[p_name] = descriptor.to_dictionary();
	return true;
}

bool CustomFeatureTracer::add_existing_file_payload(Dictionary &r_payloads, const String &p_name, const String &p_mime, const String &p_path) {
	MutexLock lock(mutex);
	CustomFeatureTraceWriter::PayloadDescriptor descriptor;
	String error;
	if (writer.reference_existing_file(p_mime, p_path, descriptor, error) != OK) {
		return false;
	}
	r_payloads[p_name] = descriptor.to_dictionary();
	return true;
}

void CustomFeatureTracer::capture_logger_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, Logger::ErrorType p_type, const Vector<Ref<ScriptBacktrace>> &p_script_backtraces) {
	if (!writer.is_active()) {
		return;
	}

	const String feature = _get_thread_feature();
	if (feature.is_empty()) {
		return;
	}

	Dictionary data;
	data["errorType"] = String(Logger::error_type_string(p_type));
	data["function"] = String(p_function);
	data["file"] = String(p_file);
	data["line"] = p_line;
	data["code"] = String(p_code);
	data["rationale"] = String(p_rationale ? p_rationale : "");

	Dictionary payloads;
	if (!p_script_backtraces.is_empty()) {
		String backtraces_text;
		for (int i = 0; i < p_script_backtraces.size(); i++) {
			if (p_script_backtraces[i].is_null() || p_script_backtraces[i]->is_empty()) {
				continue;
			}
			if (!backtraces_text.is_empty()) {
				backtraces_text += "\n\n";
			}
			backtraces_text += p_script_backtraces[i]->format(3);
		}
		if (!backtraces_text.is_empty()) {
			add_text_payload(payloads, "scriptBacktrace", "text/plain", backtraces_text);
		}
	}

	Dictionary error;
	error["code"] = "engine_log_error";
	error["message"] = String(p_rationale && *p_rationale ? p_rationale : p_code);
	record_event(feature, "engine_log_error", p_type == Logger::ERR_WARNING ? "warning" : "error", _get_thread_correlation_id(), data, payloads, error);
}

CustomFeatureTracer::ScopedEventContext::ScopedEventContext(const String &p_feature, const String &p_correlation_id) {
	previous_feature = CustomFeatureTracer::_get_thread_feature();
	previous_correlation_id = CustomFeatureTracer::_get_thread_correlation_id();
	CustomFeatureTracer::_set_thread_context(p_feature, p_correlation_id);
	active = true;
}

CustomFeatureTracer::ScopedEventContext::~ScopedEventContext() {
	if (!active) {
		return;
	}
	CustomFeatureTracer::_set_thread_context(previous_feature, previous_correlation_id);
}

CustomFeatureTracer::ScopedEventContext::ScopedEventContext(ScopedEventContext &&p_other) noexcept {
	active = p_other.active;
	previous_feature = p_other.previous_feature;
	previous_correlation_id = p_other.previous_correlation_id;
	p_other.active = false;
}

CustomFeatureTracer::ScopedEventContext &CustomFeatureTracer::ScopedEventContext::operator=(ScopedEventContext &&p_other) noexcept {
	if (this == &p_other) {
		return *this;
	}
	if (active) {
		CustomFeatureTracer::_set_thread_context(previous_feature, previous_correlation_id);
	}
	active = p_other.active;
	previous_feature = p_other.previous_feature;
	previous_correlation_id = p_other.previous_correlation_id;
	p_other.active = false;
	return *this;
}

void CustomFeatureTracer::_set_thread_context(const String &p_feature, const String &p_correlation_id) {
	custom_feature_trace_tls_feature = p_feature;
	custom_feature_trace_tls_correlation_id = p_correlation_id;
}

String CustomFeatureTracer::_get_thread_feature() {
	return custom_feature_trace_tls_feature;
}

String CustomFeatureTracer::_get_thread_correlation_id() {
	return custom_feature_trace_tls_correlation_id;
}

#ifdef TESTS_ENABLED
String CustomFeatureTracer::get_session_dir_for_tests() const {
	return writer.get_session_dir();
}

String CustomFeatureTracer::get_root_dir_for_tests() const {
	return writer.get_root_dir();
}
#endif
