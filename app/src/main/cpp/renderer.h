// Desenho da imagem do jogo na tela do Android.
// Usa Vulkan (com os filtros feitos na placa de vídeo). Se o aparelho não tiver Vulkan,
// cai para um desenho simples pela CPU, para o jogo aparecer de qualquer jeito.
#pragma once
#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

struct ViewParams {
  float x = 0, y = 0, w = 0, h = 0;  // retângulo da imagem, em pixels da superfície
  int rot = 0;                       // 0, 1 = 90° horário, 2 = 90° anti-horário
  int filter = 1;                    // 0 pixels, 1 nítido, 2 suave, 3 LCD, 4 CRT
};

class Renderer {
public:
  ~Renderer() { shutdown(); }
  bool init(ANativeWindow* win);     // true = pronto (Vulkan ou CPU)
  void shutdown();
  bool ready() const { return win_ != nullptr; }
  bool usingVulkan() const { return vk_; }
  // desenha; pixels pode ser nulo para redesenhar a última imagem
  void draw(const uint32_t* pixels, const ViewParams& v);
  void surfaceChanged() { needRecreate_ = true; }

private:
  // ---- Vulkan ----
  bool initVulkan();
  bool createSwapchain();
  void destroySwapchain();
  bool drawVulkan(const uint32_t* pixels, const ViewParams& v);
  uint32_t findMemory(uint32_t typeBits, VkMemoryPropertyFlags props);
  // ---- CPU ----
  void drawCpu(const uint32_t* pixels, const ViewParams& v);

  ANativeWindow* win_ = nullptr;
  bool vk_ = false, needRecreate_ = false;
  std::vector<uint32_t> last_;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queueFamily_ = 0;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{0, 0};
  std::vector<VkImage> images_;
  std::vector<VkImageView> views_;
  std::vector<VkFramebuffer> framebuffers_;
  std::vector<VkSemaphore> renderDone_;
  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
  VkDescriptorPool dpool_ = VK_NULL_HANDLE;
  VkDescriptorSet dset_ = VK_NULL_HANDLE;
  VkShaderModule vs_ = VK_NULL_HANDLE, fs_ = VK_NULL_HANDLE;
  VkCommandPool cmdPool_ = VK_NULL_HANDLE;
  static constexpr int FRAMES = 2;
  VkCommandBuffer cmd_[FRAMES] = {};
  VkFence fence_[FRAMES] = {};
  VkSemaphore acquired_[FRAMES] = {};
  VkBuffer staging_[FRAMES] = {};
  VkDeviceMemory stagingMem_[FRAMES] = {};
  void* stagingPtr_[FRAMES] = {};
  VkImage tex_ = VK_NULL_HANDLE;
  VkDeviceMemory texMem_ = VK_NULL_HANDLE;
  VkImageView texView_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  bool texReady_ = false;
  int cur_ = 0;
};
