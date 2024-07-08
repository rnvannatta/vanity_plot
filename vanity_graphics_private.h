#ifndef VANITY_GRAPHICS_H
#define VANITY_GRAPHICS_H

#include "volk.h"
#include <SDL2/SDL.h>

typedef struct VGImage {
  VkImage img;
  VkDeviceMemory mem;
  VkImageView view;
} VGImage;

typedef struct VGBuffer {
  VkBuffer buf;
  VkDeviceMemory mem;
  size_t size;
} VGBuffer;

typedef struct VGPipeline {
  VkPipelineLayout layout;
  VkPipeline pipeline;
} VGPipeline;

typedef struct VGWindow {
  // Fundamental objects
  SDL_Window * window;

  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkSurfaceKHR surface;
  VkDevice device;
  VkDebugUtilsMessengerEXT debug_mess;

  uint32_t graphics_queue_index;
  uint32_t present_queue_index;

  VkQueue graphics_queue;
  VkQueue present_queue;
  uint32_t msaa_samples;

  // Application objects
  VkSurfaceFormatKHR swapformat;
  VkRenderPass renderpass;
  VkCommandPool commandpool;
  
  VkFence frame_fence[2];
  VkSemaphore image_available[2];
  VkSemaphore render_finished[2];

  bool swapchain_created;
  // Swapchain dependent objects
  VkExtent2D swap_extent;
  VkSwapchainKHR swapchain;
  
  uint32_t num_swapimages;
  VkImage * swapimages;
  VkImageView * swapviews;

  VGImage primary_frameimage;

  VkFramebuffer primary_framebuffer;

} VGWindow;

int VG_Init();
void VG_Quit();

VGWindow * VG_CreateWindow(int w, int h);
void VG_DestroyWindow(VGWindow * wind);

void VG_WaitIdle(VGWindow * wind);

int VG_CreateAppObjects(VGWindow * wind);
int VG_CreateSwapchain(VGWindow * wind);
int VG_RecreateSwapchain(VGWindow * wind);

// yucky
VGBuffer VG_CreateBufferImpl(VGWindow * wind, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
void VG_DestroyBuffer(VGWindow * wind, VGBuffer buf);

VGImage VG_CreateImageImpl(VGWindow * wind, uint32_t w, uint32_t h, uint32_t levels, VkSampleCountFlagBits num_samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props);
void VG_DestroyImage(VGWindow * wind, VGImage img);

void VG_DestroyPipeline(VGWindow * wind, VGPipeline pipe);

#endif
