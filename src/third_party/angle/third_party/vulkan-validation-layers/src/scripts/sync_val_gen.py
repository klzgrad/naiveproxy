#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2020 The Khronos Group Inc.
# Copyright (c) 2015-2020 Valve Corporation
# Copyright (c) 2015-2020 LunarG, Inc.
# Copyright (c) 2015-2020 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Author: John Zulauf <jzulauf@lunarg.com>

# Script operation controls
debug_table_parse = False
debug_in_bit_order = False
debug_top_level = False
debug_queue_caps = False
debug_stage_order_parse = False
debug_bubble_insert = False
experimental_ordering = False

# Some DRY constants
host_stage = 'VK_PIPELINE_STAGE_HOST_BIT'
top_stage ='VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT'
bot_stage ='VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT'

# Snipped from chapters/synchronization.txt -- tag v1.2.135
snippet_access_types_supported = '''
[[synchronization-access-types-supported]]
.Supported access types
[cols="50,50",options="header"]
|====
|Access flag                                                  | Supported pipeline stages
|ename:VK_ACCESS_INDIRECT_COMMAND_READ_BIT                    | ename:VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
|ename:VK_ACCESS_INDEX_READ_BIT                               | ename:VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
|ename:VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT                    | ename:VK_PIPELINE_STAGE_VERTEX_INPUT_BIT

|ename:VK_ACCESS_UNIFORM_READ_BIT                             |
ifdef::VK_NV_mesh_shader[]
                                                               ename:VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV, ename:VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV,
endif::VK_NV_mesh_shader[]
ifdef::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
                                                               ename:VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
endif::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
                                                               ename:VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, ename:VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT, ename:VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, ename:VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, ename:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, or ename:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT

|ename:VK_ACCESS_SHADER_READ_BIT                              |
ifdef::VK_NV_mesh_shader[]
                                                               ename:VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV, ename:VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV,
endif::VK_NV_mesh_shader[]
ifdef::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
                                                               ename:VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
endif::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
                                                               ename:VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, ename:VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT, ename:VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, ename:VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, ename:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, or ename:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT

|ename:VK_ACCESS_SHADER_WRITE_BIT                             |
ifdef::VK_NV_mesh_shader[]
                                                               ename:VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV, ename:VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV,
endif::VK_NV_mesh_shader[]
ifdef::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
                                                               ename:VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
endif::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
                                                               ename:VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, ename:VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT, ename:VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, ename:VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, ename:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, or ename:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT

|ename:VK_ACCESS_INPUT_ATTACHMENT_READ_BIT                    | ename:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
|ename:VK_ACCESS_COLOR_ATTACHMENT_READ_BIT                    | ename:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
|ename:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT                   | ename:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
|ename:VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT            | ename:VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, or ename:VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
|ename:VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT           | ename:VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, or ename:VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
|ename:VK_ACCESS_TRANSFER_READ_BIT                            | ename:VK_PIPELINE_STAGE_TRANSFER_BIT
|ename:VK_ACCESS_TRANSFER_WRITE_BIT                           | ename:VK_PIPELINE_STAGE_TRANSFER_BIT
|ename:VK_ACCESS_HOST_READ_BIT                                | ename:VK_PIPELINE_STAGE_HOST_BIT
|ename:VK_ACCESS_HOST_WRITE_BIT                               | ename:VK_PIPELINE_STAGE_HOST_BIT
|ename:VK_ACCESS_MEMORY_READ_BIT                              | Any
|ename:VK_ACCESS_MEMORY_WRITE_BIT                             | Any
ifdef::VK_EXT_blend_operation_advanced[]
|ename:VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT    | ename:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
endif::VK_EXT_blend_operation_advanced[]
ifdef::VK_NV_device_generated_commands[]
|ename:VK_ACCESS_COMMAND_PREPROCESS_READ_BIT_NV               | ename:VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_NV
|ename:VK_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_NV              | ename:VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_NV
endif::VK_NV_device_generated_commands[]
ifdef::VK_EXT_conditional_rendering[]
|ename:VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT           | ename:VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT
endif::VK_EXT_conditional_rendering[]
ifdef::VK_NV_shading_rate_image[]
|ename:VK_ACCESS_SHADING_RATE_IMAGE_READ_BIT_NV               | ename:VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV
endif::VK_NV_shading_rate_image[]
ifdef::VK_EXT_transform_feedback[]
|ename:VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT             | ename:VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT
|ename:VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT     | ename:VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT
|ename:VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT      | ename:VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
endif::VK_EXT_transform_feedback[]
ifdef::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
|ename:VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR          | ename:VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, or ename:VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
|ename:VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR         | ename:VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
endif::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
ifdef::VK_EXT_fragment_density_map[]
|ename:VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT            | ename:VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT
endif::VK_EXT_fragment_density_map[]
|====
'''

# use simplest filtering to assure completest list
def ParseAccessType(table_text):
    preproc = ''
    access_stage_table = {}
    skip = False
    outside_table = True
    for line in table_text.split('\n'):
        if not line.strip():
            continue
        elif outside_table:
            if line.startswith('|===='):
                outside_table = False
            continue
        elif line.startswith('ifndef'):
            # Negative preproc filter
            preproc = line
            skip = True # for now just ignore subset filtering
        elif line.startswith('ifdef'):
            # Positive preproc filter
            preproc = line
            skip = False # No support for nested directives...
        elif line.startswith('endif'):
            # Positive preproc filter
            skip = False
        elif (line.startswith('|ename:') or line.startswith('        ')) and not skip:
            if debug_table_parse:
                print("// line {}".format(line))
            cols = line.split('|')
            if debug_table_parse:
                print("len(cols)", len(cols), cols)
            if len(cols) == 3:
                stage_column = cols[2]
                access_enum = cols[1].split(':')[1].strip()
                access_stage_table[access_enum]=[]
            else:
                stage_column = cols[0].strip()
            if debug_table_parse:
                print("stage_column:", stage_column)
            if stage_column.startswith(' Any'):
                continue
            if stage_column.startswith(' None required'):
                continue
            stage_column = stage_column.replace(', or ',', ') # Oxford comma
            stage_column = stage_column.replace(' or ',', ') # !Oxford comma
            stage_column = stage_column.replace(', ',',')
            stage_column = stage_column.rstrip(',')
            stages = stage_column.split(',')
            if debug_table_parse:
                print("stages", len(stages), stages)
            if len(stages) < 1:
                continue
            elif not stages[0]:
                continue
            stages_lens = [len(s.split(':')) for s in stages]
            stage_enums = [ s.split(':')[1].strip('or ') for s in stages]

            access_stage_table[access_enum] += stage_enums
            if(debug_table_parse):
                print("// access_stage_table[{}]: {}".format(access_enum, "|".join(access_stage_table[access_enum])))

    return access_stage_table

def CreateStageAccessTable(stage_order, access_stage_table):
    stage_access_table = { stage: list() for stage in stage_order}
    for access, stages in access_stage_table.items():
        for stage in stages:
            stage_access_table[stage].append(access)

    return stage_access_table

# Snipped from chapters/synchronization.txt -- tag v1.2.135
snippet_pipeline_stages_order = '''
[[synchronization-pipeline-stages-types]]
The order and set of pipeline stages executed by a given command is
determined by the command's pipeline type, as described below:

ifndef::VK_NV_mesh_shader[]
For the graphics pipeline, the following stages occur in this order:
endif::VK_NV_mesh_shader[]
ifdef::VK_NV_mesh_shader[]
For the graphics primitive shading pipeline, the following stages occur in
this order:
endif::VK_NV_mesh_shader[]

  * ename:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
  * ename:VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
  * ename:VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
  * ename:VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
  * ename:VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
  * ename:VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT
  * ename:VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT
ifdef::VK_EXT_transform_feedback[]
  * ename:VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT
endif::VK_EXT_transform_feedback[]
ifdef::VK_NV_shading_rate_image[]
  * ename:VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV
endif::VK_NV_shading_rate_image[]
  * ename:VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
  * ename:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
  * ename:VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
  * ename:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
  * ename:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT

ifdef::VK_NV_mesh_shader[]
For the graphics mesh shading pipeline, the following stages occur in this
order:

  * ename:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
  * ename:VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
  * ename:VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV
  * ename:VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV
ifdef::VK_NV_shading_rate_image[]
  * ename:VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV
endif::VK_NV_shading_rate_image[]
  * ename:VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
  * ename:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
  * ename:VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
  * ename:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
  * ename:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
endif::VK_NV_mesh_shader[]

ifdef::VK_EXT_fragment_density_map[]
For graphics pipeline commands executing in a render pass with a fragment
density map attachment, the following pipeline stage where the fragment
density map read happens has no particular order relative to the other
stages, except that it is logically earlier than
ename:VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT:

  * ename:VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT
  * ename:VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
endif::VK_EXT_fragment_density_map[]

For the compute pipeline, the following stages occur in this order:

  * ename:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
  * ename:VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
  * ename:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
  * ename:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT

ifdef::VK_EXT_conditional_rendering[]
The conditional rendering stage is formally part of both the graphics, and
the compute pipeline.
The pipeline stage where the predicate read happens has unspecified order
relative to other stages of these pipelines:

  * ename:VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT
endif::VK_EXT_conditional_rendering[]

For the transfer pipeline, the following stages occur in this order:

  * ename:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
  * ename:VK_PIPELINE_STAGE_TRANSFER_BIT
  * ename:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT

For host operations, only one pipeline stage occurs, so no order is
guaranteed:

  * ename:VK_PIPELINE_STAGE_HOST_BIT

ifdef::VK_NV_device_generated_commands[]
For the command preprocessing pipeline, the following stages occur in this
order:

  * ename:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
  * ename:VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_NV
  * ename:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
endif::VK_NV_device_generated_commands[]

ifdef::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
For the ray tracing shader pipeline, only one pipeline stage occurs, so no
order is guaranteed:

  * ename:VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR

For ray tracing acceleration structure operations, only one pipeline stage
occurs, so no order is guaranteed:

  * ename:VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR

endif::VK_NV_ray_tracing,VK_KHR_ray_tracing[]

'''

pipeline_name_labels = {
    'GRAPHICS': 'For the graphics primitive shading pipeline',
    'MESH': 'For the graphics mesh shading pipeline',
    'COMPUTE': 'For the compute pipeline',
    'TRANSFER': 'For the transfer pipeline',
    'HOST': 'For host operations, only one pipeline',
    'COMMAND_PROCESS': 'For the command preprocessing pipeline',
    'RAY_TRACING_SHADE': 'For the ray tracing shader pipelin',
    'ACCELERATION_STRUCTURE': 'For ray tracing acceleration structure operations'
}

def StageOrderListFromSet(stage_order, stage_set):
    return [ stage for stage in stage_order if stage in stage_set]

def BubbleInsertStages(stage_order, prior, subseq):
    # Get a fixed list for the order in which we'll add... this is a 'seed' order and will guarantee repeatilbity as it fixes the
    # order items not sorted relative to each other.  Any seed would do, even alphabetical
    stage_set = set(prior.keys()).union(set(subseq.keys()))

    #stage_set_ordered = StageOrderListFromSet(stage_order, stage_set)
    # Come up with a consistent stage order seed that will leave stage types (extension, core, vendor) grouped
    # Reverse the strings -- a spoonerism of the whole stage string, to put the type first
    #     spoonerism -- https://en.wikipedia.org/wiki/Spoonerism
    stage_spooner = list()
    acme = '_AAAAAA'
    stage_spooner = sorted([ stage.replace('_BIT', acme)[::-1] for stage in stage_set if stage != host_stage])
    print('ZZZ BUBBLE spooner\n', '\n '.join(stage_spooner))

    # De-spoonerize
    stage_set_ordered = [ stage[::-1].replace(acme, '_BIT') for stage in stage_spooner]
    print('ZZZ BUBBLE de-spooner\n', '\n '.join(stage_set_ordered))

    stage_set_ordered.append(host_stage)
    stages = [ stage_set_ordered.pop(0) ]

    for stage in stage_set_ordered:
        if debug_bubble_insert: print('BUBBLE adding stage:', stage)
        inserted = False;
        for i in range(len(stages)):
            if stages[i] in subseq[stage]:
                stages.insert(i, stage)
                inserted = True
                break
        if not inserted:
            stages.append(stage)
        if debug_bubble_insert: print('BUBBLE result:', stages)

    print('BUBBLE\n', '\n '.join(stages))
    return stages

def ParsePipelineStageOrder(stage_order, stage_order_snippet, config) :
    pipeline_name = ''
    stage_lists = {}
    touchup = set()
    stage_entry_prefix = '* ename:'
    all_stages = set()
    list_started = False
    # Parse the snippet
    for line in stage_order_snippet.split('\n'):
        line = line.strip()
        if debug_stage_order_parse: print ('STAGE_ORDER', line)
        if not line:
            if debug_stage_order_parse: print ('STAGE_ORDER', 'skip empty')
            if list_started:
                if debug_stage_order_parse: print ('STAGE_ORDER', 'EOL')
                pipeline_name = ''
            continue
        if line.startswith('For') :
            for name, label in pipeline_name_labels.items():
                if line.startswith(label):
                    pipeline_name = name
                    stage_lists[name] = list()
                    list_started = False
                    break
            if debug_stage_order_parse: print ('STAGE_ORDER', 'new pipeline', pipeline_name)
            continue
        if line.startswith(stage_entry_prefix):
            if debug_stage_order_parse: print ('STAGE_ORDER', 'new entry')
            list_started = True
            stage = line.lstrip(stage_entry_prefix)
            all_stages.add(stage)
            if pipeline_name:
                if debug_stage_order_parse: print ('STAGE_ORDER', 'normal entry')
                stage_lists[pipeline_name].append(stage)
            else:
                # See if we've seen this before.  Touchups must be novel
                novel_stage = all([not stage in stage_list for stage_list in stage_lists.values()])
                if (novel_stage):
                    if debug_stage_order_parse: print ('STAGE_ORDER', 'touchup entry')
                    touchup.add(stage)
                else:
                    if debug_stage_order_parse: print ('STAGE_ORDER', 'context entry')

    if debug_stage_order_parse:
        print('STAGE_ORDER', 'PARSED PIPELINES')
        for pipeline_name, stage_list in stage_lists.items():
            print(pipeline_name,"|".join(stage_list))

        print('STAGE_ORDER', 'PARSED all_stages')
        print('all_stages',"|".join(all_stages))


    # Create earlier/later maps
    prior = { stage:set() for stage in all_stages }
    subseq = { stage:set() for stage in all_stages }

    for pipeline_name, stage_list in stage_lists.items():
        for i in range(len(stage_list)):
            prior[stage_list[i]].update(stage_list[:i])
            subseq[stage_list[i]].update(stage_list[i+1:])

    # Touch up the stages that don't quite parse right
    touchups_done = set()

    # FDP Stage is only constrained to be before VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
    fdp_stage = 'VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT'
    fdp_before = 'VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT'
    prior[fdp_stage] = set()
    subseq[fdp_stage] = set([fdp_before])
    subseq[fdp_stage].update(subseq[fdp_before])
    for stage in subseq[fdp_stage]:
        prior[stage].add(fdp_stage)

    touchups_done.add(fdp_stage)

    # The is formatted oddly in the snippet, so just add it here
    cr_stage ='VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT'
    prior[cr_stage] = set()
    subseq[cr_stage] = set()

    touchups_done.add(cr_stage)

    # Make sure top and bottom got added to every prior and subseq (respectively) except for HOST
    # and the every stage is prior and susequent to bottom and top (respectively) also except for HOST
    for stage, prior_stages in prior.items():
        if stage == host_stage: continue
        prior_stages.add(top_stage)
        subseq[top_stage].add(stage)
    for stage, subseq_stages in subseq.items():
        if stage == host_stage: continue
        subseq_stages.add(bot_stage)
        prior[bot_stage].add(stage)

    if experimental_ordering:
        stage_order = BubbleInsertStages(stage_order, prior, subseq)

    # Convert sets to ordered vectors
    prior_sets = prior
    prior = { key:StageOrderListFromSet(stage_order, value) for key, value in prior_sets.items() }
    subseq_sets = subseq
    subseq = { key:StageOrderListFromSet(stage_order, value) for key, value in subseq_sets.items() }

    if debug_stage_order_parse:
        print('STAGE_ORDER PRIOR STAGES')
        for stage, stage_set in prior.items():
            print(stage,"|".join(stage_set))

        print('STAGE_ORDER SUBSEQUENT STAGES')
        for stage, stage_set in subseq.items():
            print(stage,"|".join(stage_set))

    if touchups_done != touchup:
        print('STAGE_ORDER Stage order touchups failed')
        print('STAGE_ORDER touchups_done', touchups_done)
        print('STAGE_ORDER touchups found', touchup)
        exit(-1)

    return { 'stages': all_stages, 'stage_lists':stage_lists, 'prior': prior, 'subseq':subseq, 'touchups':touchup }

# Note: there should be a machine readable merged order, but there isn't... so we'll need some built-in consistency checks
# Pipeline stages in rough order, merge sorted.  When
# Legal stages are masked in, remaining stages are ordered
pipeline_order = '''
VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT
VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT
VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT
VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT
VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV
VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV
VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV
VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
VK_PIPELINE_STAGE_TRANSFER_BIT
VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_NV
VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT
VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
VK_PIPELINE_STAGE_HOST_BIT
'''

sync_enum_types = ('VkPipelineStageFlagBits', 'VkAccessFlagBits' )

def InBitOrder(tag, enum_elem):
    # The input may be unordered or sparse w.r.t. the mask field, sort and gap fill
    found = []
    for elem in enum_elem:
        bitpos = elem.get('bitpos')
        name = elem.get('name')
        if not bitpos:
            continue

        if name.endswith("MAX_ENUM"):
            break

        found.append({'name': name, 'bitpos': int(bitpos)})

    in_bit_order = []
    for entry in sorted(found, key=lambda record: record['bitpos']):
        if debug_in_bit_order:
            print ("adding ", {'name': entry['name'], 'mask': (1 << entry['bitpos'])})
        bitpos = entry['bitpos']
        in_bit_order.append({'name': entry['name'], 'mask': (1 << bitpos), 'bitpos': bitpos})

    return in_bit_order


# As of tag v1.2.135
snippet_pipeline_stages_supported = '''
[[synchronization-pipeline-stages-supported]]
.Supported pipeline stage flags
[cols="60%,40%",options="header"]
|====
|Pipeline stage flag                                          | Required queue capability flag
|ename:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT                      | None required
|ename:VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT                    | ename:VK_QUEUE_GRAPHICS_BIT or ename:VK_QUEUE_COMPUTE_BIT
|ename:VK_PIPELINE_STAGE_VERTEX_INPUT_BIT                     | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_VERTEX_SHADER_BIT                    | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT      | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT   | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT                  | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT                  | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT             | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT              | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT          | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT                   | ename:VK_QUEUE_COMPUTE_BIT
|ename:VK_PIPELINE_STAGE_TRANSFER_BIT                         | ename:VK_QUEUE_GRAPHICS_BIT, ename:VK_QUEUE_COMPUTE_BIT or ename:VK_QUEUE_TRANSFER_BIT
|ename:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT                   | None required
|ename:VK_PIPELINE_STAGE_HOST_BIT                             | None required
|ename:VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT                     | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT                     | None required
ifdef::VK_EXT_conditional_rendering[]
|ename:VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT        | ename:VK_QUEUE_GRAPHICS_BIT or ename:VK_QUEUE_COMPUTE_BIT
endif::VK_EXT_conditional_rendering[]
ifdef::VK_EXT_transform_feedback[]
|ename:VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT           | ename:VK_QUEUE_GRAPHICS_BIT
endif::VK_EXT_transform_feedback[]
ifdef::VK_NV_device_generated_commands[]
|ename:VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_NV            | ename:VK_QUEUE_GRAPHICS_BIT or ename:VK_QUEUE_COMPUTE_BIT
endif::VK_NV_device_generated_commands[]
ifdef::VK_NV_shading_rate_image[]
|ename:VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV            | ename:VK_QUEUE_GRAPHICS_BIT
endif::VK_NV_shading_rate_image[]
ifdef::VK_NV_mesh_shader[]
|ename:VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV                   | ename:VK_QUEUE_GRAPHICS_BIT
|ename:VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV                   | ename:VK_QUEUE_GRAPHICS_BIT
endif::VK_NV_mesh_shader[]
ifdef::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
|ename:VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR           | ename:VK_QUEUE_COMPUTE_BIT
|ename:VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | ename:VK_QUEUE_COMPUTE_BIT
endif::VK_NV_ray_tracing,VK_KHR_ray_tracing[]
ifdef::VK_EXT_fragment_density_map[]
|ename:VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT     | ename:VK_QUEUE_GRAPHICS_BIT
endif::VK_EXT_fragment_density_map[]
|====
'''

def BitSuffixed(name):
    alt_bit = ('_ANDROID', '_EXT', '_IMG', '_KHR', '_NV', '_NVX')
    bit_suf = name + '_BIT'
    for alt in alt_bit:
        if name.endswith(alt) :
            bit_suf = name.replace(alt, '_BIT' + alt)
            break
    return bit_suf;

 # Create the stage/access combination from the legal uses of access with stages
def CreateStageAccessCombinations(config, stage_order, stage_access_types):
    index = 0;
    enum_prefix = config['enum_prefix']
    stage_accesses = []
    for stage in stage_order:
        mini_stage = stage.lstrip()
        if mini_stage.startswith(enum_prefix):
            mini_stage = mini_stage.replace(enum_prefix,"")
        else:
            mini_stage = mini_stage.replace("VK_PIPELINE_STAGE_", "")
        mini_stage = mini_stage.replace("_BIT", "")

        # Because access_stage_table's elements order might be different sometimes.
        # It causes the generator creates different code. It needs to be sorted.
        stage_access_types[stage].sort();
        for access in stage_access_types[stage]:
            mini_access = access.replace("VK_ACCESS_", "").replace("_BIT", "")
            stage_access = "_".join((mini_stage,mini_access))
            stage_access = enum_prefix + stage_access
            stage_access_bit = BitSuffixed(stage_access)
            is_read = stage_access.endswith('_READ') or ( '_READ_' in stage_access)
            stage_accesses.append({
                    'stage_access': stage_access,
                    'stage_access_string' : '"' + stage_access + '"',
                    'stage_access_bit': stage_access_bit,
                    'index': index,
                    'stage': stage,
                    'access': access,
                    'is_read': 'true' if is_read else 'false' })
            index += 1

    # Add synthetic stage/access
    synth_stage_access = [ 'IMAGE_LAYOUT_TRANSITION', 'QUEUE_FAMILY_OWNERSHIP_TRANSFER']
    stage = 'VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM'  # The only invalid values available.
    access = 'VK_ACCESS_FLAG_BITS_MAX_ENUM'
    for synth in synth_stage_access :
        stage_access = enum_prefix + synth
        stage_access_bit = BitSuffixed(stage_access)
        is_read = False # both ILT and QFO are R/W operations
        stage_accesses.append({
                    'stage_access': stage_access,
                    'stage_access_string' : '"' + stage_access + '"',
                    'stage_access_bit': stage_access_bit,
                    'index': index,
                    'stage': stage,
                    'access': access,
                    'is_read': 'true' if is_read else 'false' })
        index += 1

    return stage_accesses

def StageAccessEnums(stage_accesses, config):
    type_prefix = config['type_prefix']
    var_prefix = config['var_prefix']
    sync_mask_name = config['sync_mask_name']
    indent = config['indent']
    # The stage/access combinations in ordinal order
    output = []
    ordinal_name = config['ordinal_name']
    output.append('// Unique number for each  stage/access combination')
    output.append('enum {} {{'.format(ordinal_name))
    output.extend([ '{}{} = {},'.format(indent, e['stage_access'], e['index'])  for e in stage_accesses])
    output.append('};')
    output.append('')

    # The stage/access combinations as bit mask
    bits_name = config['bits_name']
    output.append('// Unique bit for each  stage/access combination')
    output.append('enum {} : {} {{'.format(bits_name, sync_mask_name))
    output.extend([ '{}{} = {}(1) << {},'.format(indent, e['stage_access_bit'], sync_mask_name, e['stage_access'])  for e in stage_accesses])
    output.append('};')
    output.append('')

    map_name = var_prefix + 'StageAccessIndexByStageAccessBit'
    output.append('// Map of the StageAccessIndices from the StageAccess Bit')
    output.append('static std::map<{}, {}> {}  = {{'.format(sync_mask_name, ordinal_name, map_name))
    output.extend([ '{}{{ {}, {} }},'.format(indent, e['stage_access_bit'], e['stage_access'])  for e in stage_accesses])
    output.append('};')
    output.append('')

    # stage/access debug information based on ordinal enum
    sa_info_type = '{}StageAccessInfoType'.format(type_prefix)
    output.append('struct {} {{'.format(sa_info_type))
    output.append('{}const char *name;'.format(indent))
    output.append('{}VkPipelineStageFlagBits stage_mask;'.format(indent))
    output.append('{}VkAccessFlagBits access_mask;'.format(indent))
    output.append('{}{} stage_access_index;'.format(indent, ordinal_name))
    output.append('{}{} stage_access_bit;'.format(indent, bits_name))
    output.append('};\n')

    sa_info_var = '{}StageAccessInfoByStageAccessIndex'.format(config['var_prefix'])
    output.append('// Array of text names and component masks for each stage/access index')
    output.append('static std::array<{}, {}> {} = {{ {{'.format(sa_info_type, len(stage_accesses), sa_info_var))
    fields_format ='{tab}{tab}{}'
    fields = ['stage_access_string', 'stage', 'access', 'stage_access', 'stage_access_bit']
    for entry in stage_accesses:
        output.append(indent+'{')
        output.append (',\n'.join([fields_format.format(entry[field], tab=indent)  for field in fields]))
        output.append('\n' + indent +'},')
    output.append('} };')
    output.append('')

    return output

def UnpackField(map, field='name'):
    return [ e[field] for e in map ]

def CrossReferenceTable(table_name, table_desc, key_type, mapped_type, key_vec, mask_map, config):
    indent = config['indent']

    table = ['// ' + table_desc]
    table.append('static std::map<{}, {}> {} = {{'.format(key_type, mapped_type, config['var_prefix'] + table_name))

    for mask_key in key_vec:
        mask_vec = mask_map[mask_key]
        if len(mask_vec) == 0:
            continue

        if len(mask_vec) > 1:
            sep = ' |\n' + indent * 2
            table.append( '{tab}{{ {}, (\n{tab}{tab}{}\n{tab})}},'.format(mask_key, sep.join(mask_vec), tab=indent))
        else:
            table.append( '{}{{ {}, {}}},'.format(indent, mask_key, mask_vec[0]))
    if(table_name == 'StageAccessMaskByAccessBit'):
            table.append( '{}{{ {}, {}}},'.format(indent, 'VK_ACCESS_MEMORY_READ_BIT', 'syncStageAccessReadMask'))
            table.append( '{}{{ {}, {}}},'.format(indent, 'VK_ACCESS_MEMORY_WRITE_BIT', 'syncStageAccessWriteMask'))
    table.append('};')
    table.append('')

    return table

def DoubleCrossReferenceTable(table_name, table_desc, stage_keys, access_keys, stage_access_stage_access_map, config):
    indent = config['indent']
    ordinal_name = config['ordinal_name']

    table = ['// ' + table_desc]
    table.append('static std::map<{vk_stage_flags}, std::map<{vk_access_flags}, {ordinal_name}>> {var_prefix}{} = {{'.format(table_name, **config))
    sep = ' },\n' + indent * 2 + '{ '

    # Because stage_access_stage_access_map's elements order might be different sometimes.
    # It causes the generator creates different code. It needs to be sorted.
    for i in sorted(stage_access_stage_access_map):
        if len(stage_access_stage_access_map[i].keys()) == 0: continue
        accesses = [ '{}, {}'.format(access, index) for access, index in sorted(stage_access_stage_access_map[i].items()) ]
        entry_format = '{tab}{{ {key}, {{\n{tab}{tab}{{ {val} }}\n{tab}}} }},'
        table.append( entry_format.format(key=i, val=sep.join(accesses), tab=indent))
    table.append('};')
    table.append('')

    return table

def StageAccessCrossReference(sync_enum, stage_access_combinations, config):
    output = []
    # Setup the cross reference tables
    enum_in_bit_order = dict()
    for enum_type in sync_enum_types:
        enum_in_bit_order[enum_type] = InBitOrder(enum_type, sync_enum[enum_type])

    stages_in_bit_order =  enum_in_bit_order['VkPipelineStageFlagBits']
    access_in_bit_order =  enum_in_bit_order['VkAccessFlagBits']
    stage_access_mask_stage_map = { e['name']: [] for e in stages_in_bit_order }
    #stage_access_mask_stage_map[none_stage] = [] # Support for N/A
    stage_access_mask_access_map = { e['name']: [] for e in access_in_bit_order }
    stage_access_stage_access_map = {  e['name']: dict() for e in stages_in_bit_order }

    for stage_access_combo in stage_access_combinations:
        combo_bit = stage_access_combo['stage_access_bit']
        stage = stage_access_combo['stage']
        if stage == 'VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM': continue
        access = stage_access_combo['access']
        if access == 'VK_ACCESS_FLAG_BITS_MAX_ENUM' : continue
        stage_access_mask_stage_map[stage].append(combo_bit)
        stage_access_mask_access_map[access].append(combo_bit)
        stage_access_stage_access_map[stage][access] = stage_access_combo['stage_access']

    # sas: stage_access masks by stage used to build up SyncMaskTypes from VkPipelineStageFlagBits
    sas_desc = 'Bit order mask of stage_access bit for each stage'
    sas_name = 'StageAccessMaskByStageBit'
    output.extend(CrossReferenceTable(sas_name, sas_desc, 'VkPipelineStageFlags', config['sync_mask_name'],
                                      UnpackField(stages_in_bit_order), stage_access_mask_stage_map, config))

    # saa -- stage_access by access used to build up SyncMaskTypes from VkAccessFlagBits
    saa_name = 'StageAccessMaskByAccessBit'
    saa_desc = 'Bit order mask of stage_access bit for each access'
    output.extend(CrossReferenceTable(saa_name, saa_desc, 'VkAccessFlags',  config['sync_mask_name'],
                                      UnpackField(access_in_bit_order), stage_access_mask_access_map, config))

    #sasa -- stage access index by stage by access
    sasa_name = 'StageAccessIndexByStageAndAccess'
    sasa_desc = 'stage_access index for each stage and access'
    output.extend(DoubleCrossReferenceTable(sasa_name, sasa_desc,stages_in_bit_order, access_in_bit_order, stage_access_stage_access_map, config))

    return output

def GenerateStaticMask(name, desc, bits, config):
    sep = ' |\n' + config['indent']
    variable_format = 'static {sync_mask_name} {var_prefix}StageAccess{}Mask = ( //  {}'
    output = [variable_format.format(name, desc, **config)]
    output.append(config['indent'] + sep.join(bits))
    output.extend([');', ''])

    return output

def ReadWriteMasks(stage_access_combinations, config):
    read_list = [ e['stage_access_bit'] for e in stage_access_combinations if e['is_read']  == 'true']
    write_list = [ e['stage_access_bit'] for e in stage_access_combinations if e['is_read'] != 'true']
    output = ['// Constants defining the mask of all read and write stage_access states']
    output.extend(GenerateStaticMask('Read',  'Mask of all read StageAccess bits', read_list, config))
    output.extend(GenerateStaticMask('Write',  'Mask of all write StageAccess bits', write_list, config))

    return output

def AllCommandsByQueueCapability(stage_order, stage_queue_table, config):
    queue_cap_set = set()
    for stage, queue_flag_list in stage_queue_table.items():
        for queue_flag in queue_flag_list:
            queue_cap_set.add(queue_flag)

    queue_caps = sorted(queue_cap_set)
    queue_flag_map = { queue_flag:list() for queue_flag in queue_caps }

    for stage in stage_order:
        if stage == 'VK_PIPELINE_STAGE_ALL_COMMANDS_BIT' : continue # this is the one we're skipping
        if stage == 'VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT' : continue # we'll use the VK_QUEUE_GRAPHICS_BIT to expand this too

        queue_flag_list = stage_queue_table[stage]

        if len(queue_flag_list) == 0:
            queue_flag_list = queue_caps

        if debug_queue_caps: print(stage, queue_flag_list)
        for queue_flag in queue_flag_list:
             queue_flag_map[queue_flag].append(stage)

    name = "AllCommandStagesByQueueFlags"
    desc = 'Pipeline stages corresponding to VK_PIPELINE_STAGE_ALL_COMMANDS_BIT for each VkQueueFlagBits'
    return CrossReferenceTable(name, desc, 'VkQueueFlagBits', 'VkPipelineStageFlags', queue_caps, queue_flag_map, config)

def PipelineOrderMaskMap(stage_order, stage_order_map, config):
    output = list()
    prior_name = 'LogicallyEarlierStages'
    prior_desc = 'Masks of logically earlier stage flags for a given stage flag'
    output.extend(CrossReferenceTable(prior_name, prior_desc, config['vk_stage_bits'], config['vk_stage_flags'], stage_order,
                                     stage_order_map['prior'], config))

    subseq_name = 'LogicallyLaterStages'
    subseq_desc = 'Masks of logically later stage flags for a given stage flag'
    output.extend(CrossReferenceTable(subseq_name, subseq_desc, config['vk_stage_bits'], config['vk_stage_flags'], stage_order,
                                     stage_order_map['subseq'], config))
    return output

def ShaderStageToSyncStageAccess( shader_stage_key, sync_stage_key ):
    return '    {{VK_SHADER_STAGE_{}, {{\n        SYNC_{}_SHADER_READ, SYNC_{}_SHADER_WRITE, SYNC_{}_UNIFORM_READ}}}},'.format(shader_stage_key, sync_stage_key, sync_stage_key, sync_stage_key)

def ShaderStageAndSyncStageAccessMap():
    output = []
    output.append('struct SyncShaderStageAccess {')
    output.append('    SyncStageAccessIndex shader_read;')
    output.append('    SyncStageAccessIndex shader_write;')
    output.append('    SyncStageAccessIndex uniform_read;')
    output.append('};\n')
    output.append('static std::map<VkShaderStageFlagBits, SyncShaderStageAccess> syncStageAccessMaskByShaderStage = {')
    output.append(ShaderStageToSyncStageAccess('VERTEX_BIT', 'VERTEX_SHADER'))
    output.append(ShaderStageToSyncStageAccess('TESSELLATION_CONTROL_BIT', 'TESSELLATION_CONTROL_SHADER'))
    output.append(ShaderStageToSyncStageAccess('TESSELLATION_EVALUATION_BIT', 'TESSELLATION_EVALUATION_SHADER'))
    output.append(ShaderStageToSyncStageAccess('GEOMETRY_BIT', 'GEOMETRY_SHADER'))
    output.append(ShaderStageToSyncStageAccess('FRAGMENT_BIT', 'FRAGMENT_SHADER'))
    output.append(ShaderStageToSyncStageAccess('COMPUTE_BIT', 'COMPUTE_SHADER'))
    output.append(ShaderStageToSyncStageAccess('RAYGEN_BIT_KHR', 'RAY_TRACING_SHADER_KHR'))
    output.append(ShaderStageToSyncStageAccess('ANY_HIT_BIT_KHR', 'RAY_TRACING_SHADER_KHR'))
    output.append(ShaderStageToSyncStageAccess('CLOSEST_HIT_BIT_KHR', 'RAY_TRACING_SHADER_KHR'))
    output.append(ShaderStageToSyncStageAccess('MISS_BIT_KHR', 'RAY_TRACING_SHADER_KHR'))
    output.append(ShaderStageToSyncStageAccess('INTERSECTION_BIT_KHR', 'RAY_TRACING_SHADER_KHR'))
    output.append(ShaderStageToSyncStageAccess('CALLABLE_BIT_KHR', 'RAY_TRACING_SHADER_KHR'))
    output.append(ShaderStageToSyncStageAccess('TASK_BIT_NV', 'TASK_SHADER_NV'))
    output.append(ShaderStageToSyncStageAccess('MESH_BIT_NV', 'MESH_SHADER_NV'))
    output.append('};\n')
    return output

def GenSyncTypeHelper(gen) :
    config = {
        'var_prefix': 'sync',
        'type_prefix': 'Sync',
        'enum_prefix': 'SYNC_',
        'indent': '    ',
        'sync_mask_base_type': 'uint64_t',
        'vk_stage_flags': 'VkPipelineStageFlags',
        'vk_stage_bits': 'VkPipelineStageFlagBits',
        'vk_access_flags': 'VkAccessFlags',
        'vk_access_bits': 'VkAccessFlagBits'}
    config['sync_mask_name'] = '{}StageAccessFlags'.format(config['type_prefix'])
    config['ordinal_name'] = '{}StageAccessIndex'.format(config['type_prefix'])
    config['bits_name'] = '{}StageAccessFlagBits'.format(config['type_prefix'])

    lines = ['#pragma once', '', '#include <array>', '#include <map>', '#include <stdint.h>', '#include <vulkan/vulkan.h>', '']
    lines.extend(['// clang-format off', ''])
    lines.extend(("using {} = {};".format(config['sync_mask_name'], config['sync_mask_base_type']), ''))

    stage_order = pipeline_order.split()
    access_types = {stage:list() for stage in stage_order}
    if debug_top_level:
        lines.append('// Access types \n//    ' + '\n//    '.join(access_types) +  '\n' * 2)

    stage_order_map = ParsePipelineStageOrder(stage_order, snippet_pipeline_stages_order, config)
    access_stage_table = ParseAccessType(snippet_access_types_supported)
    stage_queue_cap_table = ParseAccessType(snippet_pipeline_stages_supported)
    stage_access_table = CreateStageAccessTable(stage_order, access_stage_table)
    stage_access_combinations = CreateStageAccessCombinations(config, stage_order, stage_access_table)
    lines.extend(StageAccessEnums(stage_access_combinations, config))

    lines.extend(ReadWriteMasks(stage_access_combinations, config))

    lines.extend(StageAccessCrossReference(gen.sync_enum, stage_access_combinations, config))
    lines.extend(AllCommandsByQueueCapability(stage_order, stage_queue_cap_table, config))
    lines.extend(PipelineOrderMaskMap(stage_order, stage_order_map, config))
    lines.extend(ShaderStageAndSyncStageAccessMap())
    return  '\n'.join(lines)
