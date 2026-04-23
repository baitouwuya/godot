/**************************************************************************/
/*  cli_performance_recorder.cpp                                          */
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

#include "cli_performance_recorder.h"

#include "core/config/engine.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include "core/variant/typed_array.h"

CLIPerformanceRecorder::P2QuantileEstimator::P2QuantileEstimator() {}

CLIPerformanceRecorder::P2QuantileEstimator::P2QuantileEstimator(double p_quantile) {
	quantile = p_quantile;
}

void CLIPerformanceRecorder::P2QuantileEstimator::_sort_samples(double *p_samples, uint64_t p_count) {
	for (uint64_t i = 0; i < p_count; i++) {
		for (uint64_t j = i + 1; j < p_count; j++) {
			if (p_samples[j] < p_samples[i]) {
				SWAP(p_samples[i], p_samples[j]);
			}
		}
	}
}

void CLIPerformanceRecorder::P2QuantileEstimator::_initialize_markers() {
	double sorted_samples[5];
	for (int i = 0; i < 5; i++) {
		sorted_samples[i] = initial_samples[i];
	}
	_sort_samples(sorted_samples, 5);

	for (int i = 0; i < 5; i++) {
		marker_heights[i] = sorted_samples[i];
		marker_positions[i] = (double)(i + 1);
	}

	desired_positions[0] = 1.0;
	desired_positions[1] = 1.0 + 2.0 * quantile;
	desired_positions[2] = 1.0 + 4.0 * quantile;
	desired_positions[3] = 3.0 + 2.0 * quantile;
	desired_positions[4] = 5.0;

	desired_increments[0] = 0.0;
	desired_increments[1] = quantile / 2.0;
	desired_increments[2] = quantile;
	desired_increments[3] = (1.0 + quantile) / 2.0;
	desired_increments[4] = 1.0;
}

double CLIPerformanceRecorder::P2QuantileEstimator::_parabolic(int p_index, int p_direction) const {
	const double prev_position = marker_positions[p_index - 1];
	const double current_position = marker_positions[p_index];
	const double next_position = marker_positions[p_index + 1];
	const double prev_height = marker_heights[p_index - 1];
	const double current_height = marker_heights[p_index];
	const double next_height = marker_heights[p_index + 1];
	const double direction = (double)p_direction;

	return current_height +
			direction / (next_position - prev_position) *
					((current_position - prev_position + direction) * (next_height - current_height) / (next_position - current_position) +
							(next_position - current_position - direction) * (current_height - prev_height) / (current_position - prev_position));
}

double CLIPerformanceRecorder::P2QuantileEstimator::_linear(int p_index, int p_direction) const {
	const int adjacent_index = p_index + p_direction;
	return marker_heights[p_index] +
			(double)p_direction * (marker_heights[adjacent_index] - marker_heights[p_index]) / (marker_positions[adjacent_index] - marker_positions[p_index]);
}

void CLIPerformanceRecorder::P2QuantileEstimator::push_sample(double p_sample) {
	if (count < 5) {
		initial_samples[count] = p_sample;
		count++;
		if (count == 5) {
			_initialize_markers();
		}
		return;
	}

	count++;

	int k = 0;
	if (p_sample < marker_heights[0]) {
		marker_heights[0] = p_sample;
		k = 0;
	} else if (p_sample < marker_heights[1]) {
		k = 0;
	} else if (p_sample < marker_heights[2]) {
		k = 1;
	} else if (p_sample < marker_heights[3]) {
		k = 2;
	} else if (p_sample <= marker_heights[4]) {
		k = 3;
	} else {
		marker_heights[4] = p_sample;
		k = 3;
	}

	for (int i = k + 1; i < 5; i++) {
		marker_positions[i] += 1.0;
	}
	for (int i = 0; i < 5; i++) {
		desired_positions[i] += desired_increments[i];
	}

	for (int i = 1; i <= 3; i++) {
		const double delta = desired_positions[i] - marker_positions[i];
		if ((delta >= 1.0 && marker_positions[i + 1] - marker_positions[i] > 1.0) ||
				(delta <= -1.0 && marker_positions[i - 1] - marker_positions[i] < -1.0)) {
			const int direction = delta > 0.0 ? 1 : -1;
			const double parabolic = _parabolic(i, direction);
			if (marker_heights[i - 1] < parabolic && parabolic < marker_heights[i + 1]) {
				marker_heights[i] = parabolic;
			} else {
				marker_heights[i] = _linear(i, direction);
			}
			marker_positions[i] += (double)direction;
		}
	}
}

double CLIPerformanceRecorder::P2QuantileEstimator::estimate() const {
	if (count == 0) {
		return 0.0;
	}

	if (count < 5) {
		double sorted_samples[5];
		for (uint64_t i = 0; i < count; i++) {
			sorted_samples[i] = initial_samples[i];
		}
		_sort_samples(sorted_samples, count);
		if (count == 1) {
			return sorted_samples[0];
		}

		const double fractional_index = quantile * (double)(count - 1);
		const int lower_index = (int)Math::floor(fractional_index);
		const int upper_index = MIN((int)count - 1, lower_index + 1);
		const double weight = fractional_index - (double)lower_index;
		return sorted_samples[lower_index] + (sorted_samples[upper_index] - sorted_samples[lower_index]) * weight;
	}

	return marker_heights[2];
}

CLIPerformanceRecorder::OnlineStats::OnlineStats() :
		p50(0.5),
		p95(0.95),
		p99(0.99) {}

bool CLIPerformanceRecorder::_consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error) {
	if (!r_next) {
		r_error = "Missing value after " + p_arg + ".";
		return false;
	}

	r_value = r_next->get();
	r_next = r_next->next();
	return true;
}

Error CLIPerformanceRecorder::_validate_samples_output_path(const String &p_path, String &r_error) {
	Error err = OK;
	Ref<FileAccess> output = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (output.is_null() || err != OK) {
		r_error = vformat("Unable to open --perf-samples-file path \"%s\" for writing (error code %d).", p_path, (int)err);
		return err == OK ? ERR_CANT_OPEN : err;
	}

	output->close();
	return OK;
}

Dictionary CLIPerformanceRecorder::_serialize_stats_map(const HashMap<String, OnlineStats> &p_stats_map) {
	Dictionary result;
	for (const KeyValue<String, OnlineStats> &E : p_stats_map) {
		if (E.value.count == 0) {
			continue;
		}
		result[E.key] = E.value.to_dictionary();
	}
	return result;
}

double CLIPerformanceRecorder::_variant_to_double(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::INT:
			return (double)(int64_t)p_value;
		case Variant::FLOAT:
			return (double)p_value;
		default:
			return 0.0;
	}
}

void CLIPerformanceRecorder::OnlineStats::push_sample(double p_sample) {
	last = p_sample;
	p50.push_sample(p_sample);
	p95.push_sample(p_sample);
	p99.push_sample(p_sample);
	if (count == 0) {
		count = 1;
		min = p_sample;
		max = p_sample;
		mean = p_sample;
		m2 = 0.0;
		first = p_sample;
		return;
	}

	count++;
	min = MIN(min, p_sample);
	max = MAX(max, p_sample);
	const double delta = p_sample - mean;
	mean += delta / (double)count;
	const double delta2 = p_sample - mean;
	m2 += delta * delta2;
}

Dictionary CLIPerformanceRecorder::OnlineStats::to_dictionary() const {
	Dictionary result;
	const double variance = count > 0 ? m2 / (double)count : 0.0;
	result["count"] = (int64_t)count;
	result["min"] = min;
	result["max"] = max;
	result["mean"] = mean;
	result["variance"] = variance;
	result["stddev"] = Math::sqrt(variance);
	result["first"] = first;
	result["last"] = last;
	result["delta"] = last - first;
	result["p50"] = p50.estimate();
	result["p95"] = p95.estimate();
	result["p99"] = p99.estimate();
	return result;
}

Dictionary CLIPerformanceRecorder::FrameSnapshot::to_dictionary(bool p_recorded) const {
	Dictionary result;
	result["frame"] = (int64_t)frame;
	result["frameMetrics"] = frame_metrics;
	result["builtinMonitors"] = builtin_monitors;
	result["customMonitors"] = custom_monitors;
	if (p_recorded) {
		result["recorded"] = true;
	}
	return result;
}

Dictionary CLIPerformanceRecorder::BudgetThresholdStats::to_dictionary(uint64_t p_captured_frames) const {
	Dictionary result;
	result["thresholdMs"] = threshold_ms;
	result["overCount"] = (int64_t)over_count;
	result["overRatio"] = p_captured_frames > 0 ? (double)over_count / (double)p_captured_frames : 0.0;
	return result;
}

bool CLIPerformanceRecorder::parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error) {
	String value;
	r_error = String();

	if (p_arg == "--perf-record") {
		r_options.enabled = true;
		return true;
	}

	if (p_arg == "--perf-start-frame") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--perf-start-frame must be followed by an integer.";
			return true;
		}
		r_options.start_frame = value.to_int();
		r_options.start_frame_set = true;
		return true;
	}

	if (p_arg == "--perf-end-frame") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--perf-end-frame must be followed by an integer.";
			return true;
		}
		r_options.end_frame = value.to_int();
		r_options.end_frame_set = true;
		return true;
	}

	if (p_arg == "--perf-top-frames") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--perf-top-frames must be followed by an integer.";
			return true;
		}
		r_options.top_frames = value.to_int();
		r_options.top_frames_set = true;
		return true;
	}

	if (p_arg == "--perf-samples-file") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		r_options.samples_file = value;
		r_options.samples_file_set = true;
		return true;
	}

	return false;
}

Error CLIPerformanceRecorder::validate_options(const Options &p_options, bool p_editor, bool p_project_manager, bool p_cmdline_tool, String &r_error) {
	r_error = String();

	if (!p_options.enabled) {
		if (p_options.start_frame_set || p_options.end_frame_set || p_options.top_frames_set || p_options.samples_file_set) {
			r_error = "--perf-start-frame, --perf-end-frame, --perf-top-frames, and --perf-samples-file can only be used together with --perf-record.";
			return ERR_INVALID_PARAMETER;
		}
		return OK;
	}

	if (!p_options.end_frame_set) {
		r_error = "--perf-record requires --perf-end-frame.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_options.start_frame < 0) {
		r_error = "--perf-start-frame must be greater than or equal to 0.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_options.end_frame < p_options.start_frame) {
		r_error = "--perf-end-frame must be greater than or equal to --perf-start-frame.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_options.top_frames < 0) {
		r_error = "--perf-top-frames must be greater than or equal to 0.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_editor || p_project_manager || p_cmdline_tool) {
		r_error = "--perf-record only supports CLI project runs. It cannot be used with the editor, project manager, or one-shot command line tools.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_options.samples_file_set) {
		return _validate_samples_output_path(p_options.samples_file, r_error);
	}

	return OK;
}

bool CLIPerformanceRecorder::should_include_builtin_monitor(Performance::Monitor p_monitor) {
	switch (p_monitor) {
		case Performance::TIME_FPS:
		case Performance::TIME_PROCESS:
		case Performance::TIME_PHYSICS_PROCESS:
		case Performance::TIME_NAVIGATION_PROCESS:
			return false;
		default:
			return true;
	}
}

bool CLIPerformanceRecorder::should_include_custom_monitor_value(const Variant &p_value) {
	return p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT;
}

CLIPerformanceRecorder::CLIPerformanceRecorder(const Options &p_options) {
	options = p_options;
	if (options.top_frames > 0) {
		slow_frame_heap.reserve(options.top_frames);
	}

	budget_thresholds.resize(3);
	budget_thresholds.write[0].key = "over16_667ms";
	budget_thresholds.write[0].threshold_ms = 16.667;
	budget_thresholds.write[0].threshold_usec = 16667.0;
	budget_thresholds.write[1].key = "over33_333ms";
	budget_thresholds.write[1].threshold_ms = 33.333;
	budget_thresholds.write[1].threshold_usec = 33333.0;
	budget_thresholds.write[2].key = "over50_000ms";
	budget_thresholds.write[2].threshold_ms = 50.0;
	budget_thresholds.write[2].threshold_usec = 50000.0;
}

CLIPerformanceRecorder::~CLIPerformanceRecorder() {
	close();
}

Error CLIPerformanceRecorder::initialize(String &r_error) {
	r_error = String();
	if (!options.samples_file_set) {
		return OK;
	}

	Error err = OK;
	samples_output_file = FileAccess::open(options.samples_file, FileAccess::WRITE, &err);
	if (samples_output_file.is_null() || err != OK) {
		r_error = vformat("Unable to open --perf-samples-file path \"%s\" for writing (error code %d).", options.samples_file, (int)err);
		return err == OK ? ERR_CANT_OPEN : err;
	}

	return OK;
}

void CLIPerformanceRecorder::close() {
	if (samples_output_file.is_valid()) {
		samples_output_file->flush();
		samples_output_file->close();
		samples_output_file.unref();
	}
}

void CLIPerformanceRecorder::_record_metric(HashMap<String, OnlineStats> &r_metrics, const String &p_key, double p_sample, Dictionary *r_current_samples) {
	if (!r_metrics.has(p_key)) {
		r_metrics.insert(p_key, OnlineStats());
	}
	r_metrics[p_key].push_sample(p_sample);
	if (r_current_samples) {
		(*r_current_samples)[p_key] = p_sample;
	}
}

void CLIPerformanceRecorder::_record_budget(double p_frame_time_usec) {
	for (int i = 0; i < budget_thresholds.size(); i++) {
		BudgetThresholdStats &threshold = budget_thresholds.write[i];
		if (p_frame_time_usec > threshold.threshold_usec) {
			threshold.over_count++;
		}
	}
}

bool CLIPerformanceRecorder::SlowFrameAscendingComparator::operator()(const FrameSnapshot &p_a, const FrameSnapshot &p_b) const {
	if (p_a.frame_time_usec == p_b.frame_time_usec) {
		return p_a.frame < p_b.frame;
	}
	return p_a.frame_time_usec < p_b.frame_time_usec;
}

bool CLIPerformanceRecorder::SlowFrameDescendingComparator::operator()(const FrameSnapshot &p_a, const FrameSnapshot &p_b) const {
	if (p_a.frame_time_usec == p_b.frame_time_usec) {
		return p_a.frame < p_b.frame;
	}
	return p_a.frame_time_usec > p_b.frame_time_usec;
}

void CLIPerformanceRecorder::_record_slow_frame(const FrameSnapshot &p_snapshot) {
	if (options.top_frames <= 0) {
		return;
	}

	if (slow_frame_heap.size() < options.top_frames) {
		slow_frame_heap.push_back(p_snapshot);
		slow_frame_heap.sort_custom<SlowFrameAscendingComparator>();
		return;
	}

	const SlowFrameAscendingComparator compare;
	if (compare(slow_frame_heap[0], p_snapshot)) {
		slow_frame_heap.write[0] = p_snapshot;
		slow_frame_heap.sort_custom<SlowFrameAscendingComparator>();
	}
}

void CLIPerformanceRecorder::_write_frame_sample(const FrameSnapshot &p_snapshot) {
	if (samples_output_file.is_null()) {
		return;
	}

	samples_output_file->store_line(JSON::stringify(p_snapshot.to_dictionary(true)));
}

Dictionary CLIPerformanceRecorder::_build_budget_summary() const {
	Dictionary result;
	for (int i = 0; i < budget_thresholds.size(); i++) {
		const BudgetThresholdStats &threshold = budget_thresholds[i];
		result[threshold.key] = threshold.to_dictionary(captured_frames);
	}
	return result;
}

Array CLIPerformanceRecorder::_build_slow_frames_array() const {
	Array result;
	Vector<FrameSnapshot> slow_frames = slow_frame_heap;
	slow_frames.sort_custom<SlowFrameDescendingComparator>();
	for (int i = 0; i < slow_frames.size(); i++) {
		result.push_back(slow_frames[i].to_dictionary());
	}
	return result;
}

void CLIPerformanceRecorder::record_frame(uint64_t p_frame_index, uint64_t p_frame_time_usec, uint64_t p_process_time_usec, uint64_t p_physics_process_time_usec, uint64_t p_navigation_process_time_usec, double p_physics_frame_time_sec) {
	if (!options.enabled) {
		return;
	}

	if (p_frame_index < (uint64_t)options.start_frame || p_frame_index > (uint64_t)options.end_frame) {
		return;
	}

	captured_frames++;
	last_sampled_frame = p_frame_index;
	has_sampled_frame = true;

	Dictionary frame_metric_samples;
	_record_metric(frame_metrics, "frame_time_usec", (double)p_frame_time_usec, &frame_metric_samples);
	_record_metric(frame_metrics, "process_time_usec", (double)p_process_time_usec, &frame_metric_samples);
	_record_metric(frame_metrics, "physics_process_time_usec", (double)p_physics_process_time_usec, &frame_metric_samples);
	_record_metric(frame_metrics, "navigation_process_time_usec", (double)p_navigation_process_time_usec, &frame_metric_samples);
	_record_metric(frame_metrics, "physics_frame_time_sec", p_physics_frame_time_sec, &frame_metric_samples);
	_record_metric(frame_metrics, "fps", p_frame_time_usec > 0 ? 1000000.0 / (double)p_frame_time_usec : 0.0, &frame_metric_samples);
	_record_budget((double)p_frame_time_usec);

	Dictionary builtin_monitor_samples;
	Dictionary custom_monitor_samples;

	Performance *performance = Performance::get_singleton();
	if (performance) {
		for (int i = 0; i < Performance::MONITOR_MAX; i++) {
			const Performance::Monitor monitor = (Performance::Monitor)i;
			if (!should_include_builtin_monitor(monitor)) {
				continue;
			}
			const double monitor_value = performance->get_monitor(monitor);
			_record_metric(builtin_monitor_metrics, performance->get_monitor_name(monitor), monitor_value, &builtin_monitor_samples);
		}

		const TypedArray<StringName> custom_monitor_names = performance->get_custom_monitor_names();
		for (int i = 0; i < custom_monitor_names.size(); i++) {
			const StringName name = custom_monitor_names[i];
			const Variant value = performance->get_custom_monitor(name);
			if (!should_include_custom_monitor_value(value)) {
				continue;
			}
			_record_metric(custom_monitor_metrics, String(name), _variant_to_double(value), &custom_monitor_samples);
		}
	}

	FrameSnapshot snapshot;
	snapshot.frame = p_frame_index;
	snapshot.frame_time_usec = p_frame_time_usec;
	snapshot.frame_metrics = frame_metric_samples;
	snapshot.builtin_monitors = builtin_monitor_samples;
	snapshot.custom_monitors = custom_monitor_samples;

	_record_slow_frame(snapshot);
	_write_frame_sample(snapshot);
}

Dictionary CLIPerformanceRecorder::build_summary(bool p_completed) const {
	Dictionary recording;
	recording["startFrame"] = options.start_frame;
	recording["endFrame"] = options.end_frame;
	recording["capturedFrames"] = (int64_t)captured_frames;
	recording["completed"] = p_completed;

	Dictionary summary;
	summary["recording"] = recording;
	summary["frameMetrics"] = _serialize_stats_map(frame_metrics);
	summary["builtinMonitorMetrics"] = _serialize_stats_map(builtin_monitor_metrics);
	summary["customMonitorMetrics"] = _serialize_stats_map(custom_monitor_metrics);
	summary["budgetSummary"] = _build_budget_summary();
	if (options.top_frames > 0) {
		summary["slowFrames"] = _build_slow_frames_array();
	}
	return summary;
}

void CLIPerformanceRecorder::print_summary_stdout(bool p_completed) const {
	const bool stdout_was_enabled = OS::get_singleton()->is_stdout_enabled();
	OS::get_singleton()->set_stdout_enabled(true);
	OS::get_singleton()->print("%s\n", JSON::stringify(build_summary(p_completed)).utf8().get_data());
	OS::get_singleton()->set_stdout_enabled(stdout_was_enabled);
}

bool CLIPerformanceRecorder::is_completed() const {
	return options.enabled && options.end_frame >= 0 && has_sampled_frame && last_sampled_frame >= (uint64_t)options.end_frame;
}
