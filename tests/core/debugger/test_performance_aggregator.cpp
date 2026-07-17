/**************************************************************************/
/*  test_performance_aggregator.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "core/debugger/performance_aggregator.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_performance_aggregator);

namespace TestPerformanceAggregator {

TEST_CASE("[PerformanceAggregator] P2 quantiles and online statistics use bounded state") {
	PerformanceAggregator::P2QuantileEstimator median(0.5);
	for (int i = 1; i <= 100; i++) {
		median.push_sample(double(i));
	}
	CHECK(median.count == 100);
	CHECK(median.estimate() == doctest::Approx(50.0).epsilon(0.05));

	PerformanceAggregator::P2QuantileEstimator small(0.5);
	small.push_sample(10.0);
	small.push_sample(30.0);
	CHECK(small.estimate() == doctest::Approx(20.0));

	PerformanceAggregator::OnlineStats stats;
	stats.push_sample(1.0);
	stats.push_sample(2.0);
	stats.push_sample(3.0);
	const Dictionary result = stats.to_dictionary();
	CHECK(int64_t(result.get("count", 0)) == 3);
	CHECK(double(result.get("mean", 0.0)) == doctest::Approx(2.0));
	CHECK(double(result.get("variance", 0.0)) == doctest::Approx(2.0 / 3.0));
	CHECK(double(result.get("delta", 0.0)) == doctest::Approx(2.0));
}

static Dictionary _frame(double p_frame_time_usec, double p_process_time_usec) {
	Dictionary metrics;
	metrics["frame_time_usec"] = p_frame_time_usec;
	metrics["process_time_usec"] = p_process_time_usec;
	return metrics;
}

TEST_CASE("[PerformanceAggregator] Summaries include budgets, monitors, and bounded slow frames") {
	PerformanceAggregator aggregator;
	CHECK(aggregator.start("capture", 2) == OK);
	Dictionary builtin;
	builtin["memory/static"] = 100;
	Dictionary custom;
	custom["game/enemies"] = 3.0;
	CHECK(aggregator.push_sample(10, _frame(10000.0, 1000.0), builtin, custom) == OK);
	CHECK(aggregator.push_sample(11, _frame(20000.0, 2000.0), builtin, custom) == OK);
	CHECK(aggregator.push_sample(12, _frame(60000.0, 3000.0), builtin, custom) == OK);

	const Dictionary summary = aggregator.summary(true);
	const Dictionary recording = summary.get("recording", Dictionary());
	CHECK(recording.get("name", String()) == "capture");
	CHECK(int64_t(recording.get("startFrame", -1)) == 10);
	CHECK(int64_t(recording.get("endFrame", -1)) == 12);
	CHECK(int64_t(recording.get("capturedFrames", 0)) == 3);
	CHECK(bool(recording.get("completed", false)));
	const Dictionary frame_stats = Dictionary(summary.get("frameMetrics", Dictionary())).get("frame_time_usec", Dictionary());
	CHECK(int64_t(frame_stats.get("count", 0)) == 3);
	CHECK(double(frame_stats.get("max", 0.0)) == doctest::Approx(60000.0));
	CHECK(Dictionary(summary.get("builtinMonitorMetrics", Dictionary())).has("memory/static"));
	CHECK(Dictionary(summary.get("customMonitorMetrics", Dictionary())).has("game/enemies"));
	const Dictionary budgets = summary.get("budgetSummary", Dictionary());
	CHECK(int64_t(Dictionary(budgets.get("over16_667ms", Dictionary())).get("overCount", 0)) == 2);
	CHECK(int64_t(Dictionary(budgets.get("over33_333ms", Dictionary())).get("overCount", 0)) == 1);
	CHECK(int64_t(Dictionary(budgets.get("over50_000ms", Dictionary())).get("overCount", 0)) == 1);
	const Array slow_frames = summary.get("slowFrames", Array());
	REQUIRE(slow_frames.size() == 2);
	CHECK(int64_t(Dictionary(slow_frames[0]).get("frame", -1)) == 12);
	CHECK(int64_t(Dictionary(slow_frames[1]).get("frame", -1)) == 11);
}

TEST_CASE("[PerformanceAggregator] Start, validation, reset, and metric limits are explicit") {
	PerformanceAggregator aggregator;
	CHECK(aggregator.push_sample(1, _frame(1000.0, 10.0)) == ERR_UNCONFIGURED);
	CHECK(aggregator.start(String(), -1) == ERR_INVALID_PARAMETER);
	CHECK(aggregator.start(String(), PerformanceAggregator::MAX_TOP_FRAMES + 1) == ERR_INVALID_PARAMETER);
	CHECK(aggregator.start(String("x").repeat(PerformanceAggregator::MAX_RECORDING_NAME_LENGTH + 1), 0) == ERR_INVALID_PARAMETER);
	CHECK(aggregator.start("bounded", 0) == OK);

	Dictionary metrics = _frame(1000.0, 10.0);
	for (int i = 0; i < PerformanceAggregator::MAX_METRICS_PER_GROUP + 20; i++) {
		metrics[vformat("metric_%03d", i)] = i;
	}
	metrics["invalid"] = "text";
	CHECK(aggregator.push_sample(2, metrics) == OK);
	CHECK(aggregator.push_sample(2, metrics) == ERR_INVALID_PARAMETER);
	const Dictionary summary = aggregator.summary(false);
	CHECK_FALSE(summary.has("slowFrames"));
	CHECK(Dictionary(summary.get("frameMetrics", Dictionary())).size() == PerformanceAggregator::MAX_METRICS_PER_GROUP);
	CHECK(int64_t(Dictionary(summary.get("droppedMetricSamples", Dictionary())).get("frameMetrics", 0)) > 0);

	aggregator.reset();
	CHECK_FALSE(aggregator.is_started());
	CHECK(aggregator.get_captured_frames() == 0);
	CHECK(aggregator.start("second", 1) == OK);
	CHECK(aggregator.push_sample(100, _frame(5000.0, 50.0)) == OK);
	CHECK(int64_t(Dictionary(aggregator.summary(false).get("recording", Dictionary())).get("startFrame", -1)) == 100);
}

} // namespace TestPerformanceAggregator
