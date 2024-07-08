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
#include <stdio.h>
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

static int clampint(int x, int a, int b) {
  if(x < a) return a;
  if(x > b) return b;
  return x;
}


int VG_Init() {
  if(volkInitialize()) {
    fprintf(stderr, "volk failure\n");
    return 1;
  }

  if(SDL_Init(SDL_INIT_VIDEO)) return 1;
  if(SDL_Vulkan_LoadLibrary(NULL)) return 1;

  return 0;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* pData, void* pUserData) {
  fprintf(stderr, "validation layer: %s\n\n", pData->pMessage);
  return VK_FALSE;
}

typedef struct QueriedDevice {
  VkPhysicalDevice dev;
  int score;
  
  uint32_t graphics_index;
  uint32_t present_index;

  VkSampleCountFlagBits msaa_samples;
} QueriedDevice;

QueriedDevice score_gpu(VkPhysicalDevice device, VkSurfaceKHR surf) {
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(device, &deviceProps);

    VkPhysicalDeviceFeatures deviceFeats;
    vkGetPhysicalDeviceFeatures(device, &deviceFeats);

    int score = 1;
    bool suitable = true;
    if(deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      score += 2;
    if(deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
      score += 1;
    if(deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
      score += 1;

    VkSampleCountFlags numsamples = deviceProps.limits.framebufferColorSampleCounts & deviceProps.limits.framebufferDepthSampleCounts;

    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
    VkQueueFamilyProperties queueFamilies[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

    uint32_t graphicsQueueIndex = UINT32_MAX;
    uint32_t presentQueueIndex = UINT32_MAX;
    VkBool32 support;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
      VkQueueFamilyProperties queueFamily = queueFamilies[i];
      if (graphicsQueueIndex == UINT32_MAX && queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        graphicsQueueIndex = i;
      if (presentQueueIndex == UINT32_MAX) {
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surf, &support);
        if(support)
          presentQueueIndex = i;
      }
      // FIXME try to prioritize a queuefamily that supports both
    }
    if(graphicsQueueIndex == UINT32_MAX || presentQueueIndex == UINT32_MAX) {
      suitable = false;
    }

    printf("device %s scored %d\n", deviceProps.deviceName, score * suitable);

    return (QueriedDevice) { .dev = device,
      .score = score * suitable,
      .graphics_index = graphicsQueueIndex,
      .present_index = presentQueueIndex,
      .msaa_samples = numsamples,
    };
}

int cmp_device(struct QueriedDevice const * a, struct QueriedDevice const * b) {
  return -(a->score - b->score);
}

void swap(void * a, void * b, int size) {
  char tmp[size];
  memcpy(tmp, a, size);
  memcpy(a, b, size);
  memcpy(b, tmp, size);
}
void stable_sort(void * ptr, int ct, size_t size, int(*cmp)(void const*, void const*)) {
  // lets get bubblin'
  // why c doesn't offer a stable sort the world will never know
  // and I swear to god if you have 100,000 gpus plugged in and this
  // becomes a bottleneck you deserve it
  bool unsorted;
  do {
    unsorted = false;
    for(int i = 0; i < ct-1; i++) {
      void * a = ptr + (i+0)*size;
      void * b = ptr + (i+1)*size;
      if(cmp(a,b) > 0) {
        unsorted = true;
        swap(a, b, size);
      }
    }
  } while(unsorted);
}

QueriedDevice VG_SelectPhysicalDevice(VGWindow * wind) {
  uint32_t physicalDeviceCount;
  vkEnumeratePhysicalDevices(wind->instance, &physicalDeviceCount, NULL);
  VkPhysicalDevice physicalDevices[physicalDeviceCount];
  vkEnumeratePhysicalDevices(wind->instance, &physicalDeviceCount, physicalDevices);

  QueriedDevice err_device = {
    .dev = VK_NULL_HANDLE,
  };

  if(physicalDeviceCount == 0)
    return err_device;

  printf("found %d devices\n", physicalDeviceCount);
  struct QueriedDevice scored_devices[physicalDeviceCount];
  for(uint32_t i = 0; i < physicalDeviceCount; i++) {
    scored_devices[i] = score_gpu(physicalDevices[i], wind->surface);
  }
  stable_sort(scored_devices, physicalDeviceCount, sizeof(struct QueriedDevice), (int(*)(void const*,void const*))cmp_device);

  struct QueriedDevice ret = scored_devices[0];
  if(ret.score == 0)
    return err_device;
  {
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(ret.dev, &deviceProps);
    printf("selected device %s\n", deviceProps.deviceName);
  }
  return ret;
}

VGWindow * VG_CreateWindow(int w, int h) {
  VGWindow * wind = malloc(sizeof(VGWindow));
  wind->window = SDL_CreateWindow("such vulkan much wow", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  uint32_t sdlExtensionCount;
  SDL_Vulkan_GetInstanceExtensions(wind->window, &sdlExtensionCount, NULL);

  uint32_t instanceExtensionCount = sdlExtensionCount + 1;
  const char* instanceExtensionNames[instanceExtensionCount];
  SDL_Vulkan_GetInstanceExtensions(wind->window, &sdlExtensionCount, instanceExtensionNames);
  instanceExtensionNames[sdlExtensionCount+0] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

  for(int i = 0; i < instanceExtensionCount; i++)
    printf("%s\n", instanceExtensionNames[i]);

  const char * validation_layer = {
    "VK_LAYER_KHRONOS_validation"
  };

  bool found_validation = false;
  {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);

    VkLayerProperties availableLayers[layerCount];
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
    for(int i = 0; i < layerCount; i++) {
      if(!strcmp(availableLayers[i].layerName, validation_layer))
        found_validation = true;
      printf("%s\n", availableLayers[i].layerName);
    }
  }


  const VkInstanceCreateInfo instInfo = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .pApplicationInfo = NULL,
    .enabledLayerCount = found_validation ? 1 : 0,
    .ppEnabledLayerNames = &validation_layer,
    .enabledExtensionCount = instanceExtensionCount,
    .ppEnabledExtensionNames = instanceExtensionNames,
  };
  VkResult ret = vkCreateInstance(&instInfo, NULL, &wind->instance);
  if(ret != VK_SUCCESS) {
    char * reason;
    switch(ret) {
      case VK_ERROR_LAYER_NOT_PRESENT:
        reason = "missing layer";
        break;
      default:
        reason = "unknown error";
    }
    printf("failed to create instance: %s\n", reason);
    return NULL;
  }

  volkLoadInstance(wind->instance);

  if(true)
  {
    VkDebugUtilsMessengerCreateInfoEXT kCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debug_callback
    };
    vkCreateDebugUtilsMessengerEXT(wind->instance, &kCreateInfo, NULL, &wind->debug_mess);
  }

  SDL_Vulkan_CreateSurface(wind->window, wind->instance, &wind->surface);

  QueriedDevice queried_device = VG_SelectPhysicalDevice(wind);
  wind->physical_device = queried_device.dev;
  if(wind->physical_device == VK_NULL_HANDLE) {
    printf("failed to find suitable device supporting vulkan\n");
    return NULL;
  }

  uint32_t graphicsQueueIndex = queried_device.graphics_index;
  uint32_t presentQueueIndex = queried_device.present_index;
  wind->graphics_queue_index = graphicsQueueIndex;
  wind->present_queue_index = presentQueueIndex;

  uint32_t msaa_samples = 16;
  while(!(msaa_samples & queried_device.msaa_samples))
    msaa_samples /= 2;
  if(!msaa_samples)
    printf("no msaa support\n");
  wind->msaa_samples = msaa_samples;

  float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueInfos[] = {
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndex = graphicsQueueIndex,
      .queueCount = 1,
      .pQueuePriorities = &queuePriority,
    },
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndex = presentQueueIndex,
      .queueCount = 1,
      .pQueuePriorities = &queuePriority,
    },
  };
  
  VkPhysicalDeviceFeatures deviceFeatures = { 0 };
  const char* deviceExtensionNames[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  VkDeviceCreateInfo createInfo = {
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,   // sType
    NULL,                                // pNext
    0,                                      // flags
    graphicsQueueIndex == presentQueueIndex ? 1 : 2,                                      // queueCreateInfoCount
    queueInfos,                             // pQueueCreateInfos
    0,                                      // enabledLayerCount
    NULL,                                // ppEnabledLayerNames
    1,                                      // enabledExtensionCount
    deviceExtensionNames,                   // ppEnabledExtensionNames
    &deviceFeatures,                        // pEnabledFeatures
  };
  vkCreateDevice(wind->physical_device, &createInfo, NULL, &wind->device);

  vkGetDeviceQueue(wind->device, graphicsQueueIndex, 0, &wind->graphics_queue);
  vkGetDeviceQueue(wind->device, presentQueueIndex, 0, &wind->present_queue);

  return wind;
}

int VG_CreateAppObjects(VGWindow * wind) {
  uint32_t swapFormatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(wind->physical_device, wind->surface, &swapFormatCount, NULL);
  VkSurfaceFormatKHR swapFormats[swapFormatCount];
  vkGetPhysicalDeviceSurfaceFormatsKHR(wind->physical_device, wind->surface, &swapFormatCount, swapFormats);

  int chosenSwapFormat = -1;
  for(int i = 0; i < swapFormatCount; i++) {
    if(swapFormats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      continue;
    if(swapFormats[i].format != VK_FORMAT_B8G8R8A8_SRGB &&
       swapFormats[i].format != VK_FORMAT_R8G8B8A8_SRGB)
       continue;
    chosenSwapFormat = i;
    break;
  }
  wind->swapformat = swapFormats[chosenSwapFormat];

  {
    VkAttachmentDescription attachments[] = {
      [0] = {
        .format = wind->swapformat.format,
        .samples = wind->msaa_samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,

        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
    };

    VkAttachmentReference colorRef = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorRef,
    };

    VkSubpassDependency subpass_dependency = {
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = sizeof attachments / sizeof *attachments,
      .pAttachments = attachments,
      .subpassCount = 1,
      .pSubpasses = &subpass,

      .dependencyCount = 1,
      .pDependencies = &subpass_dependency,
    };

    if(vkCreateRenderPass(wind->device, &createInfo, NULL, &wind->renderpass) != VK_SUCCESS) {
      fprintf(stderr, "failed to create swapchain image view\n");
      return 1;
    }
  }

  {
    VkCommandPoolCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = wind->graphics_queue_index,
    };
    if(vkCreateCommandPool(wind->device, &createInfo, NULL, &wind->commandpool) != VK_SUCCESS) {
      fprintf(stderr, "failed to create command pool\n");
      return 1;
    }
  }

  {
    VkSemaphoreCreateInfo semaphoreInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for(int i = 0; i < 2; i++) {
      if(vkCreateSemaphore(wind->device, &semaphoreInfo, NULL, &wind->image_available[i]) != VK_SUCCESS ||
         vkCreateSemaphore(wind->device, &semaphoreInfo, NULL, &wind->render_finished[i]) != VK_SUCCESS ||
         vkCreateFence(wind->device, &fenceInfo, NULL, &wind->frame_fence[i]) != VK_SUCCESS) {
        fprintf(stderr, "failed to create sync objects\n");
        return 1;
      }
    }
  }
  wind->swapchain_created = false;
  return 0;
}

static uint32_t VG_FindMemoryType(VGWindow * wind, uint32_t filter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(wind->physical_device, &memProperties);

  for(uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if(filter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
      return i;
  }
  return ~0u;
}

VGImage VG_CreateImageImpl(VGWindow * wind, uint32_t w, uint32_t h, uint32_t levels, VkSampleCountFlagBits num_samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props) {
  VGImage ret = { VK_NULL_HANDLE, VK_NULL_HANDLE };

  VkImageCreateInfo imageInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .extent.width = w,
    .extent.height = h,
    .extent.depth = 1,
    .mipLevels = levels,
    .arrayLayers = 1,
    .format = format,
    .tiling = tiling,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .samples = num_samples ? num_samples : VK_SAMPLE_COUNT_1_BIT,
  };
  if(vkCreateImage(wind->device, &imageInfo, NULL, &ret.img)) {
    ret.img = VK_NULL_HANDLE;
    return ret;
  }
  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(wind->device, ret.img, &memReqs);
  VkMemoryAllocateInfo allocInfo = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = memReqs.size,
    .memoryTypeIndex = VG_FindMemoryType(wind, memReqs.memoryTypeBits, props),
  };
  if(vkAllocateMemory(wind->device, &allocInfo, NULL, &ret.mem)) {
    ret.mem = VK_NULL_HANDLE;
    return ret;
  }
  vkBindImageMemory(wind->device, ret.img, ret.mem, 0);

  VkImageViewCreateInfo viewInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = ret.img,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format,
    .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .subresourceRange.baseMipLevel = 0,
    .subresourceRange.levelCount = levels,
    .subresourceRange.baseArrayLayer = 0,
    .subresourceRange.layerCount = 1,
  };
  if(vkCreateImageView(wind->device, &viewInfo, NULL, &ret.view)) {
    ret.view = VK_NULL_HANDLE;
    return ret;
  }

  return ret;
}

void VG_DestroyImage(VGWindow * wind, VGImage img) {
  vkDestroyImageView(wind->device, img.view, NULL);
  vkDestroyImage(wind->device, img.img, NULL);
  vkFreeMemory(wind->device, img.mem, NULL);
}

VGBuffer VG_CreateBufferImpl(VGWindow * wind, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
  VGBuffer ret = { VK_NULL_HANDLE, VK_NULL_HANDLE };

  VkBufferCreateInfo bufferInfo = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  if(vkCreateBuffer(wind->device, &bufferInfo, NULL, &ret.buf) != VK_SUCCESS) {
    return ret;
  }

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(wind->device, ret.buf, &memReqs);

  VkMemoryAllocateInfo allocInfo = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = memReqs.size,
    .memoryTypeIndex = VG_FindMemoryType(wind, memReqs.memoryTypeBits, properties ),
  };
  if(vkAllocateMemory(wind->device, &allocInfo, NULL, &ret.mem) != VK_SUCCESS) {
    vkDestroyBuffer(wind->device, ret.buf, NULL);
    ret.buf = VK_NULL_HANDLE;
    return ret;
  }
  ret.size = memReqs.size;
  vkBindBufferMemory(wind->device, ret.buf, ret.mem, 0);
  return ret;
}

void VG_DestroyBuffer(VGWindow * wind, VGBuffer buf) {
  vkDestroyBuffer(wind->device, buf.buf, NULL);
  vkFreeMemory(wind->device, buf.mem, NULL);
}

VGImage Create_PrimaryFrameimage(VGWindow * wind) {
  VGImage img = VG_CreateImageImpl(wind, wind->swap_extent.width, wind->swap_extent.height, 1, wind->msaa_samples, wind->swapformat.format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  return img;
}


int VG_CreateSwapchain(VGWindow * wind) {
  VkSurfaceCapabilitiesKHR surface_caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(wind->physical_device, wind->surface, &surface_caps);

  int w,h;
  SDL_Vulkan_GetDrawableSize(wind->window, &w, &h);
  if(w == 0 || h == 0)
    return 0;

  VkExtent2D swap_extent = {
    .width = clampint(w, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
    .height = clampint(h, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height),
  };
  wind->swap_extent = swap_extent;
  // guaranteed. no tearing. mailbox is higher power draw
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

  uint32_t swapImageCount = surface_caps.minImageCount+1;
  if(surface_caps.maxImageCount && swapImageCount > surface_caps.maxImageCount)
    swapImageCount = surface_caps.maxImageCount;

  VkSwapchainCreateInfoKHR swapCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = wind->surface,
    .minImageCount = swapImageCount,
    .imageFormat = wind->swapformat.format,
    .imageColorSpace = wind->swapformat.colorSpace,
    .imageExtent = swap_extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .preTransform = surface_caps.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = present_mode,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE,
  };

  uint32_t queueFamilyIndices[] = { wind->graphics_queue_index, wind->present_queue_index };
  if(wind->graphics_queue_index != wind->present_queue_index) {
    swapCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapCreateInfo.queueFamilyIndexCount = 2;
    swapCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
  }

  if(vkCreateSwapchainKHR(wind->device, &swapCreateInfo, NULL, &wind->swapchain) != VK_SUCCESS) {
    fprintf(stderr, "failed to create swapchain\n");
    return 1;
  }

  vkGetSwapchainImagesKHR(wind->device, wind->swapchain, &swapImageCount, NULL);
  wind->num_swapimages = swapImageCount;
  wind->swapimages = malloc(sizeof(VkImage[swapImageCount]));
  vkGetSwapchainImagesKHR(wind->device, wind->swapchain, &swapImageCount, wind->swapimages);

  wind->swapviews = malloc(sizeof(VkImageView[swapImageCount]));
  for(int i = 0; i < swapImageCount; i++) {
    VkImageViewCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = wind->swapimages[i],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = wind->swapformat.format,
      .components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
      },
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    };
    if(vkCreateImageView(wind->device, &createInfo, NULL, &wind->swapviews[i]) != VK_SUCCESS) {
      fprintf(stderr, "failed to create swapchain image view\n");
      return 1;
    }
  }

  wind->primary_frameimage = Create_PrimaryFrameimage(wind);

  {
    VkImageView attachments[] = {
      wind->primary_frameimage.view,
    };
    VkFramebufferCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = wind->renderpass,
      .attachmentCount = sizeof attachments / sizeof *attachments,
      .pAttachments = attachments,
      .width = swap_extent.width,
      .height = swap_extent.height,
      .layers = 1,
    };
    if(vkCreateFramebuffer(wind->device, &createInfo, NULL, &wind->primary_framebuffer) != VK_SUCCESS) {
      fprintf(stderr, "failed to create swapchain framebuffer\n");
      return 1;
    }
  }
  wind->swapchain_created = true;
  return 0;
}

void VG_DestroySwapchain(VGWindow * wind) {
  VkDevice device = wind->device;
  vkDeviceWaitIdle(device);

  vkDestroyFramebuffer(device, wind->primary_framebuffer, NULL);
  for(int i = 0; i < wind->num_swapimages; i++)
  {
    vkDestroyImageView(device, wind->swapviews[i], NULL);
  }
  VG_DestroyImage(wind, wind->primary_frameimage);
  vkDestroySwapchainKHR(device, wind->swapchain, NULL);

  wind->swapchain_created = false;
}

int VG_RecreateSwapchain(VGWindow * wind) {
  if(wind->swapchain_created)
    VG_DestroySwapchain(wind);

  return VG_CreateSwapchain(wind);
}

void VG_WaitIdle(VGWindow * wind) {
  VkDevice device = wind->device;
  vkDeviceWaitIdle(device);
}

void VG_DestroyWindow(VGWindow * wind) {
  VkDevice device = wind->device;
  vkDeviceWaitIdle(device);

  VG_DestroySwapchain(wind);

  for(int i = 0; i < 2; i++) {
    vkDestroySemaphore(device, wind->image_available[i], NULL);
    vkDestroySemaphore(device, wind->render_finished[i], NULL);
    vkDestroyFence(device, wind->frame_fence[i], NULL);
  }

  vkDestroyCommandPool(device, wind->commandpool, NULL);

  vkDestroyRenderPass(device, wind->renderpass, NULL);
  vkDestroyDevice(device, NULL);
  vkDestroySurfaceKHR(wind->instance, wind->surface, NULL);
  vkDestroyDebugUtilsMessengerEXT(wind->instance, wind->debug_mess, NULL);
  vkDestroyInstance(wind->instance, NULL);

  SDL_DestroyWindow(wind->window);

  free(wind->swapviews);
  free(wind->swapimages);
  free(wind);
}

void VG_DestroyPipeline(VGWindow * wind, VGPipeline pipe) {
  vkDestroyPipeline(wind->device, pipe.pipeline, NULL);
  vkDestroyPipelineLayout(wind->device, pipe.layout, NULL);
}

void VG_Quit() {
  SDL_Vulkan_UnloadLibrary();
  SDL_Quit();
}

