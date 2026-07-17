/**************************************************************************/
/*  performance_aggregator.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class PerformanceAggregator {
public:
	static constexpr int MAX_TOP_FRAMES = 100;
	static constexpr int MAX_METRICS_PER_GROUP = 256;
	static constexpr int MAX_METRIC_NAME_LENGTH = 256;
	static constexpr int MAX_RECORDING_NAME_LENGTH = 256;

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

		void reset(double p_quantile);
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

		void reset();
		void push_sample(double p_sample);
		Dictionary to_dictionary() const;
	};

	Error start(const String &p_name = String(), int p_top_frames = 10, String *r_error = nullptr);
	void reset();
	Error push_sample(uint64_t p_frame, const Dictionary &p_frame_metrics, const Dictionary &p_builtin_monitors = Dictionary(), const Dictionary &p_custom_monitors = Dictionary(), String *r_error = nullptr);
	Dictionary summary(bool p_completed) const;

	bool is_started() const { return started; }
	uint64_t get_captured_frames() const { return captured_frames; }
	int get_top_frames() const { return top_frames; }

private:
	struct FrameSnapshot {
		uint64_t frame = 0;
		double frame_time_usec = 0.0;
		Dictionary frame_metrics;
		Dictionary builtin_monitors;
		Dictionary custom_monitors;

		Dictionary to_dictionary() const;
	};

	struct BudgetThreshold {
		String key;
		double threshold_ms = 0.0;
		double threshold_usec = 0.0;
		uint64_t over_count = 0;

		Dictionary to_dictionary(uint64_t p_captured_frames) const;
	};

	struct SlowFrameAscendingComparator {
		bool operator()(const FrameSnapshot &p_left, const FrameSnapshot &p_right) const;
	};

	struct SlowFrameDescendingComparator {
		bool operator()(const FrameSnapshot &p_left, const FrameSnapshot &p_right) const;
	};

	static bool _variant_to_finite_double(const Variant &p_value, double &r_value);
	static Dictionary _serialize_stats(const HashMap<String, OnlineStats> &p_stats);
	static void _set_error(String *r_error, const String &p_message);

	void _initialize_budgets();
	void _record_metric_group(const Dictionary &p_values, HashMap<String, OnlineStats> &r_stats, Dictionary &r_snapshot, uint64_t &r_dropped_samples, const String &p_skip_key = String());
	void _record_budget(double p_frame_time_usec);
	void _record_slow_frame(const FrameSnapshot &p_snapshot);
	Dictionary _budget_summary() const;
	Array _slow_frames_array() const;

	String recording_name;
	int top_frames = 0;
	bool started = false;
	bool has_sample = false;
	uint64_t first_sampled_frame = 0;
	uint64_t last_sampled_frame = 0;
	uint64_t captured_frames = 0;
	uint64_t dropped_frame_metric_samples = 0;
	uint64_t dropped_builtin_monitor_samples = 0;
	uint64_t dropped_custom_monitor_samples = 0;
	HashMap<String, OnlineStats> frame_metrics;
	HashMap<String, OnlineStats> builtin_monitor_metrics;
	HashMap<String, OnlineStats> custom_monitor_metrics;
	Vector<FrameSnapshot> slow_frame_heap;
	Vector<BudgetThreshold> budget_thresholds;
};
