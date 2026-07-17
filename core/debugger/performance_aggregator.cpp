/**************************************************************************/
/*  performance_aggregator.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "performance_aggregator.h"

#include "core/math/math_funcs.h"

PerformanceAggregator::P2QuantileEstimator::P2QuantileEstimator() {
	reset(0.0);
}

PerformanceAggregator::P2QuantileEstimator::P2QuantileEstimator(double p_quantile) {
	reset(p_quantile);
}

void PerformanceAggregator::P2QuantileEstimator::reset(double p_quantile) {
	quantile = CLAMP(p_quantile, 0.0, 1.0);
	count = 0;
	for (int i = 0; i < 5; i++) {
		initial_samples[i] = 0.0;
		marker_heights[i] = 0.0;
		marker_positions[i] = 0.0;
		desired_positions[i] = 0.0;
		desired_increments[i] = 0.0;
	}
}

void PerformanceAggregator::P2QuantileEstimator::_sort_samples(double *p_samples, uint64_t p_count) {
	for (uint64_t i = 0; i < p_count; i++) {
		for (uint64_t j = i + 1; j < p_count; j++) {
			if (p_samples[j] < p_samples[i]) {
				SWAP(p_samples[i], p_samples[j]);
			}
		}
	}
}

void PerformanceAggregator::P2QuantileEstimator::_initialize_markers() {
	double sorted_samples[5];
	for (int i = 0; i < 5; i++) {
		sorted_samples[i] = initial_samples[i];
	}
	_sort_samples(sorted_samples, 5);
	for (int i = 0; i < 5; i++) {
		marker_heights[i] = sorted_samples[i];
		marker_positions[i] = double(i + 1);
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

double PerformanceAggregator::P2QuantileEstimator::_parabolic(int p_index, int p_direction) const {
	const double previous_position = marker_positions[p_index - 1];
	const double current_position = marker_positions[p_index];
	const double next_position = marker_positions[p_index + 1];
	const double previous_height = marker_heights[p_index - 1];
	const double current_height = marker_heights[p_index];
	const double next_height = marker_heights[p_index + 1];
	const double direction = double(p_direction);
	return current_height + direction / (next_position - previous_position) *
			((current_position - previous_position + direction) * (next_height - current_height) / (next_position - current_position) +
					(next_position - current_position - direction) * (current_height - previous_height) / (current_position - previous_position));
}

double PerformanceAggregator::P2QuantileEstimator::_linear(int p_index, int p_direction) const {
	const int adjacent = p_index + p_direction;
	return marker_heights[p_index] + double(p_direction) * (marker_heights[adjacent] - marker_heights[p_index]) / (marker_positions[adjacent] - marker_positions[p_index]);
}

void PerformanceAggregator::P2QuantileEstimator::push_sample(double p_sample) {
	if (count < 5) {
		initial_samples[count++] = p_sample;
		if (count == 5) {
			_initialize_markers();
		}
		return;
	}
	count++;
	int interval = 0;
	if (p_sample < marker_heights[0]) {
		marker_heights[0] = p_sample;
	} else if (p_sample < marker_heights[1]) {
		interval = 0;
	} else if (p_sample < marker_heights[2]) {
		interval = 1;
	} else if (p_sample < marker_heights[3]) {
		interval = 2;
	} else if (p_sample <= marker_heights[4]) {
		interval = 3;
	} else {
		marker_heights[4] = p_sample;
		interval = 3;
	}
	for (int i = interval + 1; i < 5; i++) {
		marker_positions[i] += 1.0;
	}
	for (int i = 0; i < 5; i++) {
		desired_positions[i] += desired_increments[i];
	}
	for (int i = 1; i <= 3; i++) {
		const double delta = desired_positions[i] - marker_positions[i];
		if ((delta >= 1.0 && marker_positions[i + 1] - marker_positions[i] > 1.0) || (delta <= -1.0 && marker_positions[i - 1] - marker_positions[i] < -1.0)) {
			const int direction = delta > 0.0 ? 1 : -1;
			const double parabolic = _parabolic(i, direction);
			marker_heights[i] = marker_heights[i - 1] < parabolic && parabolic < marker_heights[i + 1] ? parabolic : _linear(i, direction);
			marker_positions[i] += double(direction);
		}
	}
}

double PerformanceAggregator::P2QuantileEstimator::estimate() const {
	if (count == 0) {
		return 0.0;
	}
	if (count < 5) {
		double samples[5] = {};
		for (uint64_t i = 0; i < count; i++) {
			samples[i] = initial_samples[i];
		}
		_sort_samples(samples, count);
		if (count == 1) {
			return samples[0];
		}
		const double position = quantile * double(count - 1);
		const int lower = int(Math::floor(position));
		const int upper = MIN(int(count) - 1, lower + 1);
		return Math::lerp(samples[lower], samples[upper], position - double(lower));
	}
	return marker_heights[2];
}

PerformanceAggregator::OnlineStats::OnlineStats() :
		p50(0.5), p95(0.95), p99(0.99) {
}

void PerformanceAggregator::OnlineStats::reset() {
	count = 0;
	min = 0.0;
	max = 0.0;
	mean = 0.0;
	m2 = 0.0;
	first = 0.0;
	last = 0.0;
	p50.reset(0.5);
	p95.reset(0.95);
	p99.reset(0.99);
}

void PerformanceAggregator::OnlineStats::push_sample(double p_sample) {
	last = p_sample;
	p50.push_sample(p_sample);
	p95.push_sample(p_sample);
	p99.push_sample(p_sample);
	if (count == 0) {
		count = 1;
		min = p_sample;
		max = p_sample;
		mean = p_sample;
		first = p_sample;
		return;
	}
	count++;
	min = MIN(min, p_sample);
	max = MAX(max, p_sample);
	const double delta = p_sample - mean;
	mean += delta / double(count);
	m2 += delta * (p_sample - mean);
}

Dictionary PerformanceAggregator::OnlineStats::to_dictionary() const {
	const double variance = count > 0 ? m2 / double(count) : 0.0;
	Dictionary result;
	result["count"] = int64_t(count);
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

Dictionary PerformanceAggregator::FrameSnapshot::to_dictionary() const {
	Dictionary result;
	result["frame"] = int64_t(frame);
	result["frameMetrics"] = frame_metrics;
	result["builtinMonitors"] = builtin_monitors;
	result["customMonitors"] = custom_monitors;
	return result;
}

Dictionary PerformanceAggregator::BudgetThreshold::to_dictionary(uint64_t p_captured_frames) const {
	Dictionary result;
	result["thresholdMs"] = threshold_ms;
	result["overCount"] = int64_t(over_count);
	result["overRatio"] = p_captured_frames > 0 ? double(over_count) / double(p_captured_frames) : 0.0;
	return result;
}

bool PerformanceAggregator::SlowFrameAscendingComparator::operator()(const FrameSnapshot &p_left, const FrameSnapshot &p_right) const {
	return p_left.frame_time_usec == p_right.frame_time_usec ? p_left.frame < p_right.frame : p_left.frame_time_usec < p_right.frame_time_usec;
}

bool PerformanceAggregator::SlowFrameDescendingComparator::operator()(const FrameSnapshot &p_left, const FrameSnapshot &p_right) const {
	return p_left.frame_time_usec == p_right.frame_time_usec ? p_left.frame < p_right.frame : p_left.frame_time_usec > p_right.frame_time_usec;
}

void PerformanceAggregator::_set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

bool PerformanceAggregator::_variant_to_finite_double(const Variant &p_value, double &r_value) {
	if (p_value.get_type() == Variant::INT) {
		r_value = double(int64_t(p_value));
	} else if (p_value.get_type() == Variant::FLOAT) {
		r_value = double(p_value);
	} else {
		return false;
	}
	return Math::is_finite(r_value);
}

Dictionary PerformanceAggregator::_serialize_stats(const HashMap<String, OnlineStats> &p_stats) {
	Dictionary result;
	for (const KeyValue<String, OnlineStats> &entry : p_stats) {
		result[entry.key] = entry.value.to_dictionary();
	}
	return result;
}

void PerformanceAggregator::_initialize_budgets() {
	budget_thresholds.resize(3);
	budget_thresholds.write[0] = { "over16_667ms", 16.667, 16667.0, 0 };
	budget_thresholds.write[1] = { "over33_333ms", 33.333, 33333.0, 0 };
	budget_thresholds.write[2] = { "over50_000ms", 50.0, 50000.0, 0 };
}

void PerformanceAggregator::reset() {
	recording_name = String();
	top_frames = 0;
	started = false;
	has_sample = false;
	first_sampled_frame = 0;
	last_sampled_frame = 0;
	captured_frames = 0;
	dropped_frame_metric_samples = 0;
	dropped_builtin_monitor_samples = 0;
	dropped_custom_monitor_samples = 0;
	frame_metrics.clear();
	builtin_monitor_metrics.clear();
	custom_monitor_metrics.clear();
	slow_frame_heap.clear();
	_initialize_budgets();
}

Error PerformanceAggregator::start(const String &p_name, int p_top_frames, String *r_error) {
	_set_error(r_error, String());
	if (p_top_frames < 0 || p_top_frames > MAX_TOP_FRAMES) {
		_set_error(r_error, "topFrames must be from 0 to 100.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_name.length() > MAX_RECORDING_NAME_LENGTH) {
		_set_error(r_error, "The recording name exceeds the maximum length.");
		return ERR_INVALID_PARAMETER;
	}
	reset();
	recording_name = p_name;
	top_frames = p_top_frames;
	if (top_frames > 0) {
		slow_frame_heap.reserve(top_frames);
	}
	started = true;
	return OK;
}

void PerformanceAggregator::_record_metric_group(const Dictionary &p_values, HashMap<String, OnlineStats> &r_stats, Dictionary &r_snapshot, uint64_t &r_dropped_samples, const String &p_skip_key) {
	const Array keys = p_values.keys();
	for (const Variant &key : keys) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			r_dropped_samples++;
			continue;
		}
		const String name = key;
		if (name == p_skip_key) {
			continue;
		}
		double value = 0.0;
		if (name.is_empty() || name.length() > MAX_METRIC_NAME_LENGTH || !_variant_to_finite_double(p_values[key], value)) {
			r_dropped_samples++;
			continue;
		}
		OnlineStats *stats = r_stats.getptr(name);
		if (!stats) {
			if (r_stats.size() >= MAX_METRICS_PER_GROUP) {
				r_dropped_samples++;
				continue;
			}
			r_stats.insert(name, OnlineStats());
			stats = r_stats.getptr(name);
		}
		stats->push_sample(value);
		r_snapshot[name] = value;
	}
}

void PerformanceAggregator::_record_budget(double p_frame_time_usec) {
	for (BudgetThreshold &threshold : budget_thresholds) {
		if (p_frame_time_usec > threshold.threshold_usec) {
			threshold.over_count++;
		}
	}
}

void PerformanceAggregator::_record_slow_frame(const FrameSnapshot &p_snapshot) {
	if (top_frames == 0) {
		return;
	}
	if (slow_frame_heap.size() < top_frames) {
		slow_frame_heap.push_back(p_snapshot);
		slow_frame_heap.sort_custom<SlowFrameAscendingComparator>();
		return;
	}
	if (SlowFrameAscendingComparator()(slow_frame_heap[0], p_snapshot)) {
		slow_frame_heap.write[0] = p_snapshot;
		slow_frame_heap.sort_custom<SlowFrameAscendingComparator>();
	}
}

Error PerformanceAggregator::push_sample(uint64_t p_frame, const Dictionary &p_frame_metrics, const Dictionary &p_builtin_monitors, const Dictionary &p_custom_monitors, String *r_error) {
	_set_error(r_error, String());
	if (!started) {
		_set_error(r_error, "The performance aggregator has not been started.");
		return ERR_UNCONFIGURED;
	}
	if (has_sample && p_frame <= last_sampled_frame) {
		_set_error(r_error, "Performance sample frame indices must increase monotonically.");
		return ERR_INVALID_PARAMETER;
	}
	double frame_time_usec = 0.0;
	if (!_variant_to_finite_double(p_frame_metrics.get("frame_time_usec", Variant()), frame_time_usec) || frame_time_usec < 0.0) {
		_set_error(r_error, "frameMetrics.frame_time_usec must be a finite non-negative number.");
		return ERR_INVALID_PARAMETER;
	}

	FrameSnapshot snapshot;
	snapshot.frame = p_frame;
	snapshot.frame_time_usec = frame_time_usec;
	OnlineStats *frame_time_stats = frame_metrics.getptr("frame_time_usec");
	if (!frame_time_stats) {
		frame_metrics.insert("frame_time_usec", OnlineStats());
		frame_time_stats = frame_metrics.getptr("frame_time_usec");
	}
	frame_time_stats->push_sample(frame_time_usec);
	snapshot.frame_metrics["frame_time_usec"] = frame_time_usec;
	_record_metric_group(p_frame_metrics, frame_metrics, snapshot.frame_metrics, dropped_frame_metric_samples, "frame_time_usec");
	_record_metric_group(p_builtin_monitors, builtin_monitor_metrics, snapshot.builtin_monitors, dropped_builtin_monitor_samples);
	_record_metric_group(p_custom_monitors, custom_monitor_metrics, snapshot.custom_monitors, dropped_custom_monitor_samples);
	_record_budget(frame_time_usec);
	_record_slow_frame(snapshot);

	if (!has_sample) {
		first_sampled_frame = p_frame;
		has_sample = true;
	}
	last_sampled_frame = p_frame;
	captured_frames++;
	return OK;
}

Dictionary PerformanceAggregator::_budget_summary() const {
	Dictionary result;
	for (const BudgetThreshold &threshold : budget_thresholds) {
		result[threshold.key] = threshold.to_dictionary(captured_frames);
	}
	return result;
}

Array PerformanceAggregator::_slow_frames_array() const {
	Vector<FrameSnapshot> frames = slow_frame_heap;
	frames.sort_custom<SlowFrameDescendingComparator>();
	Array result;
	for (const FrameSnapshot &frame : frames) {
		result.push_back(frame.to_dictionary());
	}
	return result;
}

Dictionary PerformanceAggregator::summary(bool p_completed) const {
	Dictionary recording;
	recording["name"] = recording_name;
	recording["started"] = started;
	recording["startFrame"] = has_sample ? Variant(int64_t(first_sampled_frame)) : Variant();
	recording["endFrame"] = has_sample ? Variant(int64_t(last_sampled_frame)) : Variant();
	recording["capturedFrames"] = int64_t(captured_frames);
	recording["completed"] = p_completed;

	Dictionary limits;
	limits["topFrames"] = top_frames;
	limits["maxTopFrames"] = MAX_TOP_FRAMES;
	limits["maxMetricsPerGroup"] = MAX_METRICS_PER_GROUP;
	limits["maxMetricNameLength"] = MAX_METRIC_NAME_LENGTH;
	Dictionary dropped;
	dropped["frameMetrics"] = int64_t(dropped_frame_metric_samples);
	dropped["builtinMonitors"] = int64_t(dropped_builtin_monitor_samples);
	dropped["customMonitors"] = int64_t(dropped_custom_monitor_samples);

	Dictionary result;
	result["recording"] = recording;
	result["limits"] = limits;
	result["droppedMetricSamples"] = dropped;
	result["frameMetrics"] = _serialize_stats(frame_metrics);
	result["builtinMonitorMetrics"] = _serialize_stats(builtin_monitor_metrics);
	result["customMonitorMetrics"] = _serialize_stats(custom_monitor_metrics);
	result["budgetSummary"] = _budget_summary();
	if (top_frames > 0) {
		result["slowFrames"] = _slow_frames_array();
	}
	return result;
}
