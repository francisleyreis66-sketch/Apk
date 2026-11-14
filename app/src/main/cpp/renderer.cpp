#include "renderer.h"
#include <android/log.h>
#include <vulkan/vulkan_android.h>
#include <algorithm>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RetroGameSNES", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "RetroGameSNES", __VA_ARGS__)

// SPIR-V gerado na compilação (glslc -mfmt=c)
static const uint32_t kVert[] =
#include "present.vert.inc"
    ;
static const uint32_t kFrag[] =
#include "present.frag.inc"
    ;

static constexpr uint32_t TW = 256, TH = 224;
static constexpr VkDeviceSize TEX_BYTES = TW * TH * 4;

struct PushConsts {
  float rect[4];
  float outSize[2];
  float texSize[2];
  int32_t mode;
  int32_t rot;
  float extra[2];
};
static_assert(sizeof(PushConsts) == 48, "layout do push constant");

bool Renderer::init(ANativeWindow* win) {
  shutdown();
  if (!win) return false;
  win_ = win;
  ANativeWindow_acquire(win_);
  last_.assign(TW * TH, 0xFF000000u);
  vk_ = initVulkan();
  if (!vk_) {
    LOGW("Vulkan indisponível: usando desenho pela CPU");
    // libera tudo do Vulkan antes de usar a janela pela CPU
    ANativeWindow* w = win_;
    win_ = nullptr;
    shutdown();
    win_ = w;
    ANativeWindow_setBuffersGeometry(win_, 0, 0, WINDOW_FORMAT_RGBA_8888);
  }
  return true;
}

void Renderer::shutdown() {
  if (device_) vkDeviceWaitIdle(device_);
  destroySwapchain();
  if (device_) {
    for (int i = 0; i < FRAMES; i++) {
      if (fence_[i]) vkDestroyFence(device_, fence_[i], nullptr);
      if (acquired_[i]) vkDestroySemaphore(device_, acquired_[i], nullptr);
      if (staging_[i]) vkDestroyBuffer(device_, staging_[i], nullptr);
      if (stagingMem_[i]) { vkUnmapMemory(device_, stagingMem_[i]); vkFreeMemory(device_, stagingMem_[i], nullptr); }
      fence_[i] = VK_NULL_HANDLE; acquired_[i] = VK_NULL_HANDLE; staging_[i] = VK_NULL_HANDLE; stagingMem_[i] = VK_NULL_HANDLE; stagingPtr_[i] = nullptr;
      cmd_[i] = VK_NULL_HANDLE;
    }
    if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipeLayout_) vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (dpool_) vkDestroyDescriptorPool(device_, dpool_, nullptr);
    if (dsl_) vkDestroyDescriptorSetLayout(device_, dsl_, nullptr);
    if (vs_) vkDestroyShaderModule(device_, vs_, nullptr);
    if (fs_) vkDestroyShaderModule(device_, fs_, nullptr);
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    if (texView_) vkDestroyImageView(device_, texView_, nullptr);
    if (tex_) vkDestroyImage(device_, tex_, nullptr);
    if (texMem_) vkFreeMemory(device_, texMem_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  cmdPool_ = VK_NULL_HANDLE; pipeline_ = VK_NULL_HANDLE; pipeLayout_ = VK_NULL_HANDLE; renderPass_ = VK_NULL_HANDLE;
  dpool_ = VK_NULL_HANDLE; dset_ = VK_NULL_HANDLE; dsl_ = VK_NULL_HANDLE; vs_ = VK_NULL_HANDLE; fs_ = VK_NULL_HANDLE; sampler_ = VK_NULL_HANDLE;
  texView_ = VK_NULL_HANDLE; tex_ = VK_NULL_HANDLE; texMem_ = VK_NULL_HANDLE; device_ = VK_NULL_HANDLE; queue_ = VK_NULL_HANDLE; gpu_ = VK_NULL_HANDLE;
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = VK_NULL_HANDLE;
  if (instance_) vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  texReady_ = false; vk_ = false; needRecreate_ = false; cur_ = 0;
  if (win_) { ANativeWindow_release(win_); win_ = nullptr; }
}

uint32_t Renderer::findMemory(uint32_t typeBits, VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
  return UINT32_MAX;
}

bool Renderer::initVulkan() {
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "Retro Game SNES";
  app.applicationVersion = 1;
  app.pEngineName = "RetroGameSNES";
  app.engineVersion = 1;
  app.apiVersion = VK_API_VERSION_1_0;
  const char* instExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = 2;
  ici.ppEnabledExtensionNames = instExt;
  if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) { instance_ = VK_NULL_HANDLE; return false; }

  VkAndroidSurfaceCreateInfoKHR sci{};
  sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  sci.window = win_;
  if (vkCreateAndroidSurfaceKHR(instance_, &sci, nullptr, &surface_) != VK_SUCCESS) { surface_ = VK_NULL_HANDLE; return false; }

  uint32_t n = 0;
  vkEnumeratePhysicalDevices(instance_, &n, nullptr);
  if (!n) return false;
  std::vector<VkPhysicalDevice> gpus(n);
  vkEnumeratePhysicalDevices(instance_, &n, gpus.data());
  for (VkPhysicalDevice g : gpus) {
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, qf.data());
    for (uint32_t i = 0; i < qn; i++) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(g, i, surface_, &present);
      if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { gpu_ = g; queueFamily_ = i; break; }
    }
    if (gpu_) break;
  }
  if (!gpu_) return false;

  const float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queueFamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = devExt;
  if (vkCreateDevice(gpu_, &dci, nullptr, &device_) != VK_SUCCESS) { device_ = VK_NULL_HANDLE; return false; }
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

  // textura do jogo (256x224 RGBA)
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = VK_FORMAT_R8G8B8A8_UNORM;
  ii.extent = {TW, TH, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device_, &ii, nullptr, &tex_) != VK_SUCCESS) { tex_ = VK_NULL_HANDLE; return false; }
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(device_, tex_, &mr);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = findMemory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX) mai.memoryTypeIndex = findMemory(mr.memoryTypeBits, 0);
  if (mai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device_, &mai, nullptr, &texMem_) != VK_SUCCESS) { texMem_ = VK_NULL_HANDLE; return false; }
  vkBindImageMemory(device_, tex_, texMem_, 0);
  VkImageViewCreateInfo ivi{};
  ivi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ivi.image = tex_;
  ivi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ivi.format = VK_FORMAT_R8G8B8A8_UNORM;
  ivi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (vkCreateImageView(device_, &ivi, nullptr, &texView_) != VK_SUCCESS) { texView_ = VK_NULL_HANDLE; return false; }
  VkSamplerCreateInfo smp{};
  smp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  smp.magFilter = VK_FILTER_LINEAR;
  smp.minFilter = VK_FILTER_LINEAR;
  smp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  smp.addressModeU = smp.addressModeV = smp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  smp.maxLod = 0.0f;
  if (vkCreateSampler(device_, &smp, nullptr, &sampler_) != VK_SUCCESS) { sampler_ = VK_NULL_HANDLE; return false; }

  // buffers de envio (um por quadro em voo), mapeados o tempo todo
  for (int i = 0; i < FRAMES; i++) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = TEX_BYTES;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &staging_[i]) != VK_SUCCESS) { staging_[i] = VK_NULL_HANDLE; return false; }
    VkMemoryRequirements br;
    vkGetBufferMemoryRequirements(device_, staging_[i], &br);
    VkMemoryAllocateInfo bai{};
    bai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bai.allocationSize = br.size;
    bai.memoryTypeIndex = findMemory(br.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (bai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device_, &bai, nullptr, &stagingMem_[i]) != VK_SUCCESS) { stagingMem_[i] = VK_NULL_HANDLE; return false; }
    vkBindBufferMemory(device_, staging_[i], stagingMem_[i], 0);
    if (vkMapMemory(device_, stagingMem_[i], 0, TEX_BYTES, 0, &stagingPtr_[i]) != VK_SUCCESS) return false;
  }

  // descritor da textura
  VkDescriptorSetLayoutBinding bnd{};
  bnd.binding = 0;
  bnd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bnd.descriptorCount = 1;
  bnd.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dli{};
  dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dli.bindingCount = 1;
  dli.pBindings = &bnd;
  if (vkCreateDescriptorSetLayout(device_, &dli, nullptr, &dsl_) != VK_SUCCESS) { dsl_ = VK_NULL_HANDLE; return false; }
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = 1;
  dpi.poolSizeCount = 1;
  dpi.pPoolSizes = &ps;
  if (vkCreateDescriptorPool(device_, &dpi, nullptr, &dpool_) != VK_SUCCESS) { dpool_ = VK_NULL_HANDLE; return false; }
  VkDescriptorSetAllocateInfo dai{};
  dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dai.descriptorPool = dpool_;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &dsl_;
  if (vkAllocateDescriptorSets(device_, &dai, &dset_) != VK_SUCCESS) return false;
  VkDescriptorImageInfo dii{};
  dii.sampler = sampler_;
  dii.imageView = texView_;
  dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet wds{};
  wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  wds.dstSet = dset_;
  wds.dstBinding = 0;
  wds.descriptorCount = 1;
  wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  wds.pImageInfo = &dii;
  vkUpdateDescriptorSets(device_, 1, &wds, 0, nullptr);

  // shaders
  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = sizeof(kVert);
  smi.pCode = kVert;
  if (vkCreateShaderModule(device_, &smi, nullptr, &vs_) != VK_SUCCESS) { vs_ = VK_NULL_HANDLE; return false; }
  smi.codeSize = sizeof(kFrag);
  smi.pCode = kFrag;
  if (vkCreateShaderModule(device_, &smi, nullptr, &fs_) != VK_SUCCESS) { fs_ = VK_NULL_HANDLE; return false; }

  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts)};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &dsl_;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipeLayout_) != VK_SUCCESS) { pipeLayout_ = VK_NULL_HANDLE; return false; }

  // comandos e sincronização
  VkCommandPoolCreateInfo cpi{};
  cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpi.queueFamilyIndex = queueFamily_;
  if (vkCreateCommandPool(device_, &cpi, nullptr, &cmdPool_) != VK_SUCCESS) { cmdPool_ = VK_NULL_HANDLE; return false; }
  VkCommandBufferAllocateInfo cai{};
  cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cai.commandPool = cmdPool_;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cai.commandBufferCount = FRAMES;
  if (vkAllocateCommandBuffers(device_, &cai, cmd_) != VK_SUCCESS) return false;
  for (int i = 0; i < FRAMES; i++) {
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device_, &fi, nullptr, &fence_[i]) != VK_SUCCESS) { fence_[i] = VK_NULL_HANDLE; return false; }
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device_, &si, nullptr, &acquired_[i]) != VK_SUCCESS) { acquired_[i] = VK_NULL_HANDLE; return false; }
  }
  if (!createSwapchain()) return false;
  LOGI("Vulkan pronto (%ux%u)", extent_.width, extent_.height);
  return true;
}

bool Renderer::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps;
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps) != VK_SUCCESS) return false;
  uint32_t fn = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &fn, nullptr);
  if (!fn) return false;
  std::vector<VkSurfaceFormatKHR> fmts(fn);
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &fn, fmts.data());
  VkSurfaceFormatKHR chosen = fmts[0];
  for (const VkSurfaceFormatKHR& f : fmts)
    if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
  VkExtent2D ext = caps.currentExtent;
  if (ext.width == 0xFFFFFFFFu || ext.width == 0) {
    ext.width = (uint32_t)std::max(1, ANativeWindow_getWidth(win_));
    ext.height = (uint32_t)std::max(1, ANativeWindow_getHeight(win_));
  }
  if (ext.width == 0 || ext.height == 0) return false;
  uint32_t count = std::max<uint32_t>(caps.minImageCount + 1, 2);
  if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
  VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  const VkCompositeAlphaFlagBitsKHR alphas[] = {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
  for (VkCompositeAlphaFlagBitsKHR a : alphas) if (caps.supportedCompositeAlpha & a) { alpha = a; break; }
  const VkSurfaceTransformFlagBitsKHR xf = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;

  VkSwapchainCreateInfoKHR sci{};
  sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sci.surface = surface_;
  sci.minImageCount = count;
  sci.imageFormat = chosen.format;
  sci.imageColorSpace = chosen.colorSpace;
  sci.imageExtent = ext;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = xf;
  sci.compositeAlpha = alpha;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // sempre existe; sincroniza com a tela (sem rasgos)
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = VK_NULL_HANDLE;
  if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS) { swapchain_ = VK_NULL_HANDLE; return false; }
  extent_ = ext;

  if (format_ != chosen.format || !renderPass_) {
    if (renderPass_) { vkDestroyRenderPass(device_, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE; }
    if (pipeline_) { vkDestroyPipeline(device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    format_ = chosen.format;
    VkAttachmentDescription att{};
    att.format = format_;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpi, nullptr, &renderPass_) != VK_SUCCESS) { renderPass_ = VK_NULL_HANDLE; return false; }

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vs_;
    st[0].pName = "main";
    st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = fs_;
    st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2;
    gpi.pStages = st;
    gpi.pVertexInputState = &vin;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &ds;
    gpi.layout = pipeLayout_;
    gpi.renderPass = renderPass_;
    gpi.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline_) != VK_SUCCESS) { pipeline_ = VK_NULL_HANDLE; return false; }
  }

  uint32_t ic = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &ic, nullptr);
  images_.resize(ic);
  vkGetSwapchainImagesKHR(device_, swapchain_, &ic, images_.data());
  views_.assign(ic, VK_NULL_HANDLE);
  framebuffers_.assign(ic, VK_NULL_HANDLE);
  renderDone_.assign(ic, VK_NULL_HANDLE);
  for (uint32_t i = 0; i < ic; i++) {
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = images_[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format_;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vi, nullptr, &views_[i]) != VK_SUCCESS) return false;
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = renderPass_;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &views_[i];
    fbi.width = extent_.width;
    fbi.height = extent_.height;
    fbi.layers = 1;
    if (vkCreateFramebuffer(device_, &fbi, nullptr, &framebuffers_[i]) != VK_SUCCESS) return false;
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device_, &si, nullptr, &renderDone_[i]) != VK_SUCCESS) return false;
  }
  return true;
}

void Renderer::destroySwapchain() {
  if (!device_) return;
  for (VkFramebuffer f : framebuffers_) if (f) vkDestroyFramebuffer(device_, f, nullptr);
  for (VkImageView v : views_) if (v) vkDestroyImageView(device_, v, nullptr);
  for (VkSemaphore s : renderDone_) if (s) vkDestroySemaphore(device_, s, nullptr);
  framebuffers_.clear(); views_.clear(); renderDone_.clear(); images_.clear();
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

void Renderer::draw(const uint32_t* pixels, const ViewParams& v) {
  if (!win_) return;
  if (pixels) std::memcpy(last_.data(), pixels, TEX_BYTES);
  if (vk_) {
    if (!drawVulkan(pixels, v)) {
      // falhou de vez (dispositivo perdido): tenta recriar tudo na próxima
      LOGW("Vulkan falhou; recriando");
      ANativeWindow* w = win_;
      ANativeWindow_acquire(w);
      shutdown();
      init(w);
      ANativeWindow_release(w);
    }
  } else drawCpu(last_.data(), v);
}

bool Renderer::drawVulkan(const uint32_t* pixels, const ViewParams& v) {
  if (needRecreate_ || !swapchain_) {
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    needRecreate_ = false;
    if (!createSwapchain()) { needRecreate_ = true; return true; }  // janela ainda sem tamanho: tenta depois
  }
  const int f = cur_;
  if (vkWaitForFences(device_, 1, &fence_[f], VK_TRUE, 1000000000ull) != VK_SUCCESS) return false;
  uint32_t idx = 0;
  VkResult r = vkAcquireNextImageKHR(device_, swapchain_, 1000000000ull, acquired_[f], VK_NULL_HANDLE, &idx);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) { needRecreate_ = true; return true; }
  if (r == VK_TIMEOUT || r == VK_NOT_READY) return true;
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return r != VK_ERROR_DEVICE_LOST;
  if (r == VK_SUBOPTIMAL_KHR) needRecreate_ = true;
  vkResetFences(device_, 1, &fence_[f]);

  const bool upload = pixels != nullptr || !texReady_;
  if (upload) std::memcpy(stagingPtr_[f], last_.data(), TEX_BYTES);

  VkCommandBuffer cb = cmd_[f];
  vkResetCommandBuffer(cb, 0);
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  if (upload) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = texReady_ ? VK_ACCESS_SHADER_READ_BIT : 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.oldLayout = texReady_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = tex_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, texReady_ ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {TW, TH, 1};
    vkCmdCopyBufferToImage(cb, staging_[f], tex_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    texReady_ = true;
  }
  VkClearValue clear{};
  clear.color.float32[3] = 1.0f;
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = renderPass_;
  rbi.framebuffer = framebuffers_[idx];
  rbi.renderArea = {{0, 0}, extent_};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  if (v.w >= 1 && v.h >= 1) {
    VkViewport vp{0, 0, (float)extent_.width, (float)extent_.height, 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, extent_};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &dset_, 0, nullptr);
    PushConsts pc{};
    const float W = (float)extent_.width, H = (float)extent_.height;
    pc.rect[0] = v.x / W * 2.0f - 1.0f;
    pc.rect[1] = v.y / H * 2.0f - 1.0f;
    pc.rect[2] = (v.x + v.w) / W * 2.0f - 1.0f;
    pc.rect[3] = (v.y + v.h) / H * 2.0f - 1.0f;
    pc.outSize[0] = v.rot ? v.h : v.w;
    pc.outSize[1] = v.rot ? v.w : v.h;
    pc.texSize[0] = (float)TW;
    pc.texSize[1] = (float)TH;
    pc.mode = v.filter;
    pc.rot = v.rot;
    vkCmdPushConstants(cb, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
  }
  vkCmdEndRenderPass(cb);
  vkEndCommandBuffer(cb);

  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &acquired_[f];
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &renderDone_[idx];
  r = vkQueueSubmit(queue_, 1, &si, fence_[f]);
  if (r != VK_SUCCESS) return r != VK_ERROR_DEVICE_LOST;
  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &renderDone_[idx];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &idx;
  r = vkQueuePresentKHR(queue_, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) needRecreate_ = true;
  else if (r == VK_ERROR_DEVICE_LOST || r == VK_ERROR_SURFACE_LOST_KHR) return false;
  cur_ = (cur_ + 1) % FRAMES;
  return true;
}

// desenho simples pela CPU (aparelhos sem Vulkan): pixel mais próximo, com giro
void Renderer::drawCpu(const uint32_t* px, const ViewParams& v) {
  ANativeWindow_Buffer buf;
  if (ANativeWindow_lock(win_, &buf, nullptr) != 0) return;
  uint32_t* dst = static_cast<uint32_t*>(buf.bits);
  const int W = buf.width, H = buf.height, stride = buf.stride;
  for (int y = 0; y < H; y++) std::fill(dst + (size_t)y * stride, dst + (size_t)y * stride + W, 0xFF000000u);
  const int x0 = std::max(0, (int)v.x), y0 = std::max(0, (int)v.y);
  const int x1 = std::min(W, (int)(v.x + v.w)), y1 = std::min(H, (int)(v.y + v.h));
  if (v.w >= 1 && v.h >= 1) {
    for (int y = y0; y < y1; y++) {
      const float sy = (y + 0.5f - v.y) / v.h;
      uint32_t* row = dst + (size_t)y * stride;
      for (int x = x0; x < x1; x++) {
        const float sx = (x + 0.5f - v.x) / v.w;
        float u = sx, w = sy;
        if (v.rot == 1) { u = sy; w = 1.0f - sx; }
        else if (v.rot == 2) { u = 1.0f - sy; w = sx; }
        const int tx = std::min((int)TW - 1, std::max(0, (int)(u * TW)));
        const int ty = std::min((int)TH - 1, std::max(0, (int)(w * TH)));
        row[x] = px[ty * TW + tx];
      }
    }
  }
  ANativeWindow_unlockAndPost(win_);
}
