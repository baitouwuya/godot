/**************************************************************************/
/*  mcp_trace_service.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "mcp_trace_service.h"

#include "core/crypto/crypto_core.h"
#include "core/io/json.h"
#include "core/os/os.h"

MCPTraceService::~MCPTraceService() {
	stop();
}

void MCPTraceService::_set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

bool MCPTraceService::_is_secret_key(const String &p_key) {
	const String key = p_key.to_lower().replace("-", "_");
	return key.contains("auth") || key.contains("token") || key.contains("secret") || key.contains("password");
}

bool MCPTraceService::_is_session_key(const String &p_key) {
	const String key = p_key.to_lower().replace("-", "_");
	return key == "session" || key == "session_id" || key == "sessionid";
}

bool MCPTraceService::_is_path_key(const String &p_key) {
	const String key = p_key.to_lower();
	return key.contains("path") || key.contains("file") || key == "cwd" || key.contains("binary") || key.contains("executable");
}

String MCPTraceService::_session_hash(const String &p_session_id) {
	return p_session_id.is_empty() ? String() : p_session_id.sha256_text().left(16);
}

String MCPTraceService::_sanitize_path(const String &p_path) {
	const String normalized = p_path.replace_char('\\', '/').simplify_path();
	return normalized.begins_with("res://") ? normalized : normalized.get_file();
}

String MCPTraceService::_bounded_text(const String &p_value, int p_max_characters) {
	return p_value.length() <= p_max_characters ? p_value : p_value.left(p_max_characters) + "<truncated>";
}

Dictionary MCPTraceService::_sanitize_dictionary(const Dictionary &p_value, int p_depth) {
	Dictionary result;
	const Array keys = p_value.keys();
	const int count = MIN(keys.size(), MAX_CONTAINER_ENTRIES);
	for (int i = 0; i < count; i++) {
		const Variant &key_value = keys[i];
		if (key_value.get_type() != Variant::STRING && key_value.get_type() != Variant::STRING_NAME) {
			continue;
		}
		const String key = _bounded_text(String(key_value), 256);
		if (_is_secret_key(key)) {
			result[key] = "<redacted>";
		} else if (_is_session_key(key)) {
			result[key] = _session_hash(String(p_value[key_value]));
		} else {
			result[key] = _sanitize_variant(p_value[key_value], key, p_depth + 1);
		}
	}
	if (keys.size() > MAX_CONTAINER_ENTRIES) {
		result["_truncated"] = true;
		result["_originalEntries"] = keys.size();
	}
	return result;
}

Variant MCPTraceService::_sanitize_variant(const Variant &p_value, const String &p_key_hint, int p_depth) {
	if (p_depth > MAX_DEPTH) {
		Dictionary omitted;
		omitted["_truncated"] = true;
		omitted["reason"] = "maxDepth";
		return omitted;
	}
	switch (p_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
			return p_value;
		case Variant::STRING:
		case Variant::STRING_NAME: {
			const String text = p_value;
			if (_is_path_key(p_key_hint)) {
				return _sanitize_path(text);
			}
			return _bounded_text(text, MAX_STRING_CHARACTERS);
		}
		case Variant::DICTIONARY:
			return _sanitize_dictionary(p_value, p_depth);
		case Variant::ARRAY: {
			const Array source = p_value;
			Array result;
			const int count = MIN(source.size(), MAX_CONTAINER_ENTRIES);
			for (int i = 0; i < count; i++) {
				result.push_back(_sanitize_variant(source[i], p_key_hint, p_depth + 1));
			}
			if (source.size() > MAX_CONTAINER_ENTRIES) {
				Dictionary marker;
				marker["_truncated"] = true;
				marker["_originalEntries"] = source.size();
				result.push_back(marker);
			}
			return result;
		}
		case Variant::PACKED_BYTE_ARRAY: {
			const PackedByteArray bytes = p_value;
			uint8_t hash[32] = {};
			CryptoCore::sha256(bytes.ptr(), bytes.size(), hash);
			Dictionary descriptor;
			descriptor["type"] = "bytes";
			descriptor["bytes"] = bytes.size();
			descriptor["sha256"] = String::hex_encode_buffer(hash, 32);
			return descriptor;
		}
		default: {
			Dictionary omitted;
			omitted["type"] = Variant::get_type_name(p_value.get_type());
			omitted["omitted"] = true;
			return omitted;
		}
	}
}

Dictionary MCPTraceService::_bounded_dictionary(const Dictionary &p_value) {
	Dictionary sanitized = _sanitize_dictionary(p_value);
	const String serialized = JSON::stringify(sanitized);
	const uint64_t bytes = serialized.to_utf8_buffer().size();
	if (bytes <= MAX_STRUCTURED_FIELD_BYTES) {
		return sanitized;
	}
	Dictionary summary;
	summary["_truncated"] = true;
	summary["originalBytes"] = int64_t(bytes);
	summary["sha256"] = serialized.sha256_text();
	return summary;
}

Error MCPTraceService::start(const Dictionary &p_project_metadata, const String &p_process_mode, const String &p_root_directory_override, String *r_error) {
	_set_error(r_error, String());
	MutexLock lock(mutex);
	if (started) {
		writer.close();
		started = false;
	}
	StructuredTraceWriter::Options options;
	options.root_directory_override = p_root_directory_override;
	Dictionary process;
	process["pid"] = int64_t(OS::get_singleton()->get_process_id());
	process["mode"] = _bounded_text(p_process_mode, 128);
	process["binaryPath"] = OS::get_singleton()->get_executable_path().get_file();
	process["cwd"] = OS::get_singleton()->get_cwd().get_file();
	const Error error = writer.open(_bounded_dictionary(p_project_metadata), process, options, r_error);
	started = error == OK;
	return error;
}

Error MCPTraceService::_record(const String &p_category, const String &p_event, const String &p_severity, const String &p_correlation_id, const Dictionary &p_data, const Dictionary &p_error, String *r_error) {
	_set_error(r_error, String());
	MutexLock lock(mutex);
	if (!started) {
		return OK;
	}
	return writer.record(p_category, _bounded_text(p_event, 256), p_severity, _bounded_text(p_correlation_id, 256), _bounded_dictionary(p_data), Dictionary(), _bounded_dictionary(p_error), r_error);
}

Error MCPTraceService::record_mcp_request(const String &p_method, const String &p_session_id, const String &p_correlation_id, const Dictionary &p_arguments, const Dictionary &p_result, const Dictionary &p_error, String *r_error) {
	if (!is_started()) {
		_set_error(r_error, String());
		return OK;
	}
	Dictionary data;
	data["method"] = _bounded_text(p_method, 256);
	data["sessionHash"] = _session_hash(p_session_id);
	data["arguments"] = _bounded_dictionary(p_arguments);
	data["result"] = _bounded_dictionary(p_result);
	return _record("mcp", "request", p_error.is_empty() ? "info" : "error", p_correlation_id, data, p_error, r_error);
}

Error MCPTraceService::record_tool_call(const String &p_tool_name, const String &p_session_id, const String &p_correlation_id, const Dictionary &p_arguments, const Dictionary &p_result, const Dictionary &p_error, String *r_error) {
	if (!is_started()) {
		_set_error(r_error, String());
		return OK;
	}
	Dictionary data;
	data["tool"] = _bounded_text(p_tool_name, 256);
	data["sessionHash"] = _session_hash(p_session_id);
	data["arguments"] = _bounded_dictionary(p_arguments);
	data["result"] = _bounded_dictionary(p_result);
	return _record("tool", "call", p_error.is_empty() ? "info" : "error", p_correlation_id, data, p_error, r_error);
}

Error MCPTraceService::record_job_event(const String &p_job_id, const String &p_event, const String &p_severity, const Dictionary &p_data, const Dictionary &p_payloads, const Dictionary &p_error, String *r_error) {
	if (!is_started()) {
		_set_error(r_error, String());
		return OK;
	}
	Dictionary data;
	data["jobId"] = _bounded_text(p_job_id, 256);
	data["data"] = _bounded_dictionary(p_data);
	data["payloads"] = _bounded_dictionary(p_payloads);
	return _record("job", p_event, p_severity, p_job_id, data, p_error, r_error);
}

void MCPTraceService::stop() {
	MutexLock lock(mutex);
	if (!started) {
		return;
	}
	writer.close();
	started = false;
}

bool MCPTraceService::is_started() const {
	MutexLock lock(mutex);
	return started;
}

String MCPTraceService::get_session_directory() const {
	MutexLock lock(mutex);
	return writer.get_session_directory();
}

Dictionary MCPTraceService::get_safe_reference(const String &p_correlation_id) const {
	MutexLock lock(mutex);
	Dictionary reference;
	reference["available"] = writer.is_open();
	const String writer_session_id = writer.get_session_id();
	reference["traceId"] = writer_session_id.is_empty() ? String("active") : writer_session_id;
	reference["manifest"] = "manifest.json";
	if (!p_correlation_id.is_empty()) {
		reference["correlationId"] = _bounded_text(p_correlation_id, 256);
	}
	return reference;
}
