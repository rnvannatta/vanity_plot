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
#include "oldskool_graphics.h"
#include "vector_math.h"

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
  VGWindow * wind = VG_CreateWindow(800, 600);
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
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &image_barrier
      );

      VkRenderPassBeginInfo rpinfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = wind->renderpass,
        .framebuffer = wind->primary_framebuffer,
        .renderArea.offset = { 0, 0 },
        .renderArea.extent = wind->swap_extent,
        .clearValueCount = 0,
        .pClearValues = NULL,
      };
      vkCmdBeginRenderPass(command_buf[frame_parity], &rpinfo, VK_SUBPASS_CONTENTS_INLINE);

      osReset(osk);

      osClearColor(osk, make_vec4( 1, 0.5, 0.0, 1 ));

      // the telltale matrix that the author has opengl brain damage
      mat4 vulkan_squish = { {
        make_vec4(1, 0, 0, 0),
        make_vec4(0,-1, 0, 0),
        make_vec4(0, 0,.5,.5),
        make_vec4(0, 0, 0, 1),
      } };
      osLoadMatrix(osk, vulkan_squish);

      osBegin(osk, OS_TRIANGLES);
        osColor3(osk, make_vec3(0, 0, 0));
        osVertex2(osk, make_vec2(-1, -1));
        osVertex2(osk, make_vec2(+1, -1));
        osVertex2(osk, make_vec2(0, +1));
      osEnd(osk);

      osPushMatrix(osk, globals.proj);
      osPushMatrix(osk, globals.view);
      osPushMatrix(osk, globals.model);

      float poses[4][2] = {
        { -0.5, -0.5 },
        { +0.5, -0.5 },
        { +0.5, +0.5 },
        { -0.5, +0.5 },
      };
      float colors[4][3] = {
        { 1, 0, 0 },
        { 0, 1, 0 },
        { 0, 0, 1 },
        { 1, 1, 1 },
      };
      unsigned indices[6] = {
        0, 1, 2, 0, 2, 3
      };
      osVertexPointer(osk, 2, OS_FLOAT, 4, 0, poses, 0);
      osColorPointer(osk, 3, OS_FLOAT, 4, 0, colors, 0);
      osDrawElements(osk, OS_TRIANGLES, 6, OS_UNSIGNED_INT, indices);

      osSubmit(osk, command_buf[frame_parity], frame_parity);

      vkCmdEndRenderPass(command_buf[frame_parity]);

      {
        VkImageMemoryBarrier image_barriers[] = {
          [0] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = wind->swapimages[image_index],
            .subresourceRange = {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .levelCount = 1,
              .layerCount = 1,
            },
          },
          [1] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = wind->primary_frameimage.img,
            .subresourceRange = {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .levelCount = 1,
              .layerCount = 1,
            },
          },
        };

        vkCmdPipelineBarrier(command_buf[frame_parity],
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0, NULL,
          0, NULL,
          2, image_barriers
        );

        VkImageResolve resolve_region = {
          .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
          },
          .srcOffset = { 0, 0, 0 },
          .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
          },
          .dstOffset = { 0, 0, 0 },
          .extent = { wind->swap_extent.width, wind->swap_extent.height, 1 },
        };

        vkCmdResolveImage(command_buf[frame_parity],
          wind->primary_frameimage.img,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          wind->swapimages[image_index],
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1,
          &resolve_region);

        image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command_buf[frame_parity],
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
          0,
          0, NULL,
          0, NULL,
          1, image_barriers
        );
      }

      if(vkEndCommandBuffer(command_buf[frame_parity]) != VK_SUCCESS) {
        fprintf(stderr, "failed to end command recording\n");
        return 1;
      }
    }

    VkSemaphore waitSemaphores[] = { wind->image_available[frame_parity] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT };
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

  VG_DestroyWindow(wind);
  VG_Quit();
  return 0;
}
