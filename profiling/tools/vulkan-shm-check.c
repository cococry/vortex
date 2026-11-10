#include "runara/runara.h"
#include "runara/vulkan.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W           32
#define H           32
#define STRIDE      (W * 4 + 16)
#define VK_OK(call) assert((call) == VK_SUCCESS)
#define RN_OKAY(call)                                                          \
  do {                                                                         \
    rn_result_t r = (call);                                                    \
                                                                                \
    if (r != RN_OK) {                                                          \
      fprintf(stderr, "%s: %s\n", #call, rn_result_to_str(r));                 \
      abort();                                                                 \
    }                                                                          \
  } while (0)

static uint64_t submit_calls;
static uint32_t last_submit_count;

VkResult __real_vkQueueSubmit(VkQueue queue, uint32_t count,
                              const VkSubmitInfo *submits, VkFence fence);

static const char *const *_extensions(void *user, uint32_t *count);

static uint32_t _memory_type(VkPhysicalDevice gpu, uint32_t bits,
                             VkMemoryPropertyFlags flags);

static const char *const *_extensions(void *user, uint32_t *count) {
  (void)user;
  static const char *extensions[] = {
      VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
  *count = 2;

  return extensions;
}

static uint32_t _memory_type(VkPhysicalDevice gpu, uint32_t bits,
                             VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(gpu, &props);

  for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
    if ((bits & (1u << i)) &&
        (props.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  }

  abort();
}

VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkQueueSubmit(VkQueue             queue,
                                                    uint32_t            count,
                                                    const VkSubmitInfo *submits,
                                                    VkFence             fence) {
  submit_calls++;
  last_submit_count = count;
  return __real_vkQueueSubmit(queue, count, submits, fence);
}

int main(void) {
  struct rn_vk_init_options_t vk_options = {
      .mode = RN_VK_TARGET_EXTERNAL,
      .instance_extensions = _extensions,
      .external_format = VK_FORMAT_B8G8R8A8_UNORM,
  };
  struct rn_init_options_t options = {
      .render_backend = RN_RENDER_BACKEND_VULKAN,
      .vk_options = &vk_options,
      .log = {.quiet = true},
  };
  struct rn_state_t *state = rn_state_init(&options);
  assert(state);
  struct rn_vk_native_handles_t vk;
  RN_OKAY(rn_vk_get_native_handles(state, &vk));
  VkImage           output;
  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .extent = {W, H, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage =
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
  };
  VK_OK(vkCreateImage(vk.device, &image_info, NULL, &output));
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(vk.device, output, &requirements);
  VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = requirements.size,
      .memoryTypeIndex =
          _memory_type(vk.physical_device, requirements.memoryTypeBits, 0),
  };
  VkDeviceMemory image_memory;
  VK_OK(vkAllocateMemory(vk.device, &memory_info, NULL, &image_memory));
  VK_OK(vkBindImageMemory(vk.device, output, image_memory, 0));
  struct rn_vk_external_target_desc_t target_desc = {
      .image = output,
      .format = image_info.format,
      .width = W,
      .height = H,
  };
  rn_vk_target_handle_t target;
  RN_OKAY(rn_vk_target_register(state, &target_desc, &target));
  RN_OKAY(rn_vk_target_set(state, target));
  struct rn_box_u32_t canvas = {0, 0, W, H};
  rn_canvas_set_bounds(state, &canvas);

  VkBuffer           buffer;
  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = W * H * 4,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
  };
  VK_OK(vkCreateBuffer(vk.device, &buffer_info, NULL, &buffer));
  vkGetBufferMemoryRequirements(vk.device, buffer, &requirements);
  memory_info.allocationSize = requirements.size;
  memory_info.memoryTypeIndex =
      _memory_type(vk.physical_device, requirements.memoryTypeBits,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory buffer_memory;
  VK_OK(vkAllocateMemory(vk.device, &memory_info, NULL, &buffer_memory));
  VK_OK(vkBindBufferMemory(vk.device, buffer, buffer_memory, 0));
  uint32_t *readback;
  VK_OK(vkMapMemory(vk.device, buffer_memory, 0, VK_WHOLE_SIZE, 0,
                    (void **)&readback));
  VkCommandPool           pool;
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = vk.graphics_queue_family,
  };
  VK_OK(vkCreateCommandPool(vk.device, &pool_info, NULL, &pool));
  VkCommandBuffer             command;
  VkCommandBufferAllocateInfo command_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VK_OK(vkAllocateCommandBuffers(vk.device, &command_info, &command));
  uint32_t                pixels[H][STRIDE / 4];
  uint32_t                expected[H][W];
  struct rn_vk_shm_desc_t desc = {
      .width = W,
      .height = H,
      .stride = STRIDE,
      .data = pixels,
      .size = sizeof(pixels),
  };
  struct rn_image_t image = {0};
  struct rn_image_t spare = {0};
  uint32_t         *large_pixels = calloc(1024 * 512, 4);
  assert(large_pixels);
  struct rn_vk_shm_desc_t large_desc = {
      .width = 1024,
      .height = 512,
      .stride = 4096,
      .data = large_pixels,
      .size = 1024 * 512 * 4,
      .opaque = true,
  };
  struct rn_image_t large[2] = {0};

  for (int opaque = 0; opaque < 2; opaque++) {
    desc.opaque = opaque;

    for (int iteration = 0; iteration < 24; iteration++) {
      uint32_t color =
          (opaque ? 0 : 0x80000000u) | (uint32_t)(iteration + 1) * 0x020101u;

      uint64_t uploads_before = submit_calls;

      if (!iteration) {
        for (int y = 0; y < H; y++) {
          for (int x = 0; x < W; x++) {
            pixels[y][x] = color;
            expected[y][x] = color | 0xff000000u;
          }
        }

        RN_OKAY(rn_vk_image_upload_shm(state, &desc, NULL, 0, &image));
        RN_OKAY(rn_vk_image_upload_shm(state, &desc, NULL, 0, &spare));
      } else {
        struct rn_box_u32_t regions[2] = {{1, 2, 3, 4}, {23, 24, 5, 6}};

        for (int r = 0; r < 2; r++) {
          for (uint32_t y = regions[r].y; y < regions[r].y + regions[r].height;
               y++) {
            for (uint32_t x = regions[r].x; x < regions[r].x + regions[r].width;
                 x++) {
              pixels[y][x] = color;
              expected[y][x] = color | 0xff000000u;
            }
          }
        }

        RN_OKAY(rn_vk_image_upload_shm(state, &desc, regions, 2, &image));
      }

      assert(submit_calls == uploads_before);
      uint64_t before, completed, after;
      RN_OKAY(rn_vk_submission_status(state, &before, &completed));
      struct rn_box_u32_t empty = {W, 0, 1, 1};
      assert(rn_vk_image_upload_shm(state, &desc, &empty, 1, &image) ==
             RN_ERROR_INVALID_ARGUMENT);
      RN_OKAY(rn_vk_image_upload_shm(state, &desc, &empty, 0, &image));
      RN_OKAY(rn_vk_submission_status(state, &after, &completed));
      assert(before == after);
      RN_OKAY(rn_begin(state));
      RN_OKAY(rn_clear_color(state, (rn_color_t){0, 0, 0, 1}));
      RN_OKAY(rn_draw_texture(state, &image, (struct rn_rect_t){0, 0, W, H},
                              (rn_color_t){1, 1, 1, 1}));
      RN_OKAY(rn_end(state));
      assert(submit_calls == uploads_before + 1);
      assert(last_submit_count == 2);
      RN_OKAY(rn_vk_submission_status(state, &after, &completed));
      assert(after == before + 1);
      if (iteration == 23) {
        for (int burst = 0; burst < 12; burst++) {
          uint32_t next =
              (opaque ? 0 : 0x80000000u) | (uint32_t)(burst + 1) * 0x030201u;

          for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
              pixels[y][x] = next;
              expected[y][x] = next | 0xff000000u;
            }
          }

          uploads_before = submit_calls;
          RN_OKAY(
              rn_vk_image_upload_shm(state, &large_desc, NULL, 0, &large[0]));
          RN_OKAY(
              rn_vk_image_upload_shm(state, &large_desc, NULL, 0, &large[1]));
          RN_OKAY(rn_vk_image_upload_shm(state, &desc, NULL, 0, &image));
          RN_OKAY(rn_vk_image_upload_shm(state, &desc, NULL, 0, &spare));
          assert(submit_calls == uploads_before);
          RN_OKAY(rn_begin(state));
          RN_OKAY(rn_clear_color(state, (rn_color_t){0, 0, 0, 1}));
          RN_OKAY(rn_draw_texture(state, &image, (struct rn_rect_t){0, 0, W, H},
                                  (rn_color_t){1, 1, 1, 1}));
          RN_OKAY(rn_end(state));
          assert(submit_calls == uploads_before + 1);
          assert(last_submit_count == 2);
        }
      }

      RN_OKAY(rn_vk_wait_for_render(state));
      VK_OK(vkResetCommandPool(vk.device, pool, 0));
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      VK_OK(vkBeginCommandBuffer(command, &begin));
      VkImageMemoryBarrier barrier = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR,
          .dstQueueFamilyIndex = vk.graphics_queue_family,
          .image = output,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .levelCount = 1,
                               .layerCount = 1},
      };
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                           1, &barrier);
      VkBufferImageCopy copy = {
          .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .layerCount = 1},
          .imageExtent = {W, H, 1},
      };
      vkCmdCopyImageToBuffer(command, output,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                             &copy);
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = 0;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.srcQueueFamilyIndex = vk.graphics_queue_family;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR;
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
                           NULL, 1, &barrier);
      VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0,
                           NULL);
      VK_OK(vkEndCommandBuffer(command));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                             .commandBufferCount = 1,
                             .pCommandBuffers = &command};
      VK_OK(vkQueueSubmit(vk.graphics_queue, 1, &submit, VK_NULL_HANDLE));
      VK_OK(vkQueueWaitIdle(vk.graphics_queue));

      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
          if (readback[y * W + x] != expected[y][x]) {
            fprintf(stderr,
                    "pixel mismatch opaque=%d iteration=%d x=%d y=%d: %08x != "
                    "%08x\n",
                    opaque, iteration, x, y, readback[y * W + x],
                    expected[y][x]);
            abort();
          }
        }
      }
    }

    RN_OKAY(rn_vk_image_upload_shm(state, &desc, NULL, 0, &spare));
    rn_texture_release(state, &spare);
    rn_texture_release(state, &image);
    image = (struct rn_image_t){0};
    spare = (struct rn_image_t){0};
  }

  RN_OKAY(rn_vk_image_upload_shm(state, &large_desc, NULL, 0, &large[0]));
  RN_OKAY(rn_vk_wait_for_render(state));
  rn_texture_release(state, &large[0]);
  rn_texture_release(state, &large[1]);
  free(large_pixels);
  RN_OKAY(rn_vk_target_unregister(state, target));
  vkDestroyCommandPool(vk.device, pool, NULL);
  vkUnmapMemory(vk.device, buffer_memory);
  vkDestroyBuffer(vk.device, buffer, NULL);
  vkFreeMemory(vk.device, buffer_memory, NULL);
  vkDestroyImage(vk.device, output, NULL);
  vkFreeMemory(vk.device, image_memory, NULL);
  rn_state_fini(state);
  puts("PASS: ARGB/XRGB, native channel order, padded rows, disjoint damage, "
       "frame-slot recycling, one queue submission per frame, staging growth, "
       "invalid regions and "
       "pending-upload destruction");
}
