--
-- Copyright 2024 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.
--

INCLUDE PERFETTO MODULE chrome.histograms;

DROP VIEW IF EXISTS HistogramSummaryTable;
CREATE PERFETTO VIEW HistogramSummaryTable AS
SELECT
    hist.name AS histname,
    CAST(AVG(hist.value) AS INTEGER) AS mean_histval,
    COUNT(*) AS hist_count,
    CAST(SUM(hist.value) AS INTEGER) AS sum_histval,
    CAST(MAX(hist.value) AS INTEGER) AS max_histval,
    CAST(PERCENTILE(hist.value, 90) AS INTEGER) AS p90_histval,
    CAST(PERCENTILE(hist.value, 50) AS INTEGER) AS p50_histval
FROM chrome_histograms hist
GROUP BY hist.name;

DROP VIEW IF EXISTS chrome_histogram_summaries_output;
CREATE PERFETTO VIEW chrome_histogram_summaries_output AS
SELECT ChromeHistogramSummaries(
    'histogram_summary', (
        SELECT RepeatedField(
            HistogramSummary(
                'name', histname,
                'mean', mean_histval,
                'count', hist_count,
                'sum', sum_histval,
                'max', max_histval,
                'p90', p90_histval,
                'p50', p50_histval
            )
        )
        FROM HistogramSummaryTable
    )
);
