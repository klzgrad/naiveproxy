--
-- Copyright 2025 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- TODO(fouly): elaborate on how to select chunk_id after chunk_id aggregates are created
-- TODO(fouly): explain where chunk_id comes from after __intrinsic_etm_v4_chunk is no longer intrinsic

-- This is a table that extracts the file_path for the binary and the relative address for each ETM instruction in a specific ETM chunk.
-- The most common use case will be to use this data to help symbolize the addresses in order to map instructions back to the code that caused them.
-- To get ETM data you need to have enabled enable_perfetto_etm_importer in your gn args.
CREATE PERFETTO FUNCTION _linux_perf_etm_metadata(
    -- ID of the chunk.
    chunk_id LONG
)
RETURNS TABLE (
  -- Name of the file containing the instruction.
  file_name STRING,
  -- Relative program counter of the instruction.
  rel_pc LONG,
  -- The mapping id of the instruction.
  mapping_id LONG,
  -- The address of the instruction.
  address LONG
) AS
SELECT
  __intrinsic_file.name AS file_name,
  __intrinsic_etm_iterate_instruction_range.address - stack_profile_mapping.start + stack_profile_mapping.exact_offset + __intrinsic_elf_file.load_bias AS rel_pc,
  __intrinsic_etm_decode_chunk.mapping_id AS mapping_id,
  __intrinsic_etm_iterate_instruction_range.address AS address
FROM __intrinsic_etm_decode_chunk($chunk_id)
JOIN __intrinsic_etm_iterate_instruction_range
  ON __intrinsic_etm_decode_chunk.instruction_range = __intrinsic_etm_iterate_instruction_range.instruction_range
JOIN stack_profile_mapping
  ON __intrinsic_etm_decode_chunk.mapping_id = stack_profile_mapping.id
JOIN __intrinsic_elf_file
  ON stack_profile_mapping.build_id = __intrinsic_elf_file.build_id
JOIN __intrinsic_file
  ON __intrinsic_elf_file.file_id = __intrinsic_file.id;
