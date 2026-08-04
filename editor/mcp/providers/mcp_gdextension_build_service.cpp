/**************************************************************************/
/*  mcp_gdextension_build_service.cpp                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "mcp_gdextension_build_service.h"

#include "mcp_path_utils.h"
#include "mcp_gdextension_profile_registry.h"
#include "mcp_project_revision.h"
#include "mcp_tool_utils.h"

#include "core/config/project_settings.h"
#include "core/extension/gdextension_library_loader.h"
#include "core/extension/gdextension_manager.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "editor/run/editor_run_bar.h"

namespace {

static constexpr int MAX_BUILD_RECORDS = 64;
static constexpr int MAX_OUTPUT_CHARACTERS = 16 * 1024;
static constexpr int MAX_DIAGNOSTICS = 128;
static constexpr int DEFAULT_TIMEOUT_MS = 120000;
static constexpr int MAX_TIMEOUT_MS = 600000;

static uint64_t _unix_time_ms() {
	return uint64_t(OS::get_singleton()->get_unix_time() * 1000.0);
}

static Dictionary _error(const String &p_code, const String &p_message, const Dictionary &p_details = Dictionary()) {
	return MCPToolUtils::make_error_result(p_code, p_message, p_details);
}

static String _session_id(const Dictionary &p_context, Dictionary &r_error) {
	String session_id;
	String session_error;
	if (MCPToolUtils::parse_session_id(p_context, session_id, &session_error) != OK) {
		r_error = _error("INVALID_SESSION", session_error);
		return String();
	}
	return session_id.is_empty() ? String("anonymous") : session_id;
}

static String _bounded_tail(const String &p_text) {
	if (p_text.length() <= MAX_OUTPUT_CHARACTERS) {
		return p_text;
	}
	return p_text.substr(p_text.length() - MAX_OUTPUT_CHARACTERS);
}

static String _find_sconstruct_root(const String &p_start_directory, const String &p_project_root) {
	String directory = p_start_directory.simplify_path();
	const String project_root = p_project_root.simplify_path();
	while (!directory.is_empty()) {
		if (FileAccess::exists(directory.path_join("SConstruct"))) {
			return directory;
		}
		if (directory == project_root) {
			break;
		}
		const String parent = directory.get_base_dir();
		if (parent == directory || !parent.begins_with(project_root)) {
			break;
		}
		directory = parent;
	}
	return String();
}

static void _append_diagnostic(Array &r_diagnostics, const String &p_line, const String &p_severity) {
	if (r_diagnostics.size() >= MAX_DIAGNOSTICS) {
		return;
	}
	Dictionary diagnostic;
	diagnostic["severity"] = p_severity;
	diagnostic["code"] = "";
	diagnostic["message"] = p_line.strip_edges();
	diagnostic["file"] = "";
	diagnostic["line"] = 0;
	diagnostic["column"] = 0;

	const PackedStringArray parts = p_line.split(":", true, 4);
	if (parts.size() >= 3 && parts[1].strip_edges().is_valid_int()) {
		diagnostic["file"] = parts[0].strip_edges();
		diagnostic["line"] = parts[1].strip_edges().to_int();
		if (parts[2].strip_edges().is_valid_int()) {
			diagnostic["column"] = parts[2].strip_edges().to_int();
		}
	}
	r_diagnostics.push_back(diagnostic);
}

static Array _parse_diagnostics(const String &p_stdout, const String &p_stderr) {
	Array diagnostics;
	const PackedStringArray lines = (p_stdout + "\n" + p_stderr).split("\n", false);
	for (const String &line : lines) {
		const String lower = line.to_lower();
		if (lower.contains("fatal error:") || lower.contains("error:")) {
			_append_diagnostic(diagnostics, line, "error");
		} else if (lower.contains("warning:")) {
			_append_diagnostic(diagnostics, line, "warning");
		}
	}
	return diagnostics;
}

static String _load_status_name(GDExtensionManager::LoadStatus p_status) {
	switch (p_status) {
		case GDExtensionManager::LOAD_STATUS_OK:
			return "reloaded";
		case GDExtensionManager::LOAD_STATUS_ALREADY_LOADED:
			return "already_loaded";
		case GDExtensionManager::LOAD_STATUS_NOT_LOADED:
			return "not_loaded";
		case GDExtensionManager::LOAD_STATUS_NEEDS_RESTART:
			return "needs_restart";
		case GDExtensionManager::LOAD_STATUS_FAILED:
		default:
			return "reload_failed";
	}
}

} // namespace

Dictionary MCPGDExtensionBuildService::_result(const BuildRecord &p_job) const {
	Dictionary result;
	result["jobId"] = p_job.id;
	result["state"] = p_job.state;
	result["extensionPath"] = p_job.extension_path;
	result["libraryPath"] = p_job.artifact_path;
	result["profile"] = p_job.profile;
	result["projectRevision"] = p_job.project_revision;
	result["startedAtMs"] = int64_t(p_job.started_unix_ms);
	result["finishedAtMs"] = int64_t(p_job.finished_unix_ms);
	result["exitCode"] = p_job.exit_code;
	result["stdoutTail"] = p_job.stdout_tail;
	result["stderrTail"] = p_job.stderr_tail;
	result["diagnostics"] = p_job.diagnostics;
	result["artifactSha256"] = p_job.artifact_sha256;
	result["reloadStatus"] = p_job.reload_status;
	result["rollbackComplete"] = p_job.rollback_complete;
	if (!p_job.failure_code.is_empty()) {
		Dictionary failure;
		failure["code"] = p_job.failure_code;
		failure["message"] = p_job.failure_message;
		result["failure"] = failure;
	}
	return MCPToolUtils::make_success_result(result);
}

void MCPGDExtensionBuildService::_drain_output(BuildRecord &r_job) {
	auto drain = [](const Ref<FileAccess> &p_pipe, String &r_tail) {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return;
		}
		while (p_pipe->get_length() > 0) {
			const int64_t available = int64_t(MIN(p_pipe->get_length(), uint64_t(8192)));
			PackedByteArray bytes;
			bytes.resize(available);
			const uint64_t read = p_pipe->get_buffer(bytes.ptrw(), bytes.size());
			if (read == 0) {
				break;
			}
			r_tail.append_utf8((const char *)bytes.ptr(), read);
			r_tail = _bounded_tail(r_tail);
		}
	};
	drain(r_job.stdout_pipe, r_job.stdout_tail);
	drain(r_job.stderr_pipe, r_job.stderr_tail);
}

void MCPGDExtensionBuildService::_cleanup_backup(BuildRecord &r_job) {
	if (!r_job.backup_path.is_empty() && FileAccess::exists(r_job.backup_path)) {
		DirAccess::remove_absolute(r_job.backup_path);
	}
	r_job.backup_path = String();
}

void MCPGDExtensionBuildService::_rollback(BuildRecord &r_job) {
	r_job.rollback_complete = true;
	if (r_job.had_artifact) {
		if (r_job.backup_path.is_empty() || !FileAccess::exists(r_job.backup_path) ||
				DirAccess::copy_absolute(r_job.backup_path, r_job.artifact_absolute_path) != OK) {
			r_job.rollback_complete = false;
		}
	} else if (FileAccess::exists(r_job.artifact_absolute_path) &&
			DirAccess::remove_absolute(r_job.artifact_absolute_path) != OK) {
		r_job.rollback_complete = false;
	}
	_cleanup_backup(r_job);
}

void MCPGDExtensionBuildService::_finish_process(BuildRecord &r_job) {
	_drain_output(r_job);
	r_job.finished_usec = OS::get_singleton()->get_ticks_usec();
	r_job.finished_unix_ms = _unix_time_ms();
	r_job.exit_code = OS::get_singleton()->get_process_exit_code(r_job.process_id);
	r_job.diagnostics = _parse_diagnostics(r_job.stdout_tail, r_job.stderr_tail);

	if (r_job.exit_code != 0) {
		r_job.state = "failed";
		r_job.reload_status = "not_attempted";
		r_job.failure_code = "BUILD_FAILED";
		r_job.failure_message = vformat("SCons exited with code %d.", r_job.exit_code);
		_rollback(r_job);
		return;
	}
	if (!FileAccess::exists(r_job.artifact_absolute_path) || FileAccess::get_size(r_job.artifact_absolute_path) <= 0) {
		r_job.state = "failed";
		r_job.stderr_tail = _bounded_tail(r_job.stderr_tail + "\nBuild succeeded but the selected GDExtension library is missing or empty.");
		r_job.diagnostics = _parse_diagnostics(r_job.stdout_tail, r_job.stderr_tail);
		r_job.reload_status = "not_attempted";
		r_job.failure_code = "ARTIFACT_MISSING";
		r_job.failure_message = "The build exited successfully, but the selected GDExtension library is missing or empty.";
		_rollback(r_job);
		return;
	}

	r_job.artifact_sha256 = FileAccess::get_sha256(r_job.artifact_absolute_path);
	r_job.state = "succeeded";
	r_job.reload_status = "not_requested";

	if (r_job.reload_policy == "none") {
		_cleanup_backup(r_job);
		return;
	}
	GDExtensionManager *manager = GDExtensionManager::get_singleton();
	if (!manager || !manager->is_extension_loaded(r_job.extension_path)) {
		r_job.reload_status = "not_loaded";
	} else {
		const GDExtensionManager::LoadStatus status = manager->reload_extension(r_job.extension_path);
		r_job.reload_status = _load_status_name(status);
		if (status == GDExtensionManager::LOAD_STATUS_NEEDS_RESTART) {
			r_job.state = "needs_restart";
		} else if (status == GDExtensionManager::LOAD_STATUS_FAILED) {
			_rollback(r_job);
			r_job.state = "failed";
			if (r_job.rollback_complete) {
				r_job.failure_code = "RELOAD_FAILED_ROLLED_BACK";
				r_job.failure_message = "The GDExtension could not be hot-reloaded; the previous artifact state was restored.";
				r_job.artifact_sha256 = FileAccess::exists(r_job.artifact_absolute_path) &&
					FileAccess::get_size(r_job.artifact_absolute_path) > 0 ?
							FileAccess::get_sha256(r_job.artifact_absolute_path) :
							String();
			} else {
				r_job.failure_code = "RELOAD_FAILED_ROLLBACK_INCOMPLETE";
				r_job.failure_message =
					"The GDExtension could not be hot-reloaded, and restoring the previous artifact state failed.";
				r_job.artifact_sha256 = String();
			}
			return;
		}
	}

	if (r_job.reload_policy == "restart_runtime" && r_job.runtime_was_playing) {
		EditorRunBar *run_bar = EditorRunBar::get_singleton();
		if (run_bar) {
			run_bar->stop_playing();
			run_bar->play_main_scene();
			if (r_job.state == "succeeded") {
				r_job.reload_status = "runtime_restarted";
			}
		}
	}

	_cleanup_backup(r_job);
}

MCPGDExtensionBuildService::BuildRecord *MCPGDExtensionBuildService::_resolve_job(const Dictionary &p_arguments,
		const Dictionary &p_context, Dictionary &r_error) {
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, PackedStringArray{ "jobId" }, unknown_argument)) {
		r_error = _error("INVALID_ARGUMENTS", "Unknown GDExtension build job argument: " + unknown_argument);
		return nullptr;
	}
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		r_error = _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
		return nullptr;
	}
	const String session_id = _session_id(p_context, r_error);
	if (!r_error.is_empty()) {
		return nullptr;
	}
	BuildRecord *job = jobs.getptr(String(job_value));
	if (!job || job->session_id != session_id) {
		r_error = _error("BUILD_JOB_NOT_FOUND", "The GDExtension build job does not exist in this MCP session.");
		return nullptr;
	}
	return job;
}

Dictionary MCPGDExtensionBuildService::build(const Dictionary &p_arguments, const Dictionary &p_context) {
	if (shutting_down) {
		return _error("BUILD_SERVICE_UNAVAILABLE", "The GDExtension build service is shutting down.");
	}
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments,
			PackedStringArray{ "extensionPath", "profile", "clean", "reload", "timeoutMs", "expectedProjectRevision" }, unknown_argument)) {
		return _error("INVALID_ARGUMENTS", "Unknown godot.gdextension.build argument: " + unknown_argument);
	}
	for (const KeyValue<String, BuildRecord> &entry : jobs) {
		if (entry.value.state == "running") {
			Dictionary details;
			details["jobId"] = entry.value.id;
			return _error("BUILD_IN_PROGRESS", "Only one GDExtension build may run for a project.", details);
		}
	}

	Dictionary context_error;
	const String session_id = _session_id(p_context, context_error);
	if (!context_error.is_empty()) {
		return context_error;
	}
	const Variant path_value = p_arguments.get("extensionPath", Variant());
	if (path_value.get_type() != Variant::STRING || !String(path_value).ends_with(".gdextension")) {
		return _error("INVALID_ARGUMENTS", "extensionPath must be a project-local .gdextension path.");
	}
	const Variant profile_value = p_arguments.get("profile", "debug");
	const Variant clean_value = p_arguments.get("clean", false);
	const Variant reload_value = p_arguments.get("reload", "if_reloadable");
	if (profile_value.get_type() != Variant::STRING || clean_value.get_type() != Variant::BOOL || reload_value.get_type() != Variant::STRING) {
		return _error("INVALID_ARGUMENTS", "profile and reload must be strings, and clean must be boolean.");
	}
	const String profile_name = profile_value;
	const String reload_policy = reload_value;
	MCPGDExtensionProfileRegistry::Profile profile;
	String profile_error;
	if (!MCPGDExtensionProfileRegistry::resolve(profile_name, profile, &profile_error)) {
		return _error("INVALID_ARGUMENTS", profile_error);
	}
	if (reload_policy != "none" && reload_policy != "if_reloadable" && reload_policy != "restart_runtime") {
		return _error("INVALID_ARGUMENTS", "reload must be none, if_reloadable, or restart_runtime.");
	}
	int64_t timeout_ms = DEFAULT_TIMEOUT_MS;
	if (p_arguments.has("timeoutMs") && !MCPToolUtils::try_get_json_integer(p_arguments["timeoutMs"], 1000, MAX_TIMEOUT_MS, timeout_ms)) {
		return _error("INVALID_ARGUMENTS", vformat("timeoutMs must be an integer between 1000 and %d.", MAX_TIMEOUT_MS));
	}

	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (!settings) {
		return _error("PROJECT_SETTINGS_UNAVAILABLE", "Project settings are unavailable.");
	}
	const String project_revision = MCPProjectRevision::compute(settings);
	if (p_arguments.has("expectedProjectRevision")) {
		const Variant expected = p_arguments["expectedProjectRevision"];
		if (expected.get_type() != Variant::STRING || String(expected).length() != 64 || !String(expected).is_valid_hex_number(false)) {
			return _error("INVALID_ARGUMENTS", "expectedProjectRevision must be a 64-character SHA-256 string.");
		}
		if (String(expected).to_lower() != project_revision) {
			Dictionary details;
			details["expectedProjectRevision"] = expected;
			details["currentProjectRevision"] = project_revision;
			return _error("PROJECT_REVISION_CONFLICT", "Project settings changed after the expected revision was read.", details);
		}
	}

	String extension_path;
	String extension_absolute_path;
	String path_error;
	if (MCPPathUtils::resolve_project_file_path(path_value, extension_path, extension_absolute_path, &path_error) != OK ||
			!FileAccess::exists(extension_absolute_path)) {
		return _error("INVALID_EXTENSION_PATH", path_error.is_empty() ? "The .gdextension file does not exist." : path_error);
	}

	Ref<ConfigFile> config;
	config.instantiate();
	const Error config_error = config->load(extension_path);
	if (config_error != OK) {
		return _error("INVALID_EXTENSION_CONFIG", "Could not parse the .gdextension file: " + String(error_names[config_error]));
	}
	const String library_path = GDExtensionLibraryLoader::find_extension_library(extension_path, config,
			[&profile](const String &p_feature) {
				if (p_feature == "debug") {
					return !profile.release;
				}
				if (p_feature == "release") {
					return profile.release;
				}
				return OS::get_singleton()->has_feature(p_feature);
			});
	if (library_path.is_empty()) {
		return _error("ARTIFACT_NOT_CONFIGURED", "No library entry in the .gdextension matches this platform and profile.");
	}
	String artifact_path;
	String artifact_absolute_path;
	if (MCPPathUtils::resolve_project_file_path(library_path, artifact_path, artifact_absolute_path, &path_error) != OK) {
		return _error("INVALID_ARTIFACT_PATH", path_error);
	}

	while (jobs.size() >= MAX_BUILD_RECORDS && !job_order.is_empty()) {
		jobs.erase(job_order[0]);
		job_order.remove_at(0);
	}
	BuildRecord job;
	job.id = "gdextension-build-" + String::num_uint64(next_job_id++);
	job.session_id = session_id;
	job.extension_path = extension_path;
	job.artifact_path = artifact_path;
	job.artifact_absolute_path = artifact_absolute_path;
	job.profile = profile.name;
	job.reload_policy = reload_policy;
	job.project_revision = project_revision;
	job.started_usec = OS::get_singleton()->get_ticks_usec();
	job.started_unix_ms = _unix_time_ms();
	job.timeout_usec = uint64_t(timeout_ms) * 1000;
	job.runtime_was_playing = EditorRunBar::get_singleton() && EditorRunBar::get_singleton()->is_playing();
	job.had_artifact = FileAccess::exists(artifact_absolute_path);
	if (job.had_artifact) {
		job.backup_path = OS::get_singleton()->get_temp_path().path_join(job.id + ".backup");
		if (DirAccess::copy_absolute(artifact_absolute_path, job.backup_path) != OK) {
			return _error("BACKUP_FAILED", "Could not create the rollback copy for the current extension library.");
		}
	}

	const String build_root = _find_sconstruct_root(extension_absolute_path.get_base_dir(), settings->get_resource_path());
	if (build_root.is_empty()) {
		_cleanup_backup(job);
		return _error("BUILDER_NOT_FOUND", "No SConstruct was found between the .gdextension directory and the project root.");
	}
	const String platform = MCPGDExtensionProfileRegistry::current_platform();
	if (platform.is_empty()) {
		_cleanup_backup(job);
		return _error("UNSUPPORTED_PLATFORM", "The current editor platform has no registered godot-cpp SCons profile.");
	}
	List<String> arguments;
	arguments.push_back("-C");
	arguments.push_back(build_root);
	if (bool(clean_value)) {
		arguments.push_back("-c");
	}
	arguments.push_back("platform=" + platform);
	arguments.push_back("target=" + profile.scons_target);
	const Dictionary process = OS::get_singleton()->execute_with_pipe("scons", arguments, false);
	if (!process.has("pid") || !process.has("stdio") || !process.has("stderr")) {
		_cleanup_backup(job);
		return _error("BUILD_START_FAILED", "Could not start the registered SCons builder. Ensure scons is available in PATH.");
	}
	job.process_id = ProcessID(int64_t(process["pid"]));
	job.stdout_pipe = process["stdio"];
	job.stderr_pipe = process["stderr"];
	job.state = "running";
	job.reload_status = "pending";
	jobs.insert(job.id, job);
	job_order.push_back(job.id);
	return _result(job);
}

Dictionary MCPGDExtensionBuildService::get_status(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	BuildRecord *job = _resolve_job(p_arguments, p_context, error);
	if (!job) {
		return error;
	}
	if (job->state == "running") {
		_drain_output(*job);
	}
	return _result(*job);
}

Dictionary MCPGDExtensionBuildService::cancel(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	BuildRecord *job = _resolve_job(p_arguments, p_context, error);
	if (!job) {
		return error;
	}
	if (job->state == "running") {
		OS::get_singleton()->kill(job->process_id);
		_drain_output(*job);
		job->state = "cancelled";
		job->reload_status = "not_attempted";
		job->failure_code = "BUILD_CANCELLED";
		job->failure_message = "The build was cancelled by the MCP client.";
		job->finished_usec = OS::get_singleton()->get_ticks_usec();
		job->finished_unix_ms = _unix_time_ms();
		job->stderr_tail = _bounded_tail(job->stderr_tail + "\nBuild cancelled by the MCP client.");
		job->diagnostics = _parse_diagnostics(job->stdout_tail, job->stderr_tail);
		_rollback(*job);
	}
	return _result(*job);
}

void MCPGDExtensionBuildService::process() {
	if (shutting_down) {
		return;
	}
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	for (KeyValue<String, BuildRecord> &entry : jobs) {
		BuildRecord &job = entry.value;
		if (job.state != "running") {
			continue;
		}
		_drain_output(job);
		if (now - job.started_usec >= job.timeout_usec) {
			OS::get_singleton()->kill(job.process_id);
			job.state = "failed";
			job.reload_status = "not_attempted";
			job.failure_code = "BUILD_TIMEOUT";
			job.failure_message = "The build exceeded timeoutMs and was terminated.";
			job.finished_usec = now;
			job.finished_unix_ms = _unix_time_ms();
			job.stderr_tail = _bounded_tail(job.stderr_tail + "\nBuild exceeded timeoutMs and was terminated.");
			job.diagnostics = _parse_diagnostics(job.stdout_tail, job.stderr_tail);
			_rollback(job);
			continue;
		}
		if (!OS::get_singleton()->is_process_running(job.process_id)) {
			_finish_process(job);
		}
	}
}

void MCPGDExtensionBuildService::release_session(const String &p_session_id) {
	const String session_id = p_session_id.is_empty() ? String("anonymous") : p_session_id;
	for (int i = job_order.size() - 1; i >= 0; i--) {
		BuildRecord *job = jobs.getptr(job_order[i]);
		if (!job || job->session_id != session_id) {
			continue;
		}
		if (job->state == "running") {
			OS::get_singleton()->kill(job->process_id);
			_rollback(*job);
		} else {
			_cleanup_backup(*job);
		}
		jobs.erase(job_order[i]);
		job_order.remove_at(i);
	}
}

void MCPGDExtensionBuildService::shutdown() {
	if (shutting_down) {
		return;
	}
	shutting_down = true;
	for (KeyValue<String, BuildRecord> &entry : jobs) {
		if (entry.value.state == "running") {
			OS::get_singleton()->kill(entry.value.process_id);
			_rollback(entry.value);
		} else {
			_cleanup_backup(entry.value);
		}
	}
	jobs.clear();
	job_order.clear();
}

MCPGDExtensionBuildService::~MCPGDExtensionBuildService() {
	shutdown();
}
