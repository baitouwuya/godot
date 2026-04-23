/**************************************************************************/
/*  cli_performance_recorder.h                                            */
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

#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "main/performance.h"

class CLIPerformanceRecorder {
public:
	struct Options {
		bool enabled = false;
		int start_frame = 0;
		int end_frame = -1;
		int top_frames = 10;
		String samples_file;
		// Internal parse bookkeeping keeps validation strict even when the caller
		// passes the same values as the defaults.
		bool start_frame_set = false;
		bool end_frame_set = false;
		bool top_frames_set = false;
		bool samples_file_set = false;
	};

	struct P2QuantileEstimator {
		double quantile = 0.0;
		uint64_t count = 0;
		double initial_samples[5] = {};
		double marker_heights[5] = {};
		double marker_positions[5] = {};
		double desired_positions[5] = {};
		double desired_increments[5] = {};

		P2QuantileEstimator();
		explicit P2QuantileEstimator(double p_quantile);

		void push_sample(double p_sample);
		double estimate() const;

	private:
		void _initialize_markers();
		static void _sort_samples(double *p_samples, uint64_t p_count);
		double _parabolic(int p_index, int p_direction) const;
		double _linear(int p_index, int p_direction) const;
	};

	struct OnlineStats {
		uint64_t count = 0;
		double min = 0.0;
		double max = 0.0;
		double mean = 0.0;
		double m2 = 0.0;
		double first = 0.0;
		double last = 0.0;
		P2QuantileEstimator p50;
		P2QuantileEstimator p95;
		P2QuantileEstimator p99;

		OnlineStats();

		void push_sample(double p_sample);
		Dictionary to_dictionary() const;
	};

	static bool parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error);
	static Error validate_options(const Options &p_options, bool p_editor, bool p_project_manager, bool p_cmdline_tool, String &r_error);
	static bool should_include_builtin_monitor(Performance::Monitor p_monitor);
	static bool should_include_custom_monitor_value(const Variant &p_value);

	explicit CLIPerformanceRecorder(const Options &p_options);
	~CLIPerformanceRecorder();

	Error initialize(String &r_error);
	void close();

	void record_frame(uint64_t p_frame_index, uint64_t p_frame_time_usec, uint64_t p_process_time_usec, uint64_t p_physics_process_time_usec, uint64_t p_navigation_process_time_usec, double p_physics_frame_time_sec);
	Dictionary build_summary(bool p_completed) const;
	void print_summary_stdout(bool p_completed) const;

	uint64_t get_captured_frames() const { return captured_frames; }
	uint64_t get_last_sampled_frame() const { return last_sampled_frame; }
	bool is_completed() const;

private:
	struct FrameSnapshot {
		uint64_t frame = 0;
		uint64_t frame_time_usec = 0;
		Dictionary frame_metrics;
		Dictionary builtin_monitors;
		Dictionary custom_monitors;

		Dictionary to_dictionary(bool p_recorded = false) const;
	};

	struct BudgetThresholdStats {
		String key;
		double threshold_ms = 0.0;
		double threshold_usec = 0.0;
		uint64_t over_count = 0;

		Dictionary to_dictionary(uint64_t p_captured_frames) const;
	};

	struct SlowFrameAscendingComparator {
		bool operator()(const FrameSnapshot &p_a, const FrameSnapshot &p_b) const;
	};

	struct SlowFrameDescendingComparator {
		bool operator()(const FrameSnapshot &p_a, const FrameSnapshot &p_b) const;
	};

	static bool _consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error);
	static Error _validate_samples_output_path(const String &p_path, String &r_error);
	static Dictionary _serialize_stats_map(const HashMap<String, OnlineStats> &p_stats_map);

	static double _variant_to_double(const Variant &p_value);

	void _record_metric(HashMap<String, OnlineStats> &r_metrics, const String &p_key, double p_sample, Dictionary *r_current_samples = nullptr);
	void _record_budget(double p_frame_time_usec);
	void _record_slow_frame(const FrameSnapshot &p_snapshot);
	void _write_frame_sample(const FrameSnapshot &p_snapshot);
	Dictionary _build_budget_summary() const;
	Array _build_slow_frames_array() const;

	Options options;
	HashMap<String, OnlineStats> frame_metrics;
	HashMap<String, OnlineStats> builtin_monitor_metrics;
	HashMap<String, OnlineStats> custom_monitor_metrics;
	Vector<FrameSnapshot> slow_frame_heap;
	Vector<BudgetThresholdStats> budget_thresholds;
	Ref<FileAccess> samples_output_file;
	uint64_t captured_frames = 0;
	uint64_t last_sampled_frame = 0;
	bool has_sampled_frame = false;
};
