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
--

CREATE PERFETTO TABLE _show_maps AS
SELECT
  id,
  -- There is a variable number of spaces between show map columns so that the text is
  -- aligned. This makes it difficult to extract the column values. So we first convert
  -- to CSV format by replacing two spaces with one space 5 times before replacing a
  -- space with a comma. This is sufficient to reduce the the variable number of spaces to
  -- CSV.
  replace(
    replace(replace(replace(replace(trim(line), '  ', ' '), '  ', ' '), '  ', ' '), '  ', ' '),
    ' ',
    ','
  ) AS line,
  section
FROM android_dumpstate
WHERE
  section GLOB 'SHOW MAP*';

CREATE PERFETTO TABLE _show_maps_process AS
SELECT
  *,
  CAST(replace(str_split(section, ' ', 2), ':', '') AS INTEGER) AS pid,
  trim(str_split(section, ' ', 3)) AS process_name,
  lead(id) OVER (ORDER BY id) AS next_id
FROM _show_maps
WHERE
  trim(line) GLOB 'virtual*shared*shared*private*private*Anon*Shmem*File*Shared*Private*'
GROUP BY
  section;

CREATE PERFETTO TABLE _show_maps_process_boundary AS
SELECT
  s.*,
  process_name,
  pid
FROM _show_maps AS s
JOIN _show_maps_process AS p
  ON s.id BETWEEN p.id AND p.next_id;

-- This table represents memory mapping information from /proc/[pid]/smaps
-- All memory values are in kilobytes (KB)
CREATE PERFETTO TABLE android_dumpsys_show_map (
  -- Name of the process.
  process_name STRING,
  -- Process ID.
  pid JOINID(process.pid),
  -- Virtual Set Size in kilobytes - total virtual memory mapped by the process.
  vss_kb LONG,
  -- Resident Set Size in kilobytes - actual physical memory used by the process.
  rss_kb LONG,
  -- Proportional Set Size in kilobytes - amount of memory shared with other processes.
  pss_kb LONG,
  -- Clean shared pages in kilobytes - shared pages that haven't been modified.
  shared_clean_kb LONG,
  -- Dirty shared pages in kilobytes - shared pages that have been modified.
  shared_dirty_kb LONG,
  -- Clean private pages in kilobytes - private pages that haven't been modified.
  private_clean_kb LONG,
  -- Dirty private pages in kilobytes - private pages that have been modified.
  private_dirty_kb LONG,
  -- Swap memory in kilobytes - memory that has been moved to swap space.
  swap_kb LONG,
  -- Proportional Swap Size in kilobytes - swap shared with other processes.
  swap_pss_kb LONG,
  -- Anonymous huge pages in kilobytes - large anonymous memory regions.
  anon_huge_pages_kb LONG,
  -- Shared Memory PMD mapped in kilobytes - page middle directory mapped shared memory.
  shmem_pmd_mapped_kb LONG,
  -- File PMD mapped in kilobytes - page middle directory mapped file memory.
  file_pmd_mapped_kb LONG,
  -- Shared huge TLB in kilobytes - shared huge page table entries.
  shared_huge_tlb_kb LONG,
  -- Private huge TLB in kilobytes - private huge page table entries.
  private_hugetlb_kb LONG,
  -- Locked memory in kilobytes - memory that can't be swapped out.
  locked_kb LONG,
  -- Number of mappings of the object.
  mapping_count LONG,
  -- Path to the mapped object (file, library, etc.).
  mapped_object STRING
) AS
SELECT
  process_name,
  pid,
  CAST(str_split(line, ',', 0) AS INTEGER) AS vss_kb,
  CAST(str_split(line, ',', 1) AS INTEGER) AS rss_kb,
  CAST(str_split(line, ',', 2) AS INTEGER) AS pss_kb,
  CAST(str_split(line, ',', 3) AS INTEGER) AS shared_clean_kb,
  CAST(str_split(line, ',', 4) AS INTEGER) AS shared_dirty_kb,
  CAST(str_split(line, ',', 5) AS INTEGER) AS private_clean_kb,
  CAST(str_split(line, ',', 6) AS INTEGER) AS private_dirty_kb,
  CAST(str_split(line, ',', 7) AS INTEGER) AS swap_kb,
  CAST(str_split(line, ',', 8) AS INTEGER) AS swap_pss_kb,
  CAST(str_split(line, ',', 9) AS INTEGER) AS anon_huge_pages_kb,
  CAST(str_split(line, ',', 10) AS INTEGER) AS shmem_pmd_mapped_kb,
  CAST(str_split(line, ',', 11) AS INTEGER) AS file_pmd_mapped_kb,
  CAST(str_split(line, ',', 12) AS INTEGER) AS shared_huge_tlb_kb,
  CAST(str_split(line, ',', 13) AS INTEGER) AS private_hugetlb_kb,
  CAST(str_split(line, ',', 14) AS INTEGER) AS locked_kb,
  CAST(str_split(line, ',', 15) AS INTEGER) AS mapping_count,
  str_split(line, ',', 16) AS mapped_object
FROM _show_maps_process_boundary
WHERE
  -- Check if the row starts with a number which means it's not a delimeter or header
  -- Also exclude TOTAL since this can be computed from the table and could yield
  -- wrong results if summing pss for instance over a process.
  substr(trim(line), 1, 1) IN ('0', '1', '2', '3', '4', '5', '6', '7', '8', '9')
  AND str_split(line, ',', 16) != 'TOTAL';
