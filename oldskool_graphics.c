/* Copyright 2024 Richard N Van Natta
 *
 * This file is part of the Vanity Plot library.
 *
 * The Vanity Plot library is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 2 of the
 * License, or (at your option) any later version.
 * 
 * The Vanity Plot library is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with the Vanity Scheme Compiler.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include "volk.h"
#include "oldskool_graphics.h"
#include "vanity_graphics_private.h"
#include "vector_math.h"

#include "vert.h"
#include "frag.h"

VkShaderModule VG_CreateShaderModule(VGWindow * wind, char * code, size_t size) {
  if(size % 4)
    return VK_NULL_HANDLE;
  VkShaderModuleCreateInfo createInfo = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = size,
    .pCode = (uint32_t*)code,
  };
  VkShaderModule module = VK_NULL_HANDLE;
  if(vkCreateShaderModule(wind->device, &createInfo, NULL, &module) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  return module;
}

static VGPipeline VG_CreatePipeline(VGWindow * wind) {
  VGPipeline ret = { VK_NULL_HANDLE, VK_NULL_HANDLE };
  VkShaderModule vert = VK_NULL_HANDLE;
  VkShaderModule frag = VK_NULL_HANDLE;

  VkPushConstantRange pushLayout = {
    .offset = 0,
    .size = sizeof(mat4),
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
  };

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges = &pushLayout,
  };
  VkPipelineLayout pipelineLayout;
  if(vkCreatePipelineLayout(wind->device, &pipelineLayoutInfo, NULL, &pipelineLayout) != VK_SUCCESS) {
    goto end;
  }
  ret.layout = pipelineLayout;

  vert = VG_CreateShaderModule(wind, _binary_vert_spv_start, _binary_vert_spv_end - _binary_vert_spv_start);
  frag = VG_CreateShaderModule(wind, _binary_frag_spv_start, _binary_frag_spv_end - _binary_frag_spv_start);
  if(vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
    fprintf(stderr, "failed to create shader modules\n");
    goto end;
  }
  
  VkPipelineShaderStageCreateInfo stages[] = {
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert,
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag,
      .pName = "main",
    }
  };

  VkDynamicState dynamicStates[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo dynamicState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = sizeof dynamicStates / sizeof *dynamicStates,
    .pDynamicStates = dynamicStates,
  };

  // BEGIN YUCK //
  VkVertexInputBindingDescription vertexBinding = {
    .binding = 0,
    .stride = sizeof(vec4[2]),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };

  VkVertexInputAttributeDescription vertexAttribs[] = {
    {
      .binding = 0,
      .location = 0,
      .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .offset = 0,
    },
    {
      .binding = 0,
      .location = 1,
      .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .offset = sizeof(vec4),
    },
  };

  VkPipelineVertexInputStateCreateInfo vertexInput = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &vertexBinding,
    .vertexAttributeDescriptionCount = sizeof vertexAttribs / sizeof *vertexAttribs,
    .pVertexAttributeDescriptions = vertexAttribs,
  };
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };
  // END YUCK //

  VkPipelineViewportStateCreateInfo viewportState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .scissorCount = 1,
  };

  VkPipelineRasterizationStateCreateInfo rasterizer = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .lineWidth = 1,
    .cullMode = VK_CULL_MODE_NONE,
    .frontFace = VK_FRONT_FACE_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
  };

  VkPipelineMultisampleStateCreateInfo multisampling = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .sampleShadingEnable = VK_FALSE,
    .rasterizationSamples = wind->msaa_samples,
  };

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    .blendEnable = VK_FALSE,
  };

  VkPipelineColorBlendStateCreateInfo colorBlending = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachment,
  };

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = 2,
    .pStages = stages,
    .pVertexInputState = &vertexInput,
    .pInputAssemblyState = &inputAssembly,
    .pViewportState = &viewportState,
    .pRasterizationState = &rasterizer,
    .pMultisampleState = &multisampling,
    .pDepthStencilState = NULL,
    .pColorBlendState = &colorBlending,
    .pDynamicState = &dynamicState,

    .layout = pipelineLayout,
    .renderPass = wind->renderpass,
    .subpass = 0,
  };

  VkPipeline pipeline;
  if(vkCreateGraphicsPipelines(wind->device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, NULL, &pipeline) != VK_SUCCESS) {
    fprintf(stderr, "failed to create pipeline!\n");
    goto end;
  }
  ret.pipeline = pipeline;

end:
  vkDestroyShaderModule(wind->device, vert, NULL);
  vkDestroyShaderModule(wind->device, frag, NULL);
  if(ret.pipeline == VK_NULL_HANDLE && ret.layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(wind->device, ret.layout, NULL);
    ret.layout = VK_NULL_HANDLE;
  }
  return ret;
}

typedef struct OldskoolVert {
  vec4 pos;
  vec4 color;
} OldskoolVert;

enum { OS_VERTEX, OS_COLOR, OS_NUM_ARRAYS };

typedef struct OldskoolArray {
  int vector_width;
  int type;
  int count;
  int stride;
  void * data;
  int offset;
} OldskoolArray;

enum { OS_DRAW_ARRAYS, OS_DRAW_ELEMENTS, OS_PUSHMAT, OS_CLEARCOLOR };

typedef struct OldskoolCmd {
  int type;
  union {
    struct {
      int prim;
      int start;
      int count;
    } draw;
    mat4 matrix;
    vec4 clearcolor;
  };
} OldskoolCmd;

typedef struct OldskoolContext {
  int state;
  int start;
  vec4 active_color;

  int numverts;
  size_t vertsize;
  OldskoolVert * verts;

  int numinds;
  size_t indsize;
  unsigned * inds;

  bool arrays_changed;
  OldskoolArray arrays[2];
  int uploaded_array_start;
  int uploaded_array_count;

  int numcmds;
  size_t cmdsize;
  OldskoolCmd * cmds;

  mat4 lastmat;

  int nummats;
  size_t matsize;
  mat4 * matstack;

  VGWindow * wind;
  VGBuffer vertbuf[2];
  void * vertmap[2];

  VGBuffer indbuf[2];
  void * indmap[2];

  VGPipeline triangle_pipe;

} OldskoolContext;

static size_t rounduppow2(size_t x) {
  x--;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x |= x >> 32;
  x++;
  return x;
}

static int osAllocGpu(OldskoolContext * k, bool parity) {
  size_t vertsize = rounduppow2(sizeof(OldskoolVert[k->numverts]));

  if(vertsize > k->vertbuf[parity].size) {
    VG_DestroyBuffer(k->wind, k->vertbuf[parity]);
    k->vertbuf[parity] = VG_CreateBufferImpl(k->wind, sizeof(OldskoolVert[k->numverts]), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(k->vertbuf[parity].buf == VK_NULL_HANDLE)
      return 1;
    vkMapMemory(k->wind->device, k->vertbuf[parity].mem, 0, k->vertbuf[parity].size, 0, &k->vertmap[parity]);
  }

  size_t indsize = rounduppow2(sizeof(unsigned[k->numinds]));

  if(indsize > k->indbuf[parity].size) {
    VG_DestroyBuffer(k->wind, k->indbuf[parity]);
    k->indbuf[parity] = VG_CreateBufferImpl(k->wind, sizeof(unsigned[k->numinds]), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(k->indbuf[parity].buf == VK_NULL_HANDLE)
      return 1;
    vkMapMemory(k->wind->device, k->indbuf[parity].mem, 0, k->indbuf[parity].size, 0, &k->indmap[parity]);
  }

  return 0;
}

OldskoolContext * osCreate(VGWindow * wind) {
  OldskoolContext * ret = malloc(sizeof(OldskoolContext));
  *ret = (OldskoolContext) {
    .state = OS_IDLE,
    .start = 0,
    .active_color = make_vec4(0, 0, 0, 1),

    .numverts = 0,
    .vertsize = 0,
    .verts = NULL,

    .numinds = 0,
    .indsize = 0,
    .inds = NULL,
    
    .numcmds = 0,
    .cmdsize = 0,
    .cmds = NULL,

    .arrays_changed = true,

    .nummats = 1,
    .matsize = 1,
    .matstack = malloc(sizeof(mat4)),

    .wind = wind,

    .vertbuf = { [0 ... 1] = { VK_NULL_HANDLE, VK_NULL_HANDLE, 0 } },
    .indbuf = { [0 ... 1] = { VK_NULL_HANDLE, VK_NULL_HANDLE, 0 } },
  };
  *ret->matstack = mat4_id();

  memset(&ret->lastmat, -1, sizeof ret->lastmat);

  ret->triangle_pipe = VG_CreatePipeline(wind);

  return ret;
}
void osDestroy(OldskoolContext * k) {
  VG_WaitIdle(k->wind);
  VG_DestroyBuffer(k->wind, k->vertbuf[0]);
  VG_DestroyBuffer(k->wind, k->vertbuf[1]);
  VG_DestroyBuffer(k->wind, k->indbuf[0]);
  VG_DestroyBuffer(k->wind, k->indbuf[1]);
  VG_DestroyPipeline(k->wind, k->triangle_pipe);

  free(k->verts);
  free(k->cmds);

  free(k);
}

void osSubmit(OldskoolContext * k, VkCommandBuffer cmdbuf, bool parity) {
  assert(!osAllocGpu(k, parity));
  memcpy(k->vertmap[parity], k->verts, sizeof(OldskoolVert[k->numverts]));
  memcpy(k->indmap[parity], k->inds, sizeof(unsigned[k->numinds]));

  VGPipeline pipe = k->triangle_pipe;
  mat4 id = mat4_id();
  vkCmdPushConstants(cmdbuf, pipe.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &id);

  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline);

  VkViewport viewport = {
    .x = 0,
    .y = 0,
    .width = k->wind->swap_extent.width,
    .height = k->wind->swap_extent.height,
    .minDepth = 0,
    .maxDepth = 1,
  };
  VkRect2D scissor = {
    .offset = { 0, 0 },
    .extent = k->wind->swap_extent,
  };
  vkCmdSetViewport(cmdbuf, 0, 1, &viewport);
  vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

  VkDeviceSize offset = 0;
  if(k->numverts)
    vkCmdBindVertexBuffers(cmdbuf, 0, 1, &k->vertbuf[parity].buf, &offset);
  if(k->numinds)
    vkCmdBindIndexBuffer(cmdbuf, k->indbuf[parity].buf, 0, VK_INDEX_TYPE_UINT32);
  for(int i = 0; i < k->numcmds; i++) {
    OldskoolCmd cmd = k->cmds[i];
    switch(cmd.type) {
      case OS_DRAW_ARRAYS:
      {
        vkCmdDraw(cmdbuf, cmd.draw.count, 1, cmd.draw.start, 0);
        break;
      }
      case OS_DRAW_ELEMENTS:
      {
        // FIXME instead of pre shifting indices, pass the shift here
        vkCmdDrawIndexed(cmdbuf, cmd.draw.count, 1, cmd.draw.start, 0, 0);
        break;
      }
      case OS_PUSHMAT:
      {
        vkCmdPushConstants(cmdbuf, pipe.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &cmd.matrix);
        break;
      }
      case OS_CLEARCOLOR:
      {
        float color[4];
        memcpy(color, &cmd.clearcolor, sizeof color);

        VkClearAttachment clear_attach = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .colorAttachment = 0,
          .clearValue = {{{ color[0], color[1], color[2], color[3] }}},
        };

        VkClearRect clear_region = {
          .rect = { { 0, 0, }, k->wind->swap_extent },
          .baseArrayLayer = 0,
          .layerCount = 1,
        };

        vkCmdClearAttachments(cmdbuf, 1, &clear_attach, 1, &clear_region);
        break;
      }
      default:
        assert(0);
    }
  }
}

static void * exalloc(void * ptr, size_t size, size_t *oldsize) {
  if(size == 0) {
    return ptr;
  }
  if(size <= *oldsize)
    return ptr;

  size--;
  size |= size >> 1;
  size |= size >> 2;
  size |= size >> 4;
  size |= size >> 8;
  size |= size >> 16;
  size |= size >> 32;
  size++;

  *oldsize = size;

  return realloc(ptr, size);
}

void osReset(OldskoolContext * k) {
  k->state = OS_IDLE;
  k->start = 0;
  k->numverts = 0;
  k->numinds = 0;
  k->numcmds = 0;
  k->active_color = make_vec4(0, 0, 0, 1);

  k->arrays_changed = true;
  k->arrays[0].data = NULL;
  k->arrays[1].data = NULL;

  k->nummats = 1;
  k->matstack = exalloc(k->matstack, sizeof(mat4[k->nummats]), &k->matsize);
  k->matstack[k->nummats-1] = mat4_id();
  
  memset(&k->lastmat, -1, sizeof k->lastmat);
}

void osClearColor(OldskoolContext * k, vec4 color) {
  OldskoolCmd cmd = {
    .type = OS_CLEARCOLOR,
    .clearcolor = color,
  };
  k->numcmds++;
  k->cmds = exalloc(k->cmds, sizeof(OldskoolCmd[k->numcmds]), &k->cmdsize);
  k->cmds[k->numcmds-1] = cmd;
}

void osBegin(OldskoolContext * k, int primtype) {
  assert(primtype == OS_TRIANGLES);
  assert(k->state == OS_IDLE);

  k->start = k->numverts;
  k->active_color = make_vec4(0, 0, 0, 1);
  k->state = primtype;
}

static void osUploadMatrix(OldskoolContext * k) {
  mat4 m = k->matstack[k->nummats-1];
  if(memcmp(&m, &k->lastmat, sizeof m)) {
    k->lastmat = m;

    OldskoolCmd cmd = {
      .type = OS_PUSHMAT,
      .matrix = m,
    };
    k->numcmds++;
    k->cmds = exalloc(k->cmds, sizeof(OldskoolCmd[k->numcmds]), &k->cmdsize);
    k->cmds[k->numcmds-1] = cmd;
  }
}

void osEnd(OldskoolContext * k) {
  assert(k->state != OS_IDLE);

  osUploadMatrix(k);

  OldskoolCmd cmd = {
    .type = OS_DRAW_ARRAYS,
    .draw = {
      .prim = k->state,
      .start = k->start,
      .count = k->numverts - k->start,
    },
  };
  k->numcmds++;
  k->cmds = exalloc(k->cmds, sizeof(OldskoolCmd[k->numcmds]), &k->cmdsize);
  k->cmds[k->numcmds-1] = cmd;

  k->state = OS_IDLE;
}

void osVertex4(OldskoolContext * k, vec4 v) {
  assert(k->state != OS_IDLE);

  k->numverts++;
  k->verts = exalloc(k->verts, sizeof(OldskoolVert[k->numverts]), &k->vertsize);
  k->verts[k->numverts-1] = (OldskoolVert) {
    .pos = v,
    .color = k->active_color,
  };
}

void osVertex3(OldskoolContext * k, vec3 v) {
  osVertex4(k, make_vec4(v, 1));
}

void osVertex2(OldskoolContext * k, vec2 v) {
  osVertex4(k, make_vec4(v, 0, 1));
}

void osColor4(OldskoolContext * k, vec4 v) {
  k->active_color = v;
}

void osColor3(OldskoolContext * k, vec3 v) {
  osColor4(k, make_vec4(v, 1));
}

void osLoadMatrix(OldskoolContext * k, mat4 m) {
  k->nummats = 1;
  k->matstack = exalloc(k->matstack, sizeof(mat4[k->nummats]), &k->matsize);
  k->matstack[k->nummats-1] = m;
}

void osPushMatrix(OldskoolContext * k, mat4 m) {
  k->nummats++;
  k->matstack = exalloc(k->matstack, sizeof(mat4[k->nummats]), &k->matsize);
  k->matstack[k->nummats-1] = mat4_mul(k->matstack[k->nummats-2], m);
}
void osPopMatrix(OldskoolContext * k) {
  assert(k->nummats > 1);
  k->nummats--;
}

int osAttribPointer(OldskoolContext * k, int array, int vector_width, int type, int count, int stride, void * data, int offset) {
  assert(array < OS_NUM_ARRAYS);

  assert(1 <= vector_width && vector_width <= 4);

  assert(type == OS_FLOAT);
  if(stride == 0)
    stride = vector_width * sizeof(float);

  k->arrays[array] = (OldskoolArray) {
    .vector_width = vector_width,
    .type = type,
    .count = count,
    .stride = stride,
    .data = data,
    .offset = offset,
  };
  k->arrays_changed = true;
  return 0;
}

int osVertexPointer(OldskoolContext * k, int vector_width, int type, int count, int stride, void * data, int offset) {
  assert(2 <= vector_width);
  osAttribPointer(k, OS_VERTEX, vector_width, type, count, stride, data, offset);
  return 0;
}
int osColorPointer(OldskoolContext * k, int vector_width, int type, int count, int stride, void * data, int offset) {
  assert(3 <= vector_width);
  osAttribPointer(k, OS_COLOR, vector_width, type, count, stride, data, offset);
  return 0;
}

static vec4 osReadArray(OldskoolArray * arr, int i) {
  /*
  int vector_width;
  int type;
  int count;
  int stride;
  void * data;
  int offset;
  */
  assert(arr->type == OS_FLOAT);

  float * f = (arr->data+i*arr->stride+arr->offset);
  float mem[4] = { 0, 0, 0, 1 };
  memcpy(mem, f, sizeof(float[arr->vector_width]));
  return make_vec4(mem);
}

static void osUploadArrays(OldskoolContext * k) {
  if(!k->arrays_changed)
    return;
  k->arrays_changed = false;

  assert(k->arrays[OS_VERTEX].data && k->arrays[OS_VERTEX].count);
  if(k->arrays[OS_COLOR].data)
    assert(k->arrays[OS_VERTEX].count == k->arrays[OS_COLOR].count);

  int start = k->numverts;
  int count = k->arrays[OS_VERTEX].count;
  k->numverts += count;
  k->verts = exalloc(k->verts, sizeof(OldskoolVert[k->numverts]), &k->vertsize);

  for(int i = 0; i < count; i++) {
    k->verts[start + i].pos = osReadArray(&k->arrays[OS_VERTEX], i);
    if(k->arrays[OS_COLOR].data)
      k->verts[start + i].color = osReadArray(&k->arrays[OS_COLOR], i);
    else
      k->verts[start + i].color = make_vec4(1);
  }
  k->uploaded_array_start = start;
  k->uploaded_array_count = count;
}

int osDrawArrays(OldskoolContext * k, int mode, int start, int count) {
  assert(mode == OS_TRIANGLES);
  assert(k->state == OS_IDLE);

  osUploadArrays(k);
  osUploadMatrix(k);

  OldskoolCmd cmd = {
    .type = OS_DRAW_ARRAYS,
    .draw = {
      .prim = mode,
      .start = k->uploaded_array_start + start,
      .count = count,
    },
  };
  k->numcmds++;
  k->cmds = exalloc(k->cmds, sizeof(OldskoolCmd[k->numcmds]), &k->cmdsize);
  k->cmds[k->numcmds-1] = cmd;
  return 0;
}

int osDrawElements(OldskoolContext * k, int mode, int count, int type, void * buf) {
  assert(mode == OS_TRIANGLES);
  assert(k->state == OS_IDLE);
  assert(type == OS_UNSIGNED_INT);

  osUploadArrays(k);
  osUploadMatrix(k);

  int start = k->numinds;
  k->numinds += count;
  k->inds = exalloc(k->inds, sizeof(unsigned[k->numinds]), &k->indsize);

  unsigned * indices = buf;

  unsigned vertex_base = k->uploaded_array_start;
  unsigned vertex_count = k->uploaded_array_count;
  for(int i = 0; i < count; i++) {
    assert(indices[i] < vertex_count);
    k->inds[start + i] = indices[i] + vertex_base;
  }

  OldskoolCmd cmd = {
    .type = OS_DRAW_ELEMENTS,
    .draw = {
      .prim = mode,
      .start = start,
      .count = count,
    },
  };
  k->numcmds++;
  k->cmds = exalloc(k->cmds, sizeof(OldskoolCmd[k->numcmds]), &k->cmdsize);
  k->cmds[k->numcmds-1] = cmd;
  return 0;
}
