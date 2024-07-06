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
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#ifdef __linux__
#define VK_USE_PLATFORM_XLIB_KHR
#elif defined(_WIN64)
#define VK_USE_PLATFORM_WIN32_KHR
#else
static_assert(0);
#endif

#include "volk.h"
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

typedef struct myvert {
  float pos[2];
  float color[3];
} myvert;

typedef struct VGPipeline {
  VkPipelineLayout layout;
  VkPipeline pipeline;
} VGPipeline;

VGPipeline VG_CreatePipeline(VGWindow * wind) {
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
    //.stride = sizeof(myvert),
    .stride = sizeof(vec4[2]),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };

  VkVertexInputAttributeDescription vertexAttribs[] = {
    {
      .binding = 0,
      .location = 0,
      //.format = VK_FORMAT_R32G32_SFLOAT,
      //.offset = offsetof(myvert, pos),
      .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .offset = 0,
    },
    {
      .binding = 0,
      .location = 1,
      .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .offset = sizeof(vec4),
      //.format = VK_FORMAT_R32G32B32_SFLOAT,
      //.offset = offsetof(myvert, color),
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
    .cullMode = VK_CULL_MODE_FRONT_BIT,
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
void VG_DestroyPipeline(VGWindow * wind, VGPipeline pipe) {
  vkDestroyPipeline(wind->device, pipe.pipeline, NULL);
  vkDestroyPipelineLayout(wind->device, pipe.layout, NULL);
}

void VG_CopyBufferImpl(VGWindow * wind, VkBuffer dst, VkBuffer src, VkDeviceSize size) {
  // Holy smokes this is questionable
  VkCommandBufferAllocateInfo allocCmd = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandPool = wind->commandpool,
    .commandBufferCount = 1,
  };
  VkCommandBuffer cmdbuf;
  vkAllocateCommandBuffers(wind->device, &allocCmd, &cmdbuf);

  VkCommandBufferBeginInfo beginfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  vkBeginCommandBuffer(cmdbuf, &beginfo);
  VkBufferCopy copyRegion = {
    .srcOffset = 0,
    .dstOffset = 0,
    .size = size,
  };
  vkCmdCopyBuffer(cmdbuf, src, dst, 1, &copyRegion);
  vkEndCommandBuffer(cmdbuf);

  VkSubmitInfo submitInfo = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .commandBufferCount = 1,
    .pCommandBuffers = &cmdbuf,
  };
  vkQueueSubmit(wind->graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
  // im gonna be sick
  vkQueueWaitIdle(wind->graphics_queue);
  vkFreeCommandBuffers(wind->device, wind->commandpool, 1, &cmdbuf);
}
void VG_DestroyBuffer(VGWindow * wind, VGBuffer buf) {
  vkDestroyBuffer(wind->device, buf.buf, NULL);
  vkFreeMemory(wind->device, buf.mem, NULL);
}

VGBuffer VG_CreateIndexBuffer(VGWindow * wind) {
  const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };

  // oh my god ew
  VGBuffer staging = VG_CreateBufferImpl(wind, sizeof indices, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  VGBuffer ret = VG_CreateBufferImpl(wind, sizeof indices, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if(staging.buf == VK_NULL_HANDLE || ret.buf == VK_NULL_HANDLE)
    return ret;

  void * data;
  vkMapMemory(wind->device, staging.mem, 0, staging.size, 0, &data);
  memcpy(data, indices, sizeof indices);
  vkUnmapMemory(wind->device, staging.mem);

  VG_CopyBufferImpl(wind, ret.buf, staging.buf, sizeof indices);

  VG_DestroyBuffer(wind, staging);

  return ret;
}

typedef struct Globals {
  mat4 model;
  mat4 view;
  mat4 proj;
} Globals;

void WriteGlobals(char * buf, VkExtent2D extent) {

  static float del = 0;
  mat4 model = mat4_rotate(quat_rotateAxis(make_vec3(0, 0, 1), del));
  del += 1.0 / 60;

  Globals g = {
    .model = model,
    .view = mat4_lookat(make_vec3(2,0,2), make_vec3(0), make_vec3(0, 0, 1)),
    .proj = mat4_perspective(extent.width / (float) extent.height, M_PI / 4, 0.1f, 10),
  };
  memcpy(buf, &g, sizeof g);
};

VGBuffer VG_CreateVertexBuffer(VGWindow * wind) {
  const myvert verts[] = {
    { {-0.5, -0.5}, {1, 0, 0} },
    { {+0.5, -0.5}, {0, 1, 0} },
    { {+0.5, +0.5}, {0, 0, 1} },
    { {-0.5, +0.5}, {1, 1, 1} },
  };

  // oh my god ew
  VGBuffer staging = VG_CreateBufferImpl(wind, sizeof verts, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  VGBuffer ret = VG_CreateBufferImpl(wind, sizeof verts, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if(staging.buf == VK_NULL_HANDLE || ret.buf == VK_NULL_HANDLE)
    return ret;

  void * data;
  vkMapMemory(wind->device, staging.mem, 0, staging.size, 0, &data);
  memcpy(data, verts, sizeof verts);
  vkUnmapMemory(wind->device, staging.mem);

  VG_CopyBufferImpl(wind, ret.buf, staging.buf, sizeof verts);

  VG_DestroyBuffer(wind, staging);

  return ret;
}

typedef struct OldskoolVert {
  vec4 pos;
  vec4 color;
} OldskoolVert;


enum { OS_DRAW, OS_PUSHMAT };
enum { OS_IDLE, OS_POINTS, OS_LINES, OS_TRIANGLES };

typedef struct OldskoolCmd {
  int type;
  union {
    struct {
      int prim;
      int firstvert;
      int numindices;
    } draw;
    mat4 matrix;
  };
} OldskoolCmd;

typedef struct OldskoolContext {
  int state;
  int firstvert;
  vec4 active_color;

  int numverts;
  OldskoolVert * verts;

  int numcmds;
  OldskoolCmd * cmds;

  mat4 lastmat;

  int nummats;
  mat4 * matstack;

  VGWindow * wind;
  VGBuffer vertbuf[2];
  void * vertmap[2];

  VGPipeline triangle_pipe;

} OldskoolContext;

OldskoolContext * osCreate(VGWindow * wind);
void osDestroy(OldskoolContext * k);

void osReset(OldskoolContext * k);

void osBegin(OldskoolContext * k, int primtype);
void osEnd(OldskoolContext * k);

void osSubmit(OldskoolContext * k, VkCommandBuffer cmdbuf, bool parity);

void osVertex4(OldskoolContext * k, vec4 v);
void osVertex3(OldskoolContext * k, vec3 v);
void osVertex2(OldskoolContext * k, vec2 v);
void osColor4(OldskoolContext * k, vec4 v);
void osColor3(OldskoolContext * k, vec4 v);

void osLoadMatrix(OldskoolContext * k, mat4 m);
void osPushMatrix(OldskoolContext * k, mat4 m);
void osPopMatrix(OldskoolContext * k);

static int osAllocGpu(OldskoolContext * k, bool parity) {
  size_t size = sizeof(OldskoolVert[k->numverts]);
  size--;
  size |= size >> 1;
  size |= size >> 2;
  size |= size >> 4;
  size |= size >> 8;
  size |= size >> 16;
  size |= size >> 32;
  size++;

  if(size > k->vertbuf[parity].size) {
    VG_DestroyBuffer(k->wind, k->vertbuf[parity]);
    k->vertbuf[parity] = VG_CreateBufferImpl(k->wind, sizeof(OldskoolVert[32]), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(k->vertbuf[parity].buf == VK_NULL_HANDLE)
      return 1;
    vkMapMemory(k->wind->device, k->vertbuf[parity].mem, 0, k->vertbuf[parity].size, 0, &k->vertmap[parity]);
  }
  return 0;
}

OldskoolContext * osCreate(VGWindow * wind) {
  OldskoolContext * ret = malloc(sizeof(OldskoolContext));
  *ret = (OldskoolContext) {
    .state = OS_IDLE,
    .firstvert = 0,
    .active_color = make_vec4(0, 0, 0, 1),

    .numverts = 0,
    .verts = NULL,
    
    .numcmds = 0,
    .cmds = NULL,

    .nummats = 1,
    .matstack = malloc(sizeof(mat4)),

    .wind = wind,

    .vertbuf = { [0 ... 1] = { VK_NULL_HANDLE, VK_NULL_HANDLE, 0 } },
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
  VG_DestroyPipeline(k->wind, k->triangle_pipe);

  free(k->verts);
  free(k->cmds);

  free(k);
}

void osSubmit(OldskoolContext * k, VkCommandBuffer cmdbuf, bool parity) {
  size_t upload_size = sizeof(OldskoolVert[k->numverts]);
  assert(!osAllocGpu(k, parity));
  memcpy(k->vertmap[parity], k->verts, upload_size);

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
  vkCmdBindVertexBuffers(cmdbuf, 0, 1, &k->vertbuf[parity].buf, &offset);
  for(int i = 0; i < k->numcmds; i++) {
    OldskoolCmd cmd = k->cmds[i];
    switch(cmd.type) {
      case OS_DRAW:
      {
        vkCmdDraw(cmdbuf, cmd.draw.numindices, 1, cmd.draw.firstvert, 0);
        break;
      }
      case OS_PUSHMAT:
      {
        vkCmdPushConstants(cmdbuf, pipe.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &cmd.matrix);
        break;
      }
      default:
        assert(0);
    }
  }
}

static void * exalloc(void * ptr, size_t size) {
  if(size == 0) {
    free(ptr);
    return NULL;
  }
  size--;
  size |= size >> 1;
  size |= size >> 2;
  size |= size >> 4;
  size |= size >> 8;
  size |= size >> 16;
  size |= size >> 32;
  size++;

  return realloc(ptr, size);
}

void osReset(OldskoolContext * k) {
  k->state = OS_IDLE;
  k->firstvert = 0;
  k->numverts = 0;
  k->numcmds = 0;
  k->active_color = make_vec4(0, 0, 0, 1);

  k->nummats = 1;
  k->matstack = exalloc(k->matstack, sizeof(mat4[k->nummats]));
  k->matstack[k->nummats-1] = mat4_id();
}

void osBegin(OldskoolContext * k, int primtype) {
  assert(primtype == OS_TRIANGLES);
  assert(k->state == OS_IDLE);

  k->firstvert = k->numverts;
  k->active_color = make_vec4(0, 0, 0, 1);
  k->state = primtype;
}

void osEnd(OldskoolContext * k) {
  assert(k->state != OS_IDLE);

  mat4 m = k->matstack[k->nummats-1];
  if(memcmp(&m, &k->lastmat, sizeof m)) {
    k->lastmat = m;

    OldskoolCmd cmd = {
      .type = OS_PUSHMAT,
      .matrix = m,
    };
    k->numcmds++;
    k->cmds = exalloc(k->cmds, sizeof(OldskoolCmd[k->numcmds]));
    k->cmds[k->numcmds-1] = cmd;
  }

  OldskoolCmd cmd = {
    .type = OS_DRAW,
    .draw = {
      .prim = k->state,
      .firstvert = k->firstvert,
      .numindices = k->numverts - k->firstvert,
    },
  };
  k->numcmds++;
  k->cmds = exalloc(k->cmds, sizeof(OldskoolCmd[k->numcmds]));
  k->cmds[k->numcmds-1] = cmd;

  k->state = OS_IDLE;
}

void osVertex4(OldskoolContext * k, vec4 v) {
  assert(k->state != OS_IDLE);

  k->numverts++;
  k->verts = exalloc(k->verts, sizeof(OldskoolVert[k->numverts]));
  k->verts[k->numverts-1] = (OldskoolVert) {
    .pos = v,
    .color = k->active_color,
  };
}

void osColor4(OldskoolContext * k, vec4 v) {
  k->active_color = v;
}

void osLoadMatrix(OldskoolContext * k, mat4 m) {
  k->nummats = 1;
  k->matstack = exalloc(k->matstack, sizeof(mat4[k->nummats]));
  k->matstack[k->nummats-1] = m;
}

void osPushMatrix(OldskoolContext * k, mat4 m) {
  k->nummats++;
  k->matstack = exalloc(k->matstack, sizeof(mat4[k->nummats]));
  k->matstack[k->nummats-1] = mat4_mul(k->matstack[k->nummats-2], m);
}
void osPopMatrix(OldskoolContext * k) {
  assert(k->nummats > 1);
  k->nummats--;
}

#ifdef _WIN64
int WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, char* cmdline, int cmdshow)
#else
int main(int argc, char ** argv)
#endif
{
  if(VG_Init()) {
    fprintf(stderr, "failed to init\n");
    return 1;
  }
  VGWindow * wind = VG_CreateWindow();
  if(!wind) {
    fprintf(stderr, "failed to make window\n");
    return 1;
  }
  if(VG_CreateAppObjects(wind)) {
    fprintf(stderr, "failed to make vulkan objects\n");
    return 1;
  }
  if(VG_CreateSwapchain(wind)) {
    fprintf(stderr, "failed to make swapchain related objects\n");
    return 1;
  }

  VGBuffer verts = VG_CreateVertexBuffer(wind);
  VGBuffer indices = VG_CreateIndexBuffer(wind);
  VGPipeline pipe = VG_CreatePipeline(wind);

  VkCommandBuffer command_buf[2];
  {
    VkCommandBufferAllocateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = wind->commandpool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 2,
    };
    if(vkAllocateCommandBuffers(wind->device, &createInfo, command_buf) != VK_SUCCESS) {
      fprintf(stderr, "failed to create command pool\n");
      return 1;
    }
  }

  OldskoolContext * osk = osCreate(wind);

  bool running = true;
  bool minimized = false;

  bool frame_parity = false;
  while(running) {
    SDL_Event windowEvent;
    bool resize_event = false;
    while(SDL_PollEvent(&windowEvent)) {
      if(windowEvent.type == SDL_QUIT) {
        running = false;
        break;
      }
      if(windowEvent.type == SDL_WINDOWEVENT) {
        switch(windowEvent.window.event) {
          case SDL_WINDOWEVENT_RESIZED:
          case SDL_WINDOWEVENT_SIZE_CHANGED:
          case SDL_WINDOWEVENT_MAXIMIZED:
            resize_event = true;
            break;
          case SDL_WINDOWEVENT_MINIMIZED:
            minimized = true;
            break;
          case SDL_WINDOWEVENT_RESTORED:
            minimized = false;
            break;
        }
      }
    }

    // first order minimize handler, simple to skip
    if(minimized) {
      continue;
    }

    // second order minimize handler... ugh. 
    // window size of 0 indicates minimized.
    // so if when creating a swapchain we query a size of 0 we bail
    // and set this variable false;
    // and we basically enter a loop of pumping the events
    // trying to create a swapchain, and only if we suceed rendering

    // this only happens on windows so I can't test it
    if(!wind->swapchain_created)
    {
      int w,h;
      SDL_Vulkan_GetDrawableSize(wind->window, &w, &h);
      bool valid_window = w != 0 && h != 0;
      if(!valid_window) {
        VG_RecreateSwapchain(wind);
      }
    }
    if(!wind->swapchain_created) {
      continue;
    }

    vkWaitForFences(wind->device, 1, &wind->frame_fence[frame_parity], VK_TRUE, UINT64_MAX);

    Globals globals;
    WriteGlobals((char*)&globals, wind->swap_extent);

    uint32_t image_index;
    {
      VkResult ret = vkAcquireNextImageKHR(wind->device, wind->swapchain, UINT64_MAX, wind->image_available[frame_parity], VK_NULL_HANDLE, &image_index);
      if(ret == VK_ERROR_OUT_OF_DATE_KHR) {
        if(VG_RecreateSwapchain(wind)) {
          fprintf(stderr, "failed to resize window\n");
          return 1;
        }
        continue;

      } else if(ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "failed to acquire swapchain image\n");
        return 1;
      }
    }

    vkResetFences(wind->device, 1, &wind->frame_fence[frame_parity]);

    {

      // also resets the command buffer
      VkCommandBufferBeginInfo beginfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = NULL,
      };
      if(vkBeginCommandBuffer(command_buf[frame_parity], &beginfo) != VK_SUCCESS) {
        fprintf(stderr, "failed to begin command recording\n");
        return 1;
      }

      VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = wind->primary_frameimage.img,
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = 1,
          .layerCount = 1,
        },
      };

      vkCmdPipelineBarrier(command_buf[frame_parity],
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &image_barrier
      );

      VkRenderPassBeginInfo rpinfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = wind->renderpass,
        .framebuffer = wind->framebuffers[image_index],
        .renderArea.offset = { 0, 0 },
        .renderArea.extent = wind->swap_extent,
        .clearValueCount = 0,
        .pClearValues = NULL,
      };
      vkCmdBeginRenderPass(command_buf[frame_parity], &rpinfo, VK_SUBPASS_CONTENTS_INLINE);

      VkClearAttachment clear_attach = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0,
        .clearValue = {{{ 1, 0.5, 0, 1 }}},
      };

      VkClearRect clear_region = {
        .rect = { { 0, 0, }, wind->swap_extent },
        .baseArrayLayer = 0,
        .layerCount = 1,
      };

      vkCmdClearAttachments(command_buf[frame_parity], 1, &clear_attach, 1, &clear_region);

      osReset(osk);

      // the telltale matrix that the author has opengl brain damage
      mat4 vulkan_squish = { {
        make_vec4(1, 0, 0, 0),
        make_vec4(0,-1, 0, 0),
        make_vec4(0, 0,.5,.5),
        make_vec4(0, 0, 0, 1),
      } };
      osLoadMatrix(osk, vulkan_squish);
      osPushMatrix(osk, globals.proj);
      osPushMatrix(osk, globals.view);
      osPushMatrix(osk, globals.model);

      osBegin(osk, OS_TRIANGLES);
        osColor4(osk, make_vec4(1, 0, 0, 1));
        osVertex4(osk, make_vec4(-0.5, -0.5, 0, 1));

        osColor4(osk, make_vec4(0, 1, 0, 1));
        osVertex4(osk, make_vec4(+0.5, -0.5, 0, 1));

        osColor4(osk, make_vec4(0, 0, 1, 1));
        osVertex4(osk, make_vec4(+0.5, +0.5, 0, 1));

        osColor4(osk, make_vec4(1, 0, 0, 1));
        osVertex4(osk, make_vec4(-0.5, -0.5, 0, 1));

        osColor4(osk, make_vec4(0, 0, 1, 1));
        osVertex4(osk, make_vec4(+0.5, +0.5, 0, 1));

        osColor4(osk, make_vec4(1, 1, 1, 1));
        osVertex4(osk, make_vec4(-0.5, +0.5, 0, 1));
      osEnd(osk);

      osSubmit(osk, command_buf[frame_parity], frame_parity);

      vkCmdEndRenderPass(command_buf[frame_parity]);

      if(vkEndCommandBuffer(command_buf[frame_parity]) != VK_SUCCESS) {
        fprintf(stderr, "failed to end command recording\n");
        return 1;
      }
    }

    VkSemaphore waitSemaphores[] = { wind->image_available[frame_parity] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { wind->render_finished[frame_parity] };
    VkSubmitInfo submitInfo = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = sizeof waitSemaphores / sizeof *waitSemaphores,
      .pWaitSemaphores = waitSemaphores,
      .pWaitDstStageMask = waitStages,

      .commandBufferCount = 1,
      .pCommandBuffers = &command_buf[frame_parity],

      .signalSemaphoreCount = sizeof signalSemaphores / sizeof *signalSemaphores,
      .pSignalSemaphores = signalSemaphores,
    };

    if(vkQueueSubmit(wind->graphics_queue, 1, &submitInfo, wind->frame_fence[frame_parity]) != VK_SUCCESS) {
      fprintf(stderr, "failed to submit command buffers\n");
      return 1;
    }

    VkPresentInfoKHR presentInfo = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = signalSemaphores,

      .swapchainCount = 1,
      .pSwapchains = &wind->swapchain,
      .pImageIndices = &image_index,
      .pResults = NULL,
    };

    {
      VkResult ret = vkQueuePresentKHR(wind->present_queue, &presentInfo);
      if(resize_event || ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
        VG_RecreateSwapchain(wind);
        resize_event = false;
      } else if(ret != VK_SUCCESS) {
        fprintf(stderr, "failed to present frame\n");
        return 1;
      }
    }
    frame_parity = !frame_parity;
  }
  
  VG_WaitIdle(wind);
  vkResetCommandBuffer(command_buf[0], 0);
  vkResetCommandBuffer(command_buf[1], 0);

  osDestroy(osk);

  VG_DestroyBuffer(wind, indices);
  VG_DestroyBuffer(wind, verts);
  VG_DestroyPipeline(wind, pipe);
  VG_DestroyWindow(wind);
  VG_Quit();
  return 0;
}
