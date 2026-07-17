/**************************************************************************/
/*  test_mcp_log_analyzer.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "editor/mcp/providers/mcp_log_analyzer.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_log_analyzer);

namespace TestMCPLogAnalyzer {

TEST_CASE("[MCP] Log analyzer folds changing values and repeated issue stacks") {
	const String text =
			"tick 10\n"
			"tick 20\n"
			"ERROR: Failed request 1000000\n"
			"   at: run (res://worker.gd:12)\n"
			"   GDScript backtrace (most recent call first):\n"
			"   [0] run (res://worker.gd:12)\n"
			"ERROR: Failed request 2000000\n"
			"   at: run (res://worker.gd:12)\n"
			"   GDScript backtrace (most recent call first):\n"
			"   [0] run (res://worker.gd:12)";
	MCPLogAnalyzer::Options options;
	options.first_line = 41;
	options.total_lines = 100;
	options.requested_lines = 10;
	options.truncated = true;
	const Dictionary result = MCPLogAnalyzer::analyze(text, options);
	const Dictionary analysis = result.get("analysis", Dictionary());
	CHECK(int(analysis.get("windowLinesAnalyzed", 0)) == 10);
	CHECK(int(analysis.get("totalLines", 0)) == 100);
	CHECK(bool(analysis.get("truncated", false)));
	CHECK(int(analysis.get("foldedRepeatCount", 0)) == 2);
	CHECK(int(analysis.get("issueBlockCount", 0)) == 2);
	const Dictionary counts = result.get("counts", Dictionary());
	CHECK(int(counts.get("error", 0)) == 2);
	const Array issues = result.get("recentIssueBlocks", Array());
	REQUIRE(issues.size() == 1);
	const Dictionary issue = issues[0];
	CHECK(int(issue.get("repeat", 0)) == 2);
	CHECK(issue.get("headline", String()) == "Failed request <int>");
	CHECK(Dictionary(issue.get("callsite", Dictionary())).get("file", String()) == "res://worker.gd");
	CHECK(Array(Dictionary(issue.get("stackSummary", Dictionary())).get("frames", Array())).size() == 1);
	const Array templates = result.get("topTemplates", Array());
	REQUIRE(templates.size() == 2);
	CHECK(int(Dictionary(templates[0]).get("count", 0)) == 2);
	CHECK(Array(Dictionary(templates[0]).get("numericSlots", Array())).size() == 1);
}

TEST_CASE("[MCP] Log analyzer returns a compact fallback tail without issue blocks") {
	const Dictionary result = MCPLogAnalyzer::analyze("ready\nready\npath res://level2/mesh3.tres");
	const Array tail = result.get("fallbackTail", Array());
	REQUIRE(tail.size() == 2);
	CHECK(int(Dictionary(tail[0]).get("repeat", 0)) == 2);
	CHECK(Dictionary(tail[1]).get("text", String()) == "path res://level2/mesh3.tres");
}

} // namespace TestMCPLogAnalyzer
