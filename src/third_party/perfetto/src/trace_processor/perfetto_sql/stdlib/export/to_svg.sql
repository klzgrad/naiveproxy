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

-- Converts Perfetto trace data into interactive SVG timeline.
-- Renders thread slices and thread states with time-proportional geometry
-- and clickable links back to Perfetto UI embedded in the SVG.
-- Enhanced with the following hierarchy:
-- 1. svg_group_key - Creates separate SVG documents
-- 2. track_group_key - Groups related tracks within an SVG (e.g., thread states + slices)
-- 3. track_group_order - Orders track groups within each SVG

-- Escape XML special characters for safe embedding in SVG.
CREATE PERFETTO FUNCTION _escape_xml(
    text STRING
)
RETURNS STRING AS
SELECT
  replace(replace(replace($text, '&', '&amp;'), '<', '&lt;'), '>', '&gt;');

-- Format nanosecond duration as human-readable string (ns/μs/ms/s).
CREATE PERFETTO FUNCTION _format_duration(
    dur LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $dur >= 1000000000
    THEN printf('%.1fs', CAST($dur AS DOUBLE) / 1000000000.0)
    WHEN $dur >= 1000000
    THEN printf('%.1fms', CAST($dur AS DOUBLE) / 1000000.0)
    WHEN $dur >= 1000
    THEN printf('%.1fμs', CAST($dur AS DOUBLE) / 1000.0)
    ELSE printf('%dns', $dur)
  END;

-- Format large numbers with K, M, G, T suffixes to 2 decimal places.
CREATE PERFETTO FUNCTION _format_large_number(
    value DOUBLE
)
RETURNS STRING AS
SELECT
  CASE
    WHEN abs($value) >= 1000000000000
    THEN printf('%.2fT', $value / 1000000000000.0)
    WHEN abs($value) >= 1000000000
    THEN printf('%.2fG', $value / 1000000000.0)
    WHEN abs($value) >= 1000000
    THEN printf('%.2fM', $value / 1000000.0)
    WHEN abs($value) >= 1000
    THEN printf('%.2fK', $value / 1000.0)
    ELSE printf('%.2f', $value)
  END;

-- Calculate pixels per nanosecond scaling factor for time-to-pixel conversion.
CREATE PERFETTO FUNCTION _pixels_per_ns(
    total_width LONG,
    ts_min LONG,
    ts_max LONG
)
RETURNS DOUBLE AS
SELECT
  CAST($total_width AS DOUBLE) / CAST($ts_max - $ts_min AS DOUBLE);

-- Calculate optimal row height based on viewport width (minimum 2px).
CREATE PERFETTO FUNCTION _row_height(
    max_width LONG
)
RETURNS LONG AS
SELECT
  max(2, CAST($max_width * 0.008 AS INTEGER));

-- Calculate counter track height (between slice height and double).
CREATE PERFETTO FUNCTION _counter_height(
    max_width LONG
)
RETURNS LONG AS
SELECT
  CAST(_row_height($max_width) * 1.5 AS INTEGER);

-- Generate deterministic color from slice name hash.
CREATE PERFETTO FUNCTION _slice_color(
    name STRING
)
RETURNS STRING AS
SELECT
  'hsl(' || (
    abs(hash($name)) % 12 * 30
  ) || ',45%,78%)';

-- Map thread state to semantic color (running=green, blocked=orange, etc.).
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

-- Truncate text with ellipsis to fit available pixel width.
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

-- Generate simple SVG rect element with optional hyperlink and text.
CREATE PERFETTO FUNCTION _svg_rect(
    x DOUBLE,
    y DOUBLE,
    width DOUBLE,
    height DOUBLE,
    fill STRING,
    title STRING,
    href STRING,
    text_content STRING
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $href IS NOT NULL
    THEN '<a href="' || _escape_xml($href) || '" target="_blank">'
    ELSE ''
  END || '<rect x="' || $x || '" y="' || $y || '" width="' || $width || '" height="' || $height || '" fill="' || $fill || '">' || CASE
    WHEN $title IS NOT NULL
    THEN '<title>' || _escape_xml($title) || '</title>'
    ELSE ''
  END || '</rect>' || coalesce($text_content, '') || CASE WHEN $href IS NOT NULL THEN '</a>' ELSE '' END;

-- Generate minimal CSS styles.
CREATE PERFETTO FUNCTION _svg_styles()
RETURNS STRING AS
SELECT
  '<style>
    rect { cursor: pointer; }
    path { cursor: pointer; }
    text { font-family: sans-serif; pointer-events: none; dominant-baseline: central; }
    a { text-decoration: none !important; }
    a:hover { text-decoration: none !important; }
  </style>';

-- Convert time intervals to pixel coordinates with grouping metadata.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _intervals_to_positions(
    intervals_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    max_width Expr,
    min_width Expr,
    use_shared_counter_scale Expr
)
RETURNS Expr AS
(
  WITH
    -- Calculate bounds per SVG group
    bounds AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        min(ts) AS ts_min,
        max(ts + dur) AS ts_max,
        max(coalesce(depth, 0)) AS max_depth
      FROM $intervals_table
      WHERE
        dur > 0
      GROUP BY
        $svg_group_key_col
    ),
    -- Calculate counter bounds per individual counter
    counter_bounds_individual AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $track_group_key_col AS track_group_key,
        name AS counter_name,
        min(counter_value) AS min_counter_value,
        max(counter_value) AS max_counter_value
      FROM $intervals_table
      WHERE
        dur > 0 AND element_type = 'counter'
      GROUP BY
        $svg_group_key_col,
        $track_group_key_col,
        name
    ),
    -- Calculate shared counter bounds across all counters in SVG
    counter_bounds_shared AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        min(counter_value) AS min_counter_value,
        max(counter_value) AS max_counter_value
      FROM $intervals_table
      WHERE
        dur > 0 AND element_type = 'counter'
      GROUP BY
        $svg_group_key_col
    ),
    -- Select the appropriate bounds based on shared counter scale setting
    counter_bounds AS (
      SELECT
        cbi.svg_group_key,
        cbi.track_group_key,
        cbi.counter_name,
        CASE
          WHEN $use_shared_counter_scale = 1
          THEN cbs.min_counter_value
          ELSE cbi.min_counter_value
        END AS min_counter_value,
        CASE
          WHEN $use_shared_counter_scale = 1
          THEN cbs.max_counter_value
          ELSE cbi.max_counter_value
        END AS max_counter_value
      FROM counter_bounds_individual AS cbi
      JOIN counter_bounds_shared AS cbs
        ON cbi.svg_group_key = cbs.svg_group_key
    ),
    scale_params AS (
      SELECT
        b.svg_group_key,
        CAST($max_width AS INTEGER) AS total_width,
        _pixels_per_ns(CAST($max_width AS INTEGER), b.ts_min, b.ts_max) AS pixels_per_ns,
        _row_height(CAST($max_width AS INTEGER)) AS row_height,
        _counter_height(CAST($max_width AS INTEGER)) AS counter_height,
        coalesce(CAST($min_width AS INTEGER), 2) AS min_cutoff,
        b.ts_min,
        b.ts_max
      FROM bounds AS b
    )
  SELECT
    $svg_group_key_col AS svg_group_key,
    $svg_group_key_col,
    $track_group_key_col AS track_group_key,
    $track_group_key_col,
    $track_group_order_col AS track_group_order,
    $track_group_order_col,
    i.*,
    (
      i.ts - sp.ts_min
    ) * sp.pixels_per_ns AS x_pixel,
    i.dur * sp.pixels_per_ns AS width_pixel,
    CASE
      WHEN i.element_type = 'slice'
      THEN 5 + coalesce(i.depth, 0) * sp.row_height
      WHEN i.element_type = 'thread_state'
      THEN 2
      WHEN i.element_type = 'counter'
      THEN 5
      ELSE 0
    END AS y_pixel,
    CASE
      WHEN i.element_type = 'thread_state'
      THEN CAST(sp.row_height / 2 AS INTEGER)
      WHEN i.element_type = 'counter'
      THEN sp.counter_height
      ELSE sp.row_height
    END AS height_pixel,
    sp.ts_min,
    sp.ts_max,
    sp.pixels_per_ns,
    sp.total_width,
    sp.min_cutoff,
    sp.counter_height,
    coalesce(cb.min_counter_value, 0) AS min_counter_value,
    coalesce(cb.max_counter_value, 1) AS max_counter_value
  FROM $intervals_table AS i
  JOIN scale_params AS sp
    ON i.$svg_group_key_col = sp.svg_group_key
  LEFT JOIN counter_bounds AS cb
    ON i.$svg_group_key_col = cb.svg_group_key
    AND i.$track_group_key_col = cb.track_group_key
    AND i.name = cb.counter_name
  WHERE
    i.dur > 0 AND i.dur * sp.pixels_per_ns >= sp.min_cutoff
);

-- Render slice interval as simple SVG rect with text overlay when space permits.
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
      $name || ' (' || _format_duration($dur) || ')',
      $href,
      CASE
        WHEN $width_pixel >= 15
        THEN '<text x="' || (
          $x_pixel + $width_pixel / 2
        ) || '" y="' || (
          $y_pixel + $height_pixel / 2
        ) || '" text-anchor="middle" font-size="11" fill="#333">' || _escape_xml(_fit_text($name, CAST($width_pixel AS INTEGER) - 4)) || '</text>'
        ELSE ''
      END
    )
  END;

-- Render thread state interval as simple SVG rect.
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
      'Thread State: ' || $state || ' (' || _format_duration($dur) || ')',
      $href,
      NULL
    )
  END;

-- Render counter value as step in filled area chart with proper negative value handling.
CREATE PERFETTO FUNCTION _counter_to_svg(
    x_pixel DOUBLE,
    y_pixel DOUBLE,
    width_pixel DOUBLE,
    height_pixel DOUBLE,
    value DOUBLE,
    max_value DOUBLE,
    min_value DOUBLE,
    name STRING,
    href STRING,
    min_pixel_width LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $width_pixel < $min_pixel_width
    THEN ''
    ELSE CASE
      WHEN $href IS NOT NULL
      THEN '<a href="' || _escape_xml($href) || '" target="_blank">'
      ELSE ''
    END || '<rect x="' || $x_pixel || '" y="' || CASE
      WHEN $value >= 0
      THEN CASE
        WHEN $min_value >= 0
        THEN $y_pixel + $height_pixel - (
          $height_pixel * (
            $value - $min_value
          ) / (
            $max_value - $min_value
          )
        )
        ELSE $y_pixel + $height_pixel * (
          $max_value / (
            $max_value - $min_value
          )
        ) - (
          $height_pixel * $value / (
            $max_value - $min_value
          )
        )
      END
      ELSE CASE
        WHEN $max_value <= 0
        THEN $y_pixel
        ELSE $y_pixel + $height_pixel * (
          $max_value / (
            $max_value - $min_value
          )
        )
      END
    END || '" width="' || $width_pixel || '" height="' || CASE
      WHEN $value >= 0
      THEN CASE
        WHEN $min_value >= 0
        THEN $height_pixel * (
          $value - $min_value
        ) / (
          $max_value - $min_value
        )
        ELSE $height_pixel * $value / (
          $max_value - $min_value
        )
      END
      ELSE CASE
        WHEN $max_value <= 0
        THEN $height_pixel * (
          $value - $max_value
        ) / (
          $max_value - $min_value
        )
        ELSE $height_pixel * abs($value) / (
          $max_value - $min_value
        )
      END
    END || '" fill="' || CASE WHEN $value >= 0 THEN 'steelblue' ELSE 'coral' END || '">' || '<title>' || _escape_xml($name || ': ' || printf('%.1f', $value)) || '</title>' || '</rect>' || CASE WHEN $href IS NOT NULL THEN '</a>' ELSE '' END
  END;

-- Generate track SVG from positioned elements without labels.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _svg_from_positions(
    positions_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    top_margin Expr
)
RETURNS Expr AS
(
  WITH
    track_params AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $track_group_key_col AS track_group_key,
        total_width,
        min_cutoff,
        min(y_pixel) AS track_top,
        max(y_pixel + height_pixel) AS track_bottom,
        CAST($top_margin AS INTEGER) AS top_margin
      FROM $positions_table
      GROUP BY
        $svg_group_key_col,
        $track_group_key_col
      LIMIT 1
    )
  SELECT
    tp.svg_group_key,
    tp.track_group_key,
    '<g transform="translate(0,' || tp.top_margin || ')">' || coalesce(
      (
        SELECT
          GROUP_CONCAT(
            CASE
              WHEN p.element_type = 'thread_state'
              THEN _thread_state_to_svg(
                p.x_pixel,
                cast_double!(p.y_pixel),
                p.width_pixel,
                cast_double!(p.height_pixel),
                p.state,
                p.io_wait,
                p.blocked_function,
                p.dur,
                p.href,
                tp.min_cutoff
              )
              WHEN p.element_type = 'counter'
              THEN _counter_to_svg(
                p.x_pixel,
                cast_double!(p.y_pixel),
                p.width_pixel,
                cast_double!(p.height_pixel),
                p.counter_value,
                p.max_counter_value,
                p.min_counter_value,
                p.name,
                p.href,
                tp.min_cutoff
              )
              ELSE _slice_to_svg(p.x_pixel, cast_double!(p.y_pixel), p.width_pixel, cast_double!(p.height_pixel), p.name, p.dur, p.href, tp.min_cutoff)
            END,
            ''
          )
        FROM $positions_table AS p
        WHERE
          p.$svg_group_key_col = tp.svg_group_key
          AND p.$track_group_key_col = tp.track_group_key
        ORDER BY
          p.ts,
          p.depth,
          p.dur DESC
      ),
      ''
    ) || '</g>' AS track_svg,
    tp.track_bottom + tp.top_margin AS track_height
  FROM track_params AS tp
);

-- Generate track SVG from positioned elements with track labels.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _svg_from_positions_with_label(
    positions_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    label_text ColumnName,
    label_top_margin Expr,
    label_gap Expr
)
RETURNS Expr AS
(
  WITH
    track_params AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $track_group_key_col AS track_group_key,
        $label_text AS label_text,
        total_width,
        min_cutoff,
        min(y_pixel) AS track_top,
        max(y_pixel + height_pixel) AS track_bottom,
        -- Get counter-specific info for y-axis labels (with NULL safety)
        max(CASE WHEN element_type = 'counter' THEN max_counter_value ELSE NULL END) AS max_counter_value,
        max(CASE WHEN element_type = 'counter' THEN min_counter_value ELSE NULL END) AS min_counter_value,
        max(CASE WHEN element_type = 'counter' THEN 1 ELSE 0 END) AS is_counter_track
      FROM $positions_table
      GROUP BY
        $svg_group_key_col,
        $track_group_key_col
    ),
    -- Calculate counter scaling parameters with NULL safety
    counter_scale AS (
      SELECT
        tp.*,
        -- Prevent division by zero and handle NULL values
        CASE
          WHEN tp.is_counter_track = 1
          AND NOT tp.max_counter_value IS NULL
          AND NOT tp.min_counter_value IS NULL AND tp.max_counter_value != tp.min_counter_value
          THEN tp.max_counter_value - tp.min_counter_value
          ELSE 1.0
        END AS counter_range,
        -- Calculate zero line Y position
        CASE
          WHEN tp.is_counter_track = 1
          AND NOT tp.max_counter_value IS NULL AND tp.min_counter_value IS NOT NULL
          THEN CASE
            WHEN tp.min_counter_value >= 0
            THEN 5 + tp.track_bottom
            WHEN tp.max_counter_value <= 0
            THEN 5
            ELSE 5 + tp.track_bottom * (
              tp.max_counter_value / (
                tp.max_counter_value - tp.min_counter_value
              )
            )
          END
          ELSE 5 + tp.track_bottom
        END AS zero_y
      FROM track_params AS tp
    ),
    -- Generate counter path data
    counter_path_points AS (
      SELECT
        cs.svg_group_key,
        cs.track_group_key,
        p.x_pixel,
        p.x_pixel + p.width_pixel AS x_end,
        -- Calculate Y position for counter value
        CASE
          WHEN p.counter_value >= 0
          THEN CASE
            WHEN cs.min_counter_value >= 0
            THEN 5 + cs.track_bottom - (
              cs.track_bottom * (
                p.counter_value - cs.min_counter_value
              ) / cs.counter_range
            )
            ELSE cs.zero_y - (
              cs.track_bottom * p.counter_value / cs.counter_range
            )
          END
          ELSE CASE
            WHEN cs.max_counter_value <= 0
            THEN 5 + (
              cs.track_bottom * (
                p.counter_value - cs.max_counter_value
              ) / cs.counter_range
            )
            ELSE cs.zero_y + (
              cs.track_bottom * abs(p.counter_value) / cs.counter_range
            )
          END
        END AS y_value,
        cs.zero_y,
        cs.total_width
      FROM counter_scale AS cs
      JOIN $positions_table AS p
        ON p.$svg_group_key_col = cs.svg_group_key
        AND p.$track_group_key_col = cs.track_group_key
        AND p.element_type = 'counter'
      WHERE
        cs.is_counter_track = 1
      ORDER BY
        p.ts
    ),
    -- Build the SVG path string
    counter_path AS (
      SELECT
        cpp.svg_group_key,
        cpp.track_group_key,
        '<path d="M0,' || cpp.zero_y || ' ' || GROUP_CONCAT(
          'L' || cpp.x_pixel || ',' || cpp.y_value || ' L' || cpp.x_end || ',' || cpp.y_value,
          ' '
        ) || ' L' || cpp.total_width || ',' || cpp.zero_y || ' L0,' || cpp.zero_y || ' Z" ' || 'fill="steelblue" fill-opacity="0.7" stroke="steelblue" stroke-width="1"/>' AS counter_svg
      FROM counter_path_points AS cpp
      GROUP BY
        cpp.svg_group_key,
        cpp.track_group_key
    ),
    -- Generate rect slice/thread_state elements
    rect_elements AS (
      SELECT
        p.$svg_group_key_col AS svg_group_key,
        p.$track_group_key_col AS track_group_key,
        GROUP_CONCAT(
          CASE
            WHEN p.element_type = 'thread_state'
            THEN _thread_state_to_svg(
              p.x_pixel,
              cast_double!(p.y_pixel),
              p.width_pixel,
              cast_double!(p.height_pixel),
              p.state,
              p.io_wait,
              p.blocked_function,
              p.dur,
              p.href,
              tp.min_cutoff
            )
            WHEN p.element_type = 'counter'
            THEN ''
            ELSE _slice_to_svg(p.x_pixel, cast_double!(p.y_pixel), p.width_pixel, cast_double!(p.height_pixel), p.name, p.dur, p.href, tp.min_cutoff)
          END,
          ''
        ) AS elements_svg
      FROM $positions_table AS p
      JOIN track_params AS tp
        ON p.$svg_group_key_col = tp.svg_group_key
        AND p.$track_group_key_col = tp.track_group_key
      GROUP BY
        p.$svg_group_key_col,
        p.$track_group_key_col
    )
  -- Final assembly
  SELECT
    tp.svg_group_key,
    tp.track_group_key,
    -- Track label
    '<text x="5" y="15" font-size="11" fill="#333">' || _escape_xml(cast_string!(tp.label_text)) || '</text>' || CASE
      WHEN tp.is_counter_track = 1
      AND NOT tp.max_counter_value IS NULL AND tp.min_counter_value IS NOT NULL
      THEN '<text x="5" y="30" font-size="9" fill="#000">' || _format_large_number(tp.max_counter_value) || '</text>' || CASE
        WHEN tp.min_counter_value < 0
        THEN '<text x="5" y="' || (
          30 + tp.track_bottom
        ) || '" font-size="9" fill="#000">' || _format_large_number(tp.min_counter_value) || '</text>'
        ELSE ''
      END
      ELSE ''
    END || '<g transform="translate(0,20)">' || coalesce(cp.counter_svg, '') || coalesce(re.elements_svg, '') || '</g>' AS track_svg,
    tp.track_bottom + 20 AS track_height
  FROM track_params AS tp
  LEFT JOIN counter_path AS cp
    ON tp.svg_group_key = cp.svg_group_key AND tp.track_group_key = cp.track_group_key
  LEFT JOIN rect_elements AS re
    ON tp.svg_group_key = re.svg_group_key AND tp.track_group_key = re.track_group_key
);

-- Generate unlabeled tracks grouped by svg_group_key and track_group_key.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _generate_tracks_by_group(
    positions_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    start_order Expr,
    order_step Expr,
    top_margin Expr
)
RETURNS Expr AS
(
  WITH
    grouped_keys AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $track_group_key_col AS track_group_key,
        min($track_group_order_col) AS track_group_order
      FROM $positions_table
      GROUP BY
        $svg_group_key_col,
        $track_group_key_col
    ),
    track_svgs_with_group AS (
      SELECT
        gk.svg_group_key,
        gk.track_group_key,
        gk.track_group_order AS track_order,
        (
          SELECT
            track_svg
          FROM _svg_from_positions!(
              (SELECT * FROM $positions_table p WHERE p.$svg_group_key_col = gk.svg_group_key AND p.$track_group_key_col = gk.track_group_key),
              $svg_group_key_col, $track_group_key_col, $top_margin)
        ) AS track_svg,
        (
          SELECT
            track_height
          FROM _svg_from_positions!(
              (SELECT * FROM $positions_table p WHERE p.$svg_group_key_col = gk.svg_group_key AND p.$track_group_key_col = gk.track_group_key),
              $svg_group_key_col, $track_group_key_col, $top_margin)
        ) AS track_height
      FROM grouped_keys AS gk
    )
  SELECT
    svg_group_key AS $svg_group_key_col,
    svg_group_key,
    track_group_key,
    track_order,
    track_svg,
    track_height
  FROM track_svgs_with_group
);

-- Generate labeled tracks grouped by svg_group_key and track_group_key.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _generate_tracks_by_group_with_label(
    positions_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    start_order Expr,
    order_step Expr,
    top_margin Expr,
    label_text ColumnName,
    label_gap Expr
)
RETURNS Expr AS
(
  WITH
    grouped_keys AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $track_group_key_col AS track_group_key,
        min($track_group_order_col) AS track_group_order,
        min($label_text) AS label_text
      FROM $positions_table
      GROUP BY
        $svg_group_key_col,
        $track_group_key_col
    ),
    track_svgs_with_group AS (
      SELECT
        gk.svg_group_key,
        gk.track_group_key,
        gk.track_group_order AS track_order,
        (
          SELECT
            track_svg
          FROM _svg_from_positions_with_label!(
              (SELECT * FROM $positions_table p WHERE p.$svg_group_key_col = gk.svg_group_key AND p.$track_group_key_col = gk.track_group_key),
              $svg_group_key_col, $track_group_key_col, gk.label_text, $top_margin, $label_gap)
        ) AS track_svg,
        (
          SELECT
            track_height
          FROM _svg_from_positions_with_label!(
              (SELECT * FROM $positions_table p WHERE p.$svg_group_key_col = gk.svg_group_key AND p.$track_group_key_col = gk.track_group_key),
              $svg_group_key_col, $track_group_key_col, gk.label_text, $top_margin, $label_gap)
        ) AS track_height
      FROM grouped_keys AS gk
    )
  SELECT
    svg_group_key AS $svg_group_key_col,
    svg_group_key,
    track_group_key,
    track_order,
    track_svg,
    track_height
  FROM track_svgs_with_group
);

-- Combine track SVGs into complete SVG documents with layout and styling.
-- svg_group_key: separate SVG documents.
CREATE PERFETTO MACRO _combine_track_svgs(
    track_svgs_table TableOrSubquery,
    svg_group_key_col ColumnName,
    total_width Expr,
    left_margin Expr
)
RETURNS Expr AS
(
  WITH
    ordered_tracks AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        track_svg,
        track_height,
        coalesce(
          track_order,
          row_number() OVER (PARTITION BY $svg_group_key_col ORDER BY track_order)
        ) AS track_order
      FROM $track_svgs_table
    ),
    positioned_tracks AS (
      SELECT
        svg_group_key,
        track_svg,
        track_height,
        track_order,
        sum(track_height) OVER (PARTITION BY svg_group_key ORDER BY track_order ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) - track_height AS y_offset
      FROM ordered_tracks
    ),
    layout_params AS (
      SELECT
        svg_group_key,
        CAST($total_width AS INTEGER) AS total_width,
        CAST($left_margin AS INTEGER) AS left_margin,
        max(track_height + y_offset) AS total_content_height
      FROM positioned_tracks
      GROUP BY
        svg_group_key
    )
  SELECT
    lp.svg_group_key,
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' || (
      lp.total_width + lp.left_margin + 10
    ) || ' ' || (
      lp.total_content_height + 25
    ) || '">' || _svg_styles() || '<g transform="translate(' || lp.left_margin || ',5)">' || coalesce(
      (
        SELECT
          GROUP_CONCAT('<g transform="translate(0,' || pt.y_offset || ')">' || pt.track_svg || '</g>', '')
        FROM positioned_tracks AS pt
        WHERE
          pt.svg_group_key = lp.svg_group_key
        ORDER BY
          pt.track_order
      ),
      ''
    ) || '</g></svg>' AS svg
  FROM layout_params AS lp
);

-- Convert slice data to positioned elements for rendering.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _slice_intervals_to_positions(
    slice_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    max_width Expr,
    min_width Expr
)
RETURNS Expr AS
(
  WITH
    intervals_with_type AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $svg_group_key_col,
        $track_group_key_col AS track_group_key,
        $track_group_key_col,
        $track_group_order_col AS track_group_order,
        $track_group_order_col,
        utid,
        ts,
        dur,
        'slice' AS element_type,
        name,
        href,
        depth,
        NULL AS state,
        NULL AS io_wait,
        NULL AS blocked_function,
        NULL AS counter_value
      FROM $slice_table
    )
  SELECT
    *
  FROM _intervals_to_positions!(
    intervals_with_type, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, $min_width, 0
  )
);

-- Convert thread state data to positioned elements for rendering.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _thread_state_intervals_to_positions(
    thread_state_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    max_width Expr,
    min_width Expr
)
RETURNS Expr AS
(
  WITH
    intervals_with_type AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $svg_group_key_col,
        $track_group_key_col AS track_group_key,
        $track_group_key_col,
        $track_group_order_col AS track_group_order,
        $track_group_order_col,
        utid,
        ts,
        dur,
        'thread_state' AS element_type,
        NULL AS name,
        href,
        NULL AS depth,
        state,
        io_wait,
        blocked_function,
        NULL AS counter_value
      FROM $thread_state_table
    )
  SELECT
    *
  FROM _intervals_to_positions!(
    intervals_with_type, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, $min_width, 0
  )
);

-- Convert counter data to positioned elements for rendering.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _counter_intervals_to_positions(
    counter_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    max_width Expr,
    min_width Expr,
    use_shared_counter_scale Expr
)
RETURNS Expr AS
(
  WITH
    intervals_with_type AS (
      SELECT
        $svg_group_key_col AS svg_group_key,
        $svg_group_key_col,
        $track_group_key_col AS track_group_key,
        $track_group_key_col,
        $track_group_order_col AS track_group_order,
        $track_group_order_col,
        NULL AS utid,
        ts,
        dur,
        'counter' AS element_type,
        name,
        href,
        NULL AS depth,
        NULL AS state,
        NULL AS io_wait,
        NULL AS blocked_function,
        value AS counter_value
      FROM $counter_table
    )
  SELECT
    *
  FROM _intervals_to_positions!(
    intervals_with_type, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, $min_width, $use_shared_counter_scale
  )
);

-- Main convenience macro to create complete SVG timeline from slice and thread state tables.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _svg_timeline(
    slice_table TableOrSubquery,
    thread_state_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    max_width Expr,
    left_margin Expr
)
RETURNS Expr AS
(
  WITH
    slice_positions AS (
      SELECT
        *
      FROM _slice_intervals_to_positions!(
        $slice_table, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, 2
      )
    ),
    thread_state_positions AS (
      SELECT
        *
      FROM _thread_state_intervals_to_positions!(
        $thread_state_table, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, 2
      )
    ),
    slice_tracks AS (
      SELECT
        *
      FROM _generate_tracks_by_group!(
        slice_positions, $svg_group_key_col, $track_group_key_col, $track_group_order_col,
        0, 1, 0
      )
    ),
    thread_state_tracks AS (
      SELECT
        *
      FROM _generate_tracks_by_group_with_label!(
        thread_state_positions, $svg_group_key_col, $track_group_key_col, $track_group_order_col,
        0, 1, 0, $track_group_key_col, 10
      )
    ),
    all_tracks AS (
      SELECT
        *
      FROM slice_tracks
      UNION ALL
      SELECT
        *
      FROM thread_state_tracks
    )
  SELECT
    *
  FROM _combine_track_svgs!(
    all_tracks, $svg_group_key_col, $max_width, $left_margin
  )
);

-- Enhanced main convenience macro to create complete SVG timeline from slice, thread state, and counter tables.
-- svg_group_key: separate SVG documents.
-- track_group_key: related tracks within SVG.
-- track_group_order: vertical ordering within track groups.
CREATE PERFETTO MACRO _svg_timeline_with_counters(
    slice_table TableOrSubquery,
    thread_state_table TableOrSubquery,
    counter_table TableOrSubquery,
    svg_group_key_col ColumnName,
    track_group_key_col ColumnName,
    track_group_order_col ColumnName,
    max_width Expr,
    left_margin Expr,
    use_shared_counter_scale Expr
)
RETURNS Expr AS
(
  WITH
    slice_positions AS (
      SELECT
        *
      FROM _slice_intervals_to_positions!(
        $slice_table, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, 2
      )
    ),
    thread_state_positions AS (
      SELECT
        *
      FROM _thread_state_intervals_to_positions!(
        $thread_state_table, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, 0
      )
    ),
    counter_positions AS (
      SELECT
        *
      FROM _counter_intervals_to_positions!(
        $counter_table, $svg_group_key_col, $track_group_key_col, $track_group_order_col, $max_width, 1, $use_shared_counter_scale
      )
    ),
    slice_tracks AS (
      SELECT
        *
      FROM _generate_tracks_by_group!(
        slice_positions, $svg_group_key_col, $track_group_key_col, $track_group_order_col,
        0, 1, 0
      )
    ),
    thread_state_tracks AS (
      SELECT
        *
      FROM _generate_tracks_by_group_with_label!(
        thread_state_positions, $svg_group_key_col, $track_group_key_col, $track_group_order_col,
        0, 1, 0, $track_group_key_col, 10
      )
    ),
    counter_tracks AS (
      SELECT
        *
      FROM _generate_tracks_by_group_with_label!(
        counter_positions, $svg_group_key_col, $track_group_key_col, $track_group_order_col,
        0, 1, 0, $track_group_key_col, 10
      )
    ),
    all_tracks AS (
      SELECT
        *
      FROM slice_tracks
      UNION ALL
      SELECT
        *
      FROM thread_state_tracks
      UNION ALL
      SELECT
        *
      FROM counter_tracks
    )
  SELECT
    *
  FROM _combine_track_svgs!(
    all_tracks, $svg_group_key_col, $max_width, $left_margin
  )
);
