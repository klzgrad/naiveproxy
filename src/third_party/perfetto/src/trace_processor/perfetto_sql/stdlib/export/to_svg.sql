--
-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE intervals.intersect;

-- Converts Perfetto trace data into interactive SVG timeline.
-- Renders thread slices and thread states with time-proportional geometry
-- and clickable links back to Perfetto UI embedded in the SVG.
--

-- Core utility functions
CREATE PERFETTO FUNCTION _escape_xml(
    text STRING
)
RETURNS STRING AS
SELECT
  replace(replace(replace($text, '&', '&amp;'), '<', '&lt;'), '>', '&gt;');

CREATE PERFETTO FUNCTION _format_duration(
    dur LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $dur >= 1000000000
    THEN printf('%.2f s', CAST($dur AS DOUBLE) / 1000000000.0)
    WHEN $dur >= 1000000
    THEN printf('%.2f ms', CAST($dur AS DOUBLE) / 1000000.0)
    WHEN $dur >= 1000
    THEN printf('%.2f μs', CAST($dur AS DOUBLE) / 1000.0)
    ELSE printf('%d ns', $dur)
  END;

-- Convert nanosecond time spans to pixel coordinates
CREATE PERFETTO FUNCTION _pixels_per_ns(
    total_width LONG,
    ts_min LONG,
    ts_max LONG
)
RETURNS DOUBLE AS
SELECT
  CAST($total_width AS DOUBLE) / CAST($ts_max - $ts_min AS DOUBLE);

-- Calculate optimal row height based on viewport width (minimum 2px)
CREATE PERFETTO FUNCTION _row_height(
    max_width LONG
)
RETURNS LONG AS
SELECT
  max(2, CAST($max_width * 0.008 AS INTEGER));

-- Truncate text with ellipsis when it exceeds available pixel width (assumes 6.5px avg char width)
CREATE PERFETTO FUNCTION _fit_text(
    text STRING,
    available_width LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $available_width < 12
    THEN ''
    WHEN length($text) * 6.5 <= $available_width
    THEN $text
    WHEN $available_width >= 25
    THEN substr($text, 1, CAST((
      $available_width - 18
    ) / 6.5 AS INTEGER)) || '...'
    ELSE substr($text, 1, CAST($available_width / 6.5 AS INTEGER))
  END;

CREATE PERFETTO FUNCTION _get_link()
RETURNS STRING AS
SELECT
  str_value
FROM metadata
WHERE
  name = 'trace_uuid';

-- Generate deterministic HSL color from string hash
CREATE PERFETTO FUNCTION _slice_color(
    name STRING
)
RETURNS STRING AS
SELECT
  'hsl(' || (
    abs(hash($name)) % 12 * 30
  ) || ',45%,78%)';

-- Map thread states to semantic colors (running=green, blocked=orange, etc.)
CREATE PERFETTO FUNCTION _state_color(
    state STRING,
    io_wait LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN lower($state) = 'running'
    THEN '#2f7d31'
    WHEN lower($state) IN ('r', 'r+')
    THEN '#99ba34'
    WHEN CAST($io_wait AS INTEGER) = 1
    THEN '#ff9800'
    WHEN lower($state) = 's'
    THEN '#a0a0a0'
    WHEN lower($state) = 'd'
    THEN '#a35b58'
    WHEN lower($state) = 'z'
    THEN '#8b5cf6'
    WHEN lower($state) = 't'
    THEN '#f97316'
    ELSE '#9ca3af'
  END;

-- Generate SVG rect with optional hyperlink wrapper and text overlay
CREATE PERFETTO FUNCTION _svg_rect(
    x DOUBLE,
    y DOUBLE,
    width DOUBLE,
    height DOUBLE,
    fill STRING,
    stroke STRING,
    css_class STRING,
    title STRING,
    href STRING,
    text_content STRING
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $href IS NOT NULL
    THEN '<a href="' || _escape_xml($href) || '" target="blank">'
    ELSE ''
  END || '<g transform="translate(' || $x || ',' || $y || ')">' || '<rect x="0" y="0" width="' || $width || '" height="' || $height || '" ' || 'stroke="' || coalesce($stroke, 'none') || '" ' || 'fill="' || $fill || '" ' || 'shape-rendering="crispEdges" ' || 'class="' || coalesce($css_class, '') || '">' || CASE
    WHEN $title IS NOT NULL
    THEN '<title>' || _escape_xml($title) || '</title>'
    ELSE ''
  END || '</rect>' || coalesce($text_content, '') || '</g>' || CASE WHEN $href IS NOT NULL THEN '</a>' ELSE '' END;

-- Generate SVG text element with typography and positioning
CREATE PERFETTO FUNCTION _svg_text(
    x DOUBLE,
    y DOUBLE,
    content STRING,
    font_size LONG,
    font_weight STRING,
    text_anchor STRING,
    fill STRING,
    css_class STRING
)
RETURNS STRING AS
SELECT
  '<text x="' || $x || '" y="' || $y || '" ' || 'font-family="Inter,system-ui,-apple-system,BlinkMacSystemFont,Segue UI,Roboto,sans-serif" ' || 'font-size="' || $font_size || '" font-weight="' || coalesce($font_weight, '400') || '" ' || 'text-anchor="' || coalesce($text_anchor, 'start') || '" ' || 'fill="' || $fill || '" ' || 'dominant-baseline="central" ' || CASE WHEN $css_class IS NOT NULL THEN 'class="' || $css_class || '" ' ELSE '' END || 'style="pointer-events:none;user-select:none;">' || _escape_xml($content) || '</text>';

-- Generate thread name label positioned above track
CREATE PERFETTO FUNCTION _svg_track_label(
    utid LONG,
    thread_name STRING,
    track_offset LONG,
    track_height LONG,
    font_size LONG,
    label_height LONG
)
RETURNS STRING AS
SELECT
  '<g transform="translate(5,' || (
    $track_offset - $label_height + (
      $label_height / 2
    )
  ) || ')">' || _svg_text(
    CAST(25 AS DOUBLE),
    CAST(0 AS DOUBLE),
    coalesce($thread_name, 'Thread ' || $utid),
    $font_size,
    '500',
    'start',
    'rgba(45,45,45,0.8)',
    NULL
  ) || '</g>';

-- SVG filter definitions for subtle drop shadows
CREATE PERFETTO FUNCTION _svg_defs()
RETURNS STRING AS
SELECT
  '<defs>
  <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
    <feDropShadow dx="0" dy="0.5" stdDeviation="0.5" flood-opacity="0.08"/>
  </filter>
</defs>';

-- CSS styles for hover effects and visual hierarchy
CREATE PERFETTO FUNCTION _svg_styles()
RETURNS STRING AS
SELECT
  '<style>
* {
  box-sizing: border-box;
}


rect {
  filter: url(#shadow);
  stroke: none;
  shape-rendering: crispEdges;
}


rect:hover {
  filter: url(#shadow) drop-shadow(0 1px 3px rgba(0,0,0,0.15));
}


.clickable-slice, .clickable-state {
  cursor: pointer !important;
}


.clickable-slice:hover, .clickable-state:hover {
  stroke: rgba(37,99,235,0.3) !important;
  stroke-width: 1 !important;
  filter: url(#shadow) drop-shadow(0 1px 2px rgba(37,99,235,0.1)) !important;
}


.thread-state {
  opacity: 0.9;
}


.thread-state:hover {
  opacity: 1;
}


text {
  dominant-baseline: central;
  text-rendering: optimizeLegibility;
  pointer-events: none;
  user-select: none;
}


.chart-title {
  pointer-events: all !important;
  cursor: pointer !important;
}


.chart-title:hover {
  fill: #2563eb !important;
}


a {
  text-decoration: none;
  cursor: pointer !important;
  pointer-events: all !important;
}


title {
  transition: opacity 0.1s ease-in;
}
</style>';

-- Generate clickable chart title with optional hyperlink
CREATE PERFETTO FUNCTION _svg_chart_title(
    chart_label STRING,
    chart_href STRING,
    top_margin LONG,
    font_size LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $chart_label IS NOT NULL
    THEN CASE
      WHEN $chart_href IS NOT NULL
      THEN '<a href="' || _escape_xml($chart_href) || '" target="blank">' || '<text x="5.0" y="' || CAST($top_margin * 0.1 AS DOUBLE) || '" ' || 'font-family="Inter,system-ui,-apple-system,BlinkMacSystemFont,Segue UI,Roboto,sans-serif" ' || 'font-size="' || $font_size || '" font-weight="600" ' || 'text-anchor="start" fill="#374151" ' || 'dominant-baseline="central" ' || 'class="chart-title" ' || 'style="cursor:pointer;">' || _escape_xml($chart_label) || '</text></a>'
      ELSE _svg_text(
        CAST(8 AS DOUBLE),
        CAST($top_margin * 0.6 AS DOUBLE),
        $chart_label,
        $font_size,
        '600',
        'start',
        '#374151',
        NULL
      )
    END
    ELSE ''
  END;

-- Build SVG rect for trace slice with text overlay when width permits
CREATE PERFETTO FUNCTION _slice_to_svg(
    x_pixel DOUBLE,
    y_pixel DOUBLE,
    width_pixel DOUBLE,
    height_pixel DOUBLE,
    name STRING,
    dur LONG,
    href STRING,
    min_pixel_width LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $width_pixel < $min_pixel_width
    THEN ''
    ELSE _svg_rect(
      $x_pixel,
      $y_pixel,
      $width_pixel,
      $height_pixel,
      _slice_color($name),
      'none',
      CASE WHEN $href IS NOT NULL THEN 'clickable-slice' ELSE 'slice' END,
      $name || ' (' || _format_duration($dur) || ')',
      $href,
      CASE
        WHEN $width_pixel >= 15
        THEN _svg_text(
          $width_pixel / 2,
          $height_pixel / 2,
          _fit_text($name, CAST($width_pixel AS INTEGER) - 4),
          11,
          '400',
          'middle',
          'rgba(45,45,45,0.9)',
          NULL
        )
        ELSE ''
      END
    )
  END;

-- Build SVG rect for thread scheduling state
CREATE PERFETTO FUNCTION _thread_state_to_svg(
    x_pixel DOUBLE,
    y_pixel DOUBLE,
    width_pixel DOUBLE,
    height_pixel DOUBLE,
    state STRING,
    io_wait LONG,
    blocked_function STRING,
    dur LONG,
    href STRING,
    min_pixel_width LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $width_pixel < $min_pixel_width
    THEN ''
    ELSE _svg_rect(
      $x_pixel,
      $y_pixel,
      $width_pixel,
      $height_pixel,
      _state_color($state, $io_wait),
      'rgba(255,255,255,0.3)',
      CASE WHEN $href IS NOT NULL THEN 'clickable-state' ELSE 'thread-state' END,
      'Thread State: ' || $state || ' (' || _format_duration($dur) || ')' || CASE WHEN $blocked_function IS NOT NULL THEN ' - ' || $blocked_function ELSE '' END,
      $href,
      NULL
    )
  END;

-- Calculate time bounds and max depth across slices and thread states
CREATE PERFETTO MACRO _calculate_bounds(
    slice_table TableOrSubquery,
    thread_state_table TableOrSubquery,
    group_key ColumnName
)
RETURNS Expr AS
(
  SELECT
    group_key AS $group_key,
    min(ts_min) AS ts_min,
    max(ts_max) AS ts_max,
    max(max_depth) AS max_depth,
    sum(slice_count) AS total_count
  FROM (
    SELECT
      $group_key AS group_key,
      min(ts) AS ts_min,
      max(ts + dur) AS ts_max,
      max(depth) AS max_depth,
      count(*) AS slice_count
    FROM $slice_table
    WHERE
      dur > 0
    GROUP BY
      group_key
    UNION ALL
    SELECT
      $group_key AS group_key,
      min(ts) AS ts_min,
      max(ts + dur) AS ts_max,
      0 AS max_depth,
      count(*) AS slice_count
    FROM $thread_state_table
    WHERE
      dur > 0
    GROUP BY
      group_key
  )
  GROUP BY
    group_key
);

-- Assign sequential track indices to threads for vertical layout
CREATE PERFETTO MACRO _calculate_track_layout(
    slice_table TableOrSubquery,
    thread_state_table TableOrSubquery,
    group_key ColumnName
)
RETURNS Expr AS
(
  SELECT
    $group_key AS group_key,
    utid,
    thread_name,
    max(depth) AS max_depth_in_track,
    row_number() OVER (PARTITION BY $group_key ORDER BY utid) - 1 AS track_index
  FROM (
    SELECT
      $group_key,
      utid,
      thread_name,
      depth
    FROM $slice_table
    WHERE
      dur > 0
    UNION ALL
    SELECT
      $group_key,
      utid,
      thread_name,
      0 AS depth
    FROM $thread_state_table
    WHERE
      dur > 0
  )
  GROUP BY
    utid,
    group_key
);

-- Calculate viewport scaling and layout parameters
CREATE PERFETTO MACRO _calculate_scale_params(
    bounds_table TableOrSubquery,
    track_layout_table TableOrSubquery,
    max_width Expr,
    min_pixel_width Expr,
    group_key ColumnName
)
RETURNS Expr AS
(
  SELECT
    $group_key AS group_key,
    b.ts_min,
    b.ts_max,
    CAST($max_width AS INTEGER) AS total_width,
    _pixels_per_ns(CAST($max_width AS INTEGER), b.ts_min, b.ts_max) AS pixels_per_ns,
    _row_height(CAST($max_width AS INTEGER)) AS row_height,
    coalesce(CAST($min_pixel_width AS INTEGER), 2) AS min_pixel_cutoff,
    -- 50px per depth level + 10px padding
    (
      (
        (
          SELECT
            max(max_depth_in_track)
          FROM $track_layout_table
        ) + 1
      ) * 50
    ) + 10 AS track_spacing,
    max(25, CAST($max_width * 0.04 AS INTEGER)) AS left_margin,
    max(15, CAST($max_width * 0.008 AS INTEGER)) AS label_height
  FROM $bounds_table AS b
  GROUP BY
    group_key
);

-- Convert slice intervals to pixel coordinates and SVG metadata
CREATE PERFETTO MACRO _calculate_slice_positions(
    slice_table TableOrSubquery,
    scale_params_table TableOrSubquery,
    track_layout_table TableOrSubquery,
    group_key ColumnName
)
RETURNS Expr AS
(
  SELECT
    s.$group_key AS group_key,
    'slice' AS element_type,
    s.utid,
    s.name,
    s.ts,
    s.dur,
    s.depth,
    s.href,
    NULL AS state,
    NULL AS io_wait,
    NULL AS blocked_function,
    coalesce(tl.track_index, 0) AS track_index,
    tl.thread_name,
    -- Time offset from start converted to pixels
    (
      s.ts - sp.ts_min
    ) * sp.pixels_per_ns AS x_pixel,
    -- Duration converted to pixel width
    s.dur * sp.pixels_per_ns AS width_pixel,
    -- Vertical position: track offset + depth offset
    coalesce(tl.track_index, 0) * sp.track_spacing + CAST(sp.row_height / 2 AS INTEGER) + 20 + s.depth * sp.row_height AS y_pixel,
    sp.row_height AS height_pixel,
    sp.ts_min,
    sp.ts_max,
    sp.pixels_per_ns,
    sp.total_width,
    sp.left_margin,
    sp.min_pixel_cutoff
  FROM $slice_table AS s
  JOIN $scale_params_table AS sp
    ON s.$group_key = sp.group_key
  LEFT JOIN $track_layout_table AS tl
    ON s.utid = tl.utid AND s.$group_key = tl.group_key
  WHERE
    s.dur > 0 AND s.dur * sp.pixels_per_ns >= sp.min_pixel_cutoff
);

-- Convert thread state intervals to pixel coordinates (positioned below slices)
CREATE PERFETTO MACRO _calculate_thread_state_positions(
    thread_state_table TableOrSubquery,
    scale_params_table TableOrSubquery,
    track_layout_table TableOrSubquery,
    group_key ColumnName
)
RETURNS Expr AS
(
  SELECT
    ts.$group_key AS group_key,
    'thread_state' AS element_type,
    ts.utid,
    'Thread State: ' || ts.state AS name,
    ts.ts,
    ts.dur,
    0 AS depth,
    ts.href,
    ts.state,
    ts.io_wait,
    ts.blocked_function,
    coalesce(tl.track_index, 0) AS track_index,
    tl.thread_name,
    (
      ts.ts - sp.ts_min
    ) * sp.pixels_per_ns AS x_pixel,
    ts.dur * sp.pixels_per_ns AS width_pixel,
    -- Position below slice track with 15px offset
    coalesce(tl.track_index, 0) * sp.track_spacing + 15 AS y_pixel,
    -- Half height of slice rows
    CAST(sp.row_height / 2 AS INTEGER) AS height_pixel,
    sp.ts_min,
    sp.ts_max,
    sp.pixels_per_ns,
    sp.total_width,
    sp.left_margin,
    sp.min_pixel_cutoff
  FROM $thread_state_table AS ts
  JOIN $scale_params_table AS sp
    ON ts.$group_key = sp.group_key
  LEFT JOIN $track_layout_table AS tl
    ON ts.utid = tl.utid AND ts.$group_key = tl.group_key
  WHERE
    ts.dur > 0 AND ts.dur * sp.pixels_per_ns >= sp.min_pixel_cutoff
);

-- Transform time intervals into positioned SVG elements with a unified coordinate
-- per group_key.
CREATE PERFETTO MACRO _intervals_to_positions(
    slice_table TableOrSubquery,
    thread_state_table TableOrSubquery,
    max_width Expr,
    min_pixel_width Expr,
    group_key ColumnName
)
RETURNS Expr AS
(
  WITH
    bounds AS (
      SELECT
        *
      FROM _calculate_bounds !($slice_table, $thread_state_table, $group_key)
    ),
    track_layout AS (
      SELECT
        *
      FROM _calculate_track_layout !($slice_table, $thread_state_table, $group_key)
    ),
    scale_params AS (
      SELECT
        *
      FROM _calculate_scale_params !(bounds, track_layout, $max_width, $min_pixel_width, $group_key)
    ),
    slice_positions AS (
      SELECT
        *
      FROM _calculate_slice_positions !($slice_table, scale_params, track_layout, $group_key)
    ),
    thread_state_positions AS (
      SELECT
        *
      FROM _calculate_thread_state_positions
          !($thread_state_table, scale_params, track_layout, $group_key)
    )
  SELECT
    *
  FROM slice_positions
  UNION ALL
  SELECT
    *
  FROM thread_state_positions
  ORDER BY
    track_index,
    ts,
    depth,
    dur DESC
);

-- Generate complete SVG document from positioned elements.
-- See intervals_to_positions on how to generate positioned elements.
CREATE PERFETTO MACRO _svg_from_positions(
    positions_table TableOrSubquery,
    chart_label Expr,
    chart_href Expr,
    group_key ColumnName
)
RETURNS TableOrSubQuery AS
(
  WITH
    position_params AS (
      SELECT
        $group_key AS group_key,
        total_width,
        left_margin,
        ts_min,
        ts_max,
        pixels_per_ns,
        min_pixel_cutoff,
        count(DISTINCT track_index) AS track_count,
        max(y_pixel + height_pixel) AS max_y
      FROM $positions_table
      GROUP BY
        group_key
    ),
    layout_params AS (
      SELECT
        pp.*,
        CASE
          WHEN $chart_label IS NOT NULL
          THEN max(15, CAST(pp.total_width * 0.01 AS INTEGER))
          ELSE 5
        END AS top_margin,
        max(12, CAST(pp.total_width * 0.008 AS INTEGER)) AS chart_title_font_size,
        max(10, CAST(pp.total_width * 0.007 AS INTEGER)) AS track_label_font_size,
        max(12, CAST(pp.total_width * 0.008 AS INTEGER)) AS label_height
      FROM position_params AS pp
      GROUP BY
        group_key
    ),
    track_layout AS (
      SELECT
        $group_key AS group_key,
        utid,
        thread_name,
        track_index,
        min(y_pixel) AS track_offset
      FROM $positions_table
      GROUP BY
        utid,
        group_key
    )
  SELECT
    lp.group_key,
    '<svg xmlns="http://www.w3.org/2000/svg" ' || 'viewBox="0 0 ' || (
      lp.total_width + lp.left_margin + 10
    ) || ' ' || (
      lp.max_y + lp.top_margin + lp.label_height + 50
    ) || '" ' || 'style="background: #fefdfb; font-family: system-ui;">' || _svg_defs() || _svg_styles() || _svg_chart_title($chart_label, $chart_href, lp.top_margin, lp.chart_title_font_size) || '<g id="track-labels">' || coalesce(
      (
        SELECT
          GROUP_CONCAT(
            _svg_track_label(
              tl.utid,
              tl.thread_name,
              tl.track_offset + 20,
              50,
              lp.track_label_font_size,
              lp.label_height
            ),
            ''
          )
        FROM track_layout AS tl
        WHERE
          tl.group_key = lp.group_key
      ),
      ''
    ) || '</g>' || '<g transform="translate(' || lp.left_margin || ',' || (
      lp.top_margin + lp.label_height
    ) || ')">' || coalesce(
      (
        SELECT
          GROUP_CONCAT(
            CASE
              WHEN p.element_type = 'thread_state'
              THEN _thread_state_to_svg(
                p.x_pixel,
                cast_double !(p.y_pixel),
                p.width_pixel,
                cast_double !(p.height_pixel),
                p.state,
                p.io_wait,
                p.blocked_function,
                p.dur,
                p.href,
                p.min_pixel_cutoff
              )
              ELSE _slice_to_svg(p.x_pixel, cast_double !(p.y_pixel), p.width_pixel, cast_double !(p.height_pixel), p.name, p.dur, p.href, p.min_pixel_cutoff)
            END,
            ''
          )
        FROM $positions_table AS p
        WHERE
          p.group_key = lp.group_key
        ORDER BY
          p.track_index,
          p.ts,
          p.depth,
          p.dur DESC
      ),
      ''
    ) || '</g></svg>' AS svg
  FROM layout_params AS lp
);

-- Generates a complete SVG timeline visualization directly from tables of
-- time-stamped intervals (slices and thread states).
CREATE PERFETTO MACRO _svg_from_intervals(
    -- A table or subquery of the primary trace slices to render.
    -- Expected columns: `utid`, `ts`, `dur`, `depth`, `name`, `href` (Can be NULL).
    slice_table TableOrSubquery,
    -- A table or subquery of the thread states to render beneath the slices.
    -- Expected columns: `utid`, `ts`, `dur`, `state`, `io_wait` (Can be NULL),
    -- `blocked_function` (Can be NULL), `href` (Can be NULL).
    thread_state_table TableOrSubquery,
    -- The total pixel width for the timeline content area.
    max_width Expr,
    -- The minimum pixel width required for an interval to be rendered.
    -- Used to cull very small intervals and improve readability.
    min_pixel_width Expr,
    -- An optional label/title to display at the top of the chart. Can be NULL.
    chart_label Expr,
    -- An optional URL to make the chart title a hyperlink. Can be NULL.
    chart_href Expr,
    -- A column name used to partition the data. A separate SVG is generated
    -- for each distinct value in this column. Can be NULL
    group_key ColumnName
)
RETURNS Expr AS
(
  _svg_from_positions
    !(
      _intervals_to_positions
        !($slice_table, $thread_state_table, $max_width, $min_pixel_width, $group_key),
      $chart_label,
      $chart_href,
      $group_key)
);
