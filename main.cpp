#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include "imfilebrowser.h"
#include "imguitoolbar.h"

#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_resize.h" // Include the resize library
#include "stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <HTTPRequest.hpp>
#include <base64.h>
#include <json.hpp>

#include "stable-diffusion.h"

#include <algorithm> // For std::max
#include <cmath>
#include <cstdint> // For uint32_t
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stack>

#ifdef SLOP_WINDOWS_BUILD
#include <windows.h>
#include <shlobj.h>
#endif


using json = nlohmann::json;

namespace fs = std::filesystem;

/*

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to
// maximize ease of testing and compatibility with old VS compilers. To link
// with VS2010-era libraries, VS2015+ requires linking with
// legacy_stdio_definitions.lib, which we do using this pragma. Your own project
// should not be affected, as you are likely to link with a newer binary of GLFW
// that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) &&                                 \
    !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See
// 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

//todo

const std::string config_dir =
    std::getenv("HOME") + std::string("/.config/slop");

*/


//setings location differs by platform

#ifdef SLOP_WINDOWS_BUILD
std::string getAppDataPath() {
    // Buffer to store the path
    char appDataPath[MAX_PATH];

    // Get the AppData path
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {

        return std::string(appDataPath); // Convert to std::string
    } else {
        throw std::runtime_error("Failed to get AppData path.");
    }
}
const std::string config_dir = getAppDataPath() +  std::string("/slop");
#endif

#ifdef SLOP_LINUX_BUILD
const std::string config_dir =
    std::getenv("HOME") + std::string("/.config/slop");


template<typename T>
T max(T a, T b) {
    return std::max(a, b);
}

template<typename T>
T min(T a, T b) {
    return std::min(a, b);
}

#endif


const std::string settings_file = config_dir + "/settings.json";

struct Layer {
  int height;
  int width;
  bool enabled;
  GLuint layerData;

} typedef Layer;

void freeLayer(struct Layer *layer) {
  glDeleteTextures(1, &(layer->layerData));
}

struct FlattenedLayerData {
  unsigned char *data;
  int width;
  int height;
};

struct BrushState {
  int radius;
  float RGBA[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct GenerationState {
  int height = 512;
  int width = 512;
};

struct SelectionState {
  int corner1[2];
  int corner2[2];
  bool dragging = false;
  bool completeSelection = false;
  bool selectionDragMode = false;
  int selectionXOffset = 0;
  int selectionYOffset = 0;
  int initialDragX = 0;
  int initialDragY = 0;
  int selectionZoom = 100;
  GLuint selection;
};

struct LayerResizeState {
  int targetWidth;
  int targetHeight;
};

struct EnvironmentState {
  bool stable_diffusion_path_set;
  std::string stable_diffusion_path;
};

struct ProgramState {
  bool drawMode = false;
  bool inpaintMode = false;
  bool dragMode = false;
  bool selectionMode = false;

  bool brushSettingsOpen = false;
  bool generationSettingsOpen = false;
  bool resizeLayerDialogOpen = false;
  bool warningDialogOpen = false;

  std::string warningMessage;

  int dragInitX;
  int dragInitY;
  int oldViewOffsetX;
  int oldViewOffsetY;
  struct BrushState brushState;
  struct GenerationState generationState;
  struct SelectionState selectionState;
  struct LayerResizeState layerResizeState;

  struct EnvironmentState tempEnvironmentState;
};

// from stable-diffusion.cpp cli

enum SDMode { TXT2IMG, IMG2IMG, IMG2VID, CONVERT, MODE_COUNT };

struct SDParams {
  int n_threads = -1;
  SDMode mode = TXT2IMG;

  std::string model_path;
  std::string vae_path;
  std::string taesd_path;
  std::string esrgan_path;
  std::string controlnet_path;
  std::string embeddings_path;
  std::string stacked_id_embeddings_path;
  std::string input_id_images_path;
  sd_type_t wtype = SD_TYPE_COUNT;
  std::string lora_model_dir;
  std::string output_path = "output.png";
  std::string input_path;
  std::string control_image_path;

  std::string prompt;
  std::string negative_prompt;
  float min_cfg = 1.0f;
  float cfg_scale = 7.0f;
  float style_ratio = 20.f;
  int clip_skip = -1; // <= 0 represents unspecified
  int width = 512;
  int height = 512;
  int batch_count = 1;

  int video_frames = 6;
  int motion_bucket_id = 127;
  int fps = 6;
  float augmentation_level = 0.f;

  sample_method_t sample_method = EULER_A;
  schedule_t schedule = DEFAULT;
  int sample_steps = 20;
  float strength = 0.75f;
  float control_strength = 0.9f;
  rng_type_t rng_type = CUDA_RNG;
  int64_t seed = 42;
  bool verbose = false;
  bool vae_tiling = false;
  bool control_net_cpu = false;
  bool normalize_input = false;
  bool clip_on_cpu = false;
  bool vae_on_cpu = false;
  bool canny_preprocess = false;
  bool color = false;
  int upscale_repeats = 1;
};

enum class ActionType {
  None = 0,
  Import,
  Save,
  Load,
  Export,
  Generate,
  Inpaint,
  Undo,
  BrushSettings,
  GenerationSettings,
  BoxSelect,
  ResizeLayer,
  MergeActiveLayers,
  AddLayer,
  RemoveLayer
};

enum class FilePickerActionType { None = 0, Load, Import, Save, Export };

void create_default_settings() {
  json default_settings = {{"stable_diffusion_path_set", false},
                           {"stable_diffusion_path", ""}};

  fs::create_directories(config_dir); // Create directory if it doesn't exist

  std::ofstream file(settings_file);
  if (file.is_open()) {
    file << default_settings.dump(4); // Pretty print with 4 spaces indentation
    file.close();
  } else {
    std::cerr << "Failed to create settings file." << std::endl;
  }
}

json load_settings() {
  json settings;
  if (fs::exists(settings_file)) {
    std::ifstream file(settings_file);
    if (file.is_open()) {
      file >> settings;
      file.close();
    } else {
      std::cerr << "Failed to open settings file." << std::endl;
    }
  } else {
    create_default_settings();
    settings = json::parse(std::ifstream(settings_file));
  }
  return settings;
}

void save_settings(const json &settings) {
  std::ofstream file(settings_file);
  if (file.is_open()) {
    file << settings.dump(4); // Pretty print with 4 spaces indentation
    file.close();
  } else {
    std::cerr << "Failed to save settings file." << std::endl;
  }
}

void removeLayers(std::vector<Layer> &vec, const std::vector<int> &indices) {
  // Create a copy of indices to sort and work with
  std::vector<int> sorted_indices(indices);

  // Sort indices in descending order to avoid invalidating positions
  std::sort(sorted_indices.begin(), sorted_indices.end(),
            std::greater<size_t>());

  // Remove elements at the specified indices
  for (int index : sorted_indices) {
    if (index < vec.size()) { // Ensure the index is within range
      vec.erase(vec.begin() + index);
    }
  }
}

std::string encode_file_to_base64(const std::string &path) {
  // Open the file in binary mode
  std::ifstream file(path, std::ios::binary);

  // Check if the file was opened successfully
  if (!file) {
    throw std::runtime_error("Unable to open file: " + path);
  }

  // Read the file contents into a stringstream
  std::ostringstream oss;
  oss << file.rdbuf();

  // Get the file contents as a string
  std::string file_contents = oss.str();

  // Encode the file contents to base64
  return base64::to_base64(file_contents);
}

void save_png(const std::string &filename, const unsigned char *image_data,
              int width, int height) {
  // Write the image to a PNG file
  int success =
      stbi_write_png(filename.c_str(), width, height, 4, image_data, width * 4);
  if (!success) {
    std::cout << "Error: Could not write PNG file\n";
  } else {
    std::cout << "saved" << std::endl;
  }
}

// Function to load a vector of Layer structs from a file, including pixel data
bool loadLayersFromFile(std::vector<Layer> &layers,
                        const std::string &filename) {
  std::ifstream inFile(filename, std::ios::binary);
  if (!inFile) {
    std::cerr << "Error opening file for reading: " << filename << std::endl;
  }

  uint32_t magicNumber, versionNumber;

  inFile.read(reinterpret_cast<char *>(&magicNumber), sizeof(magicNumber));
  inFile.read(reinterpret_cast<char *>(&versionNumber), sizeof(versionNumber));

  // Read the number of layers
  uint32_t layerCount;
  inFile.read(reinterpret_cast<char *>(&layerCount), sizeof(layerCount));
  layers.resize(layerCount);

  for (int i = 0; i < layers.size(); i++) {

    uint32_t width, height;

    inFile.read(reinterpret_cast<char *>(&width), sizeof(width));
    inFile.read(reinterpret_cast<char *>(&height), sizeof(height));

    layers[i].width = width;
    layers[i].height = height;

    inFile.read(reinterpret_cast<char *>(&layers[i].enabled),
                sizeof(layers[i].enabled));

    // Allocate buffer for pixel data
    std::vector<unsigned char> pixels(layers[i].height * layers[i].width *
                                      4); // Assuming RGBA format

    // Read pixel data from file
    inFile.read(reinterpret_cast<char *>(pixels.data()), pixels.size());

    // Create and bind texture

    glGenTextures(1, &(layers[i].layerData));
    glBindTexture(GL_TEXTURE_2D, layers[i].layerData);

    // Upload pixel data to texture
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Upload pixels into texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, layers[i].width, layers[i].height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Store texture ID in the layer
  }

  inFile.close();
  return true;
}

bool saveLayersToFile(const std::vector<Layer> &layers,
                      const std::string &filename) {
  std::ofstream outFile(filename, std::ios::binary);
  if (!outFile) {
    std::cerr << "Error opening file for writing: " << filename << std::endl;
    return false;
  }

  // Define magic number and version number as uint32_t
  const uint32_t magicNumber = 12312412;
  const uint32_t versionNumber = 0;

  // Write magic number
  outFile.write(reinterpret_cast<const char *>(&magicNumber),
                sizeof(magicNumber));

  // Write version number
  outFile.write(reinterpret_cast<const char *>(&versionNumber),
                sizeof(versionNumber));

  // Write the number of layers
  uint32_t layerCount = layers.size();
  outFile.write(reinterpret_cast<const char *>(&layerCount),
                sizeof(layerCount));

  for (const auto &layer : layers) {

    const uint32_t width = layer.width; // we want to ensure ints are always
                                        // saved to file with 4 bytes.
    const uint32_t height = layer.height;

    outFile.write(reinterpret_cast<const char *>(&width), sizeof(width));
    outFile.write(reinterpret_cast<const char *>(&height), sizeof(height));
    outFile.write(reinterpret_cast<const char *>(&layer.enabled),
                  sizeof(layer.enabled));

    // Bind the texture and read pixel data
    glBindTexture(GL_TEXTURE_2D, layer.layerData);

    // Allocate buffer for pixel data
    std::vector<unsigned char> pixels(layer.height * layer.width *
                                      4); // Assuming RGBA format
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Write pixel data to file
    outFile.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
  }

  outFile.close();
  return true;
}

// Simple helper function to load an image into a OpenGL texture with common
// settings
bool LoadTextureFromMemory(const void *data, size_t data_size,
                           GLuint *out_texture, int *out_width,
                           int *out_height) {
  // Load from file
  int image_width = 0;
  int image_height = 0;
  unsigned char *image_data =
      stbi_load_from_memory((const unsigned char *)data, (int)data_size,
                            &image_width, &image_height, NULL, 4);
  if (image_data == NULL)
    return false;

  // Create a OpenGL texture identifier
  GLuint image_texture;
  glGenTextures(1, &image_texture);
  glBindTexture(GL_TEXTURE_2D, image_texture);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image_data);
  stbi_image_free(image_data);

  *out_texture = image_texture;
  *out_width = image_width;
  *out_height = image_height;

  return true;
}

// Open and read a file, then forward to LoadTextureFromMemory()
bool LoadTextureFromFile(const char *file_name, GLuint *out_texture,
                         int *out_width, int *out_height) {
  FILE *f = fopen(file_name, "rb");
  if (f == NULL)
    return false;
  fseek(f, 0, SEEK_END);
  size_t file_size = (size_t)ftell(f);
  if (file_size == -1)
    return false;
  fseek(f, 0, SEEK_SET);
  void *file_data = IM_ALLOC(file_size);
  fread(file_data, 1, file_size, f);
  bool ret = LoadTextureFromMemory(file_data, file_size, out_texture, out_width,
                                   out_height);
  IM_FREE(file_data);
  return ret;
}

bool SetPixelColor(GLuint texture_id, int x, int y, unsigned char r,
                   unsigned char g, unsigned char b, unsigned char a, int width,
                   int height) {
  // Check for valid coordinates
  if (x < 0 || x >= width || y < 0 || y >= height) {
    return false; // Invalid coordinates
  }

  // Create a buffer to hold the pixel data
  unsigned char pixel_data[4] = {r, g, b, a};

  // Bind the texture
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Update the specific pixel
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                  pixel_data);

  return true;
}

// Helper function to blend a color with an existing pixel
void blendPixel(std::vector<uint8_t> &buffer, int width, int height, int x,
                int y, int r, int g, int b, int alpha) {
  if (x < 0 || x >= width || y < 0 || y >= height)
    return;                        // Out of bounds check
  int index = (y * width + x) * 4; // Assuming RGBA format

  // Read the existing color
  uint8_t existingR = buffer[index];
  uint8_t existingG = buffer[index + 1];
  uint8_t existingB = buffer[index + 2];
  uint8_t existingA = buffer[index + 3];

  // Blend the new color with the existing color
  float newAlpha = alpha / 255.0f;
  float invAlpha = 1.0f - newAlpha;

  buffer[index] =
      static_cast<uint8_t>(r); // * newAlpha + existingR * invAlpha); // Red
  buffer[index + 1] =
      static_cast<uint8_t>(g); //* newAlpha + existingG * invAlpha); // Green
  buffer[index + 2] =
      static_cast<uint8_t>(b); //* newAlpha + existingB * invAlpha); // Blue
  buffer[index + 3] =
      static_cast<uint8_t>(alpha); // std::max(static_cast<int>(alpha),
                                   // static_cast<int>(existingA))); // Alpha
                                   // (use the maximum of new and existing)
}

bool drawCircle(GLuint texture_id, int centerX, int centerY, int image_width,
                int image_height, int radius, int r, int g, int b, int alpha) {
  // Create a buffer for the pixel data (RGBA format)
  std::vector<uint8_t> pixel_buffer(image_width * image_height * 4,
                                    0); // Initialize with transparent black

  // Bind the texture and read current pixel data into the buffer
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                pixel_buffer.data());

  // Compute radius squared once for efficiency
  int radius_squared = radius * radius;

  // Loop through the bounding box of the circle
  for (int x = -radius; x <= radius; ++x) {
    int y_max = std::sqrt(radius_squared - x * x);
    for (int y = -y_max; y <= y_max; ++y) {
      // Compute the pixel position
      int pixelX = centerX + x;
      int pixelY = centerY + y;

      // Check if the pixel is within the image bounds
      if (pixelX >= 0 && pixelX < image_width && pixelY >= 0 &&
          pixelY < image_height) {
        // Blend the pixel color in the buffer
        blendPixel(pixel_buffer, image_width, image_height, pixelX, pixelY, r,
                   g, b, alpha);
      }
    }
  }

  // Update the texture from the buffer
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image_width, image_height, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixel_buffer.data());

  return true;
}

bool drawLine(GLuint texture_id, int start_x, int start_y, int end_x, int end_y,
              int image_width, int image_height, int radius, int r, int g,
              int b, int alpha) {

  // Create a buffer for the pixel data (RGBA format)
  std::vector<uint8_t> pixel_buffer(image_width * image_height * 4,
                                    0); // Initialize with transparent black

  // Bind the texture and read current pixel data into the buffer
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                pixel_buffer.data());

  float pixel_dist = sqrt(pow(end_x - start_x, 2) + pow(end_y - start_y, 2));

  int radius_squared = radius * radius;

  for (int i = 0; i < (int)pixel_dist; i++) {

    float coefficient = (float)i / pixel_dist;

    int candidate_x = start_x * (1 - coefficient) + end_x * (coefficient);
    int candidate_y = start_y * (1 - coefficient) + end_y * (coefficient);

    for (int x = -radius; x <= radius; ++x) {
      int y_max = std::sqrt(radius_squared - x * x);
      for (int y = -y_max; y <= y_max; ++y) {
        // Compute the pixel position
        int pixelX = candidate_x + x;
        int pixelY = candidate_y + y;

        // Check if the pixel is within the image bounds
        if (pixelX >= 0 && pixelX < image_width && pixelY >= 0 &&
            pixelY < image_height) {
          // Blend the pixel color in the buffer
          blendPixel(pixel_buffer, image_width, image_height, pixelX, pixelY, r,
                     g, b, alpha);
        }
      }
    }
  }

  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image_width, image_height, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixel_buffer.data());

  return true;
}

bool drawSelectionBox(GLuint texture_id, int x1, int y1, int x2, int y2,
                      int image_width, int image_height, int radius, int r,
                      int g, int b, int alpha, bool fill) {
  // Create a buffer for the pixel data (RGBA format) and initialize with
  // transparent black
  std::vector<uint8_t> pixel_buffer(image_width * image_height * 4, 0);

  // Ensure x1 <= x2 and y1 <= y2
  if (x1 > x2)
    std::swap(x1, x2);
  if (y1 > y2)
    std::swap(y1, y2);

  // Draw the box
  for (int x = x1; x <= x2; ++x) {
    for (int y = y1; y <= y2; ++y) {
      // Skip pixels outside the image bounds
      if (x < 0 || x >= image_width || y < 0 || y >= image_height)
        continue;

      // Apply radius effect for rounded corners
      int dx1 = min(x - x1, x2 - x);
      int dy1 = min(y - y1, y2 - y);
      bool is_border = (x == x1 || x == x2 || y == y1 || y == y2 ||
                        dx1 * dx1 + dy1 * dy1 <= radius * radius);

      if (fill || is_border) {
        // Overwrite the pixel color in the buffer
        int pixel_index = (y * image_width + x) * 4;
        pixel_buffer[pixel_index + 0] = r;     // Red
        pixel_buffer[pixel_index + 1] = g;     // Green
        pixel_buffer[pixel_index + 2] = b;     // Blue
        pixel_buffer[pixel_index + 3] = alpha; // Alpha
      }
    }
  }

  // Bind the texture
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Upload the modified pixel buffer back to the texture
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image_width, image_height, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixel_buffer.data());

  return true;
}

bool drawBox(GLuint texture_id, int x1, int y1, int x2, int y2, int image_width,
             int image_height, int radius, int r, int g, int b, int alpha,
             bool fill) {
  // Create a buffer for the original pixel data (RGBA format)
  std::vector<uint8_t> original_pixel_buffer(image_width * image_height * 4);

  // Bind the texture and get the current pixel data
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                original_pixel_buffer.data());

  // Create a buffer for the updated pixel data
  std::vector<uint8_t> updated_pixel_buffer = original_pixel_buffer;

  // Ensure x1 <= x2 and y1 <= y2
  if (x1 > x2)
    std::swap(x1, x2);
  if (y1 > y2)
    std::swap(y1, y2);

  // Draw the box
  for (int x = x1; x <= x2; ++x) {
    for (int y = y1; y <= y2; ++y) {
      // Skip pixels outside the image bounds
      if (x < 0 || x >= image_width || y < 0 || y >= image_height)
        continue;

      // Apply radius effect for rounded corners
      int dx1 = min(x - x1, x2 - x);
      int dy1 = min(y - y1, y2 - y);
      bool is_border = (x == x1 || x == x2 || y == y1 || y == y2 ||
                        dx1 * dx1 + dy1 * dy1 <= radius * radius);

      if (fill || is_border) {
        // Update the pixel color in the buffer
        int pixel_index = (y * image_width + x) * 4;
        updated_pixel_buffer[pixel_index + 0] = r;     // Red
        updated_pixel_buffer[pixel_index + 1] = g;     // Green
        updated_pixel_buffer[pixel_index + 2] = b;     // Blue
        updated_pixel_buffer[pixel_index + 3] = alpha; // Alpha
      }
    }
  }

  // Bind the texture
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Upload the modified pixel buffer back to the texture
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image_width, image_height, GL_RGBA,
                  GL_UNSIGNED_BYTE, updated_pixel_buffer.data());

  return true;
}

bool copyTextureSubset(GLuint *in_texture, GLuint *out_texture, int width,
                       int height, int x1, int y1, int x2, int y2) {
  // Allocate memory for the texture data
  unsigned char *image_data = new unsigned char[width * height * 4]; // RGBA

  glBindTexture(GL_TEXTURE_2D, *in_texture);

  // Read the pixel data from the texture
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

  // Unbind the texture
  glBindTexture(GL_TEXTURE_2D, 0);

  int dst_width = x2 - x1;
  int dst_height = y2 - y1;

  unsigned char *cropped_image_data =
      new unsigned char[dst_width * dst_height * 4];

  // Transfer pixel values to cropped_image_data
  for (int y = 0; y < dst_height; ++y) {
    for (int x = 0; x < dst_width; ++x) {
      int src_index = ((y1 + y) * width + (x1 + x)) * 4;
      int dst_index = (y * dst_width + x) * 4;
      cropped_image_data[dst_index] = image_data[src_index];
      cropped_image_data[dst_index + 1] = image_data[src_index + 1];
      cropped_image_data[dst_index + 2] = image_data[src_index + 2];
      cropped_image_data[dst_index + 3] = image_data[src_index + 3];
    }
  }

  // Create an OpenGL texture identifier
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dst_width, dst_height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, cropped_image_data);

  // Free the allocated image data
  delete[] image_data;
  delete[] cropped_image_data;

  *out_texture = texture_id;

  return true;
}

bool copyTextureToRegion(GLuint *in_texture, GLuint *out_texture, int src_width,
                         int src_height, int dst_width, int dst_height,
                         int x_offset, int y_offset, bool overwrite) {
  // Allocate memory for the texture data
  unsigned char *image_data =
      new unsigned char[src_width * src_height * 4]; // RGBA

  glBindTexture(GL_TEXTURE_2D, *in_texture);

  // Read the pixel data from the texture
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

  // Unbind the texture
  glBindTexture(GL_TEXTURE_2D, 0);

  unsigned char *result_image_data =
      new unsigned char[dst_width * dst_height * 4];

  glBindTexture(GL_TEXTURE_2D, *out_texture);

  // Read the pixel data from the texture
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, result_image_data);

  // Unbind the texture
  glBindTexture(GL_TEXTURE_2D, 0);

  // Transfer pixel values to cropped_image_data
  for (int y = 0; y < src_height; ++y) {
    for (int x = 0; x < src_width; ++x) {

      int src_index = (y * src_width + x) * 4;
      int dst_index = ((y_offset + y) * dst_width + (x_offset + x)) * 4;
      if (dst_index >= 0 && dst_index < dst_width * dst_height * 4 &&
          y_offset + y < dst_height && x_offset + x < dst_width &&
          y_offset + y >= 0 && x_offset + x >= 0) {

        // Read alpha values (normalized to [0, 1])
        float newAlpha = image_data[src_index + 3] / 255.0f;
        float oldAlpha = result_image_data[dst_index + 3] / 255.0f;

        // Read color values
        float srcRed = image_data[src_index + 0];
        float srcGreen = image_data[src_index + 1];
        float srcBlue = image_data[src_index + 2];

        float dstRed = result_image_data[dst_index + 0];
        float dstGreen = result_image_data[dst_index + 1];
        float dstBlue = result_image_data[dst_index + 2];

        // If newAlpha is 0, copy the source pixel directly
        if (oldAlpha == 0 || overwrite) {
          result_image_data[dst_index + 0] = srcRed;
          result_image_data[dst_index + 1] = srcGreen;
          result_image_data[dst_index + 2] = srcBlue;
          result_image_data[dst_index + 3] =
              static_cast<unsigned char>(newAlpha * 255.0f);
        } else {
          // Blending using alpha compositing
          float blendedAlpha = newAlpha + oldAlpha * (1 - newAlpha);

          float blendedRed =
              (srcRed * newAlpha + dstRed * oldAlpha * (1 - newAlpha)) /
              blendedAlpha;
          float blendedGreen =
              (srcGreen * newAlpha + dstGreen * oldAlpha * (1 - newAlpha)) /
              blendedAlpha;
          float blendedBlue =
              (srcBlue * newAlpha + dstBlue * oldAlpha * (1 - newAlpha)) /
              blendedAlpha;

          // Clamp the color values to [0, 255]
          result_image_data[dst_index + 0] =
              static_cast<unsigned char>(std::clamp(blendedRed, 0.0f, 255.0f));
          result_image_data[dst_index + 1] = static_cast<unsigned char>(
              std::clamp(blendedGreen, 0.0f, 255.0f));
          result_image_data[dst_index + 2] =
              static_cast<unsigned char>(std::clamp(blendedBlue, 0.0f, 255.0f));
          result_image_data[dst_index + 3] = static_cast<unsigned char>(
              std::clamp(blendedAlpha * 255.0f, 0.0f, 255.0f));
        }
      }
    }
  }

  // Create an OpenGL texture identifier
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dst_width, dst_height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, result_image_data);

  // Free the allocated image data
  delete[] image_data;
  delete[] result_image_data;

  *out_texture = texture_id;

  return true;
}

bool GenerateTexture(ProgramState *state, GLuint *out_texture,
                     std::string prompt_string, int width, int height,
                     std::string model_path) {

  int sd_channels = 3;

  SDParams params;

  params.model_path = model_path;
  params.prompt = prompt_string; //"insidious spiders and octopus";
  params.sample_steps = 20;
  params.seed = std::rand();

  sd_ctx_t *sd_ctx = new_sd_ctx(
      params.model_path.c_str(), params.vae_path.c_str(),
      params.taesd_path.c_str(), params.controlnet_path.c_str(),
      params.lora_model_dir.c_str(), params.embeddings_path.c_str(),
      params.stacked_id_embeddings_path.c_str(), true, params.vae_tiling, true,
      params.n_threads, params.wtype, params.rng_type, params.schedule,
      params.clip_on_cpu, params.control_net_cpu, params.vae_on_cpu);

  sd_image_t *results;
  sd_image_t *control_image = NULL;

  results =
      txt2img(sd_ctx, params.prompt.c_str(), params.negative_prompt.c_str(),
              params.clip_skip, params.cfg_scale, params.width, params.height,
              params.sample_method, params.sample_steps, params.seed,
              params.batch_count, control_image, params.control_strength,
              params.style_ratio, params.normalize_input,
              params.input_id_images_path.c_str());

  if (sd_ctx == NULL) {
    printf("new_sd_ctx_t failed\n");
    return 1;
  }

  // Allocate memory for the random texture data
  unsigned char *image_data = new unsigned char[512 * 512 * 4]; // RGBA

  // Seed the random number generator

  // Fill the image data with random values
  // for (int i = 0; i < width * height * 4; ++i)
  // {
  //     image_data[i] = static_cast<unsigned char>(std::rand() % 256);
  // }

  for (int i = 0; i < 512; i++) {
    for (int c = 0; c < 512; c++) {
      int dst_offset = i * 512 * 4 + c * 4;
      int src_offset = i * 512 * 3 + c * 3;

      image_data[dst_offset] = results->data[src_offset];
      image_data[dst_offset + 1] = results->data[src_offset + 1];
      image_data[dst_offset + 2] = results->data[src_offset + 2];
      image_data[dst_offset + 3] = 255;
    }
  }

  unsigned char *rescaled_image = (unsigned char *)malloc(width * height * 4);
  stbir_resize_uint8(image_data, 512, 512, 0, rescaled_image, width, height, 0,
                     4);

  // Create an OpenGL texture identifier
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, rescaled_image);

  // Free the allocated image data
  delete[] image_data;

  *out_texture = texture_id;

  free_sd_ctx(sd_ctx);

  return true;
}

bool copyTexture(GLuint *in_texture, GLuint *out_texture, int width,
                 int height) {
  // Allocate memory for the random texture data
  unsigned char *image_data = new unsigned char[width * height * 4]; // RGBA

  // Seed the random number generator

  glBindTexture(GL_TEXTURE_2D, *in_texture);

  // Allocate the buffer to hold the pixel data
  // image_data.resize(width * height * 4); // Assuming RGBA format with 4
  // channels (4 bytes per pixel)

  // Read the pixel data from the texture
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

  // Unbind the texture
  glBindTexture(GL_TEXTURE_2D, 0);

  // Fill the image data with random values
  // for (int i = 0; i < width * height * 4; ++i)
  // {
  //     image_data[i] = static_cast<unsigned char>(std::rand() % 256);
  // }

  // Create an OpenGL texture identifier
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image_data);

  // Free the allocated image data
  delete[] image_data;

  *out_texture = texture_id;

  return true;
}

bool GenerateEmptyTexture(GLuint *out_texture, int width, int height) {
  // Allocate memory for the random texture data
  unsigned char *image_data = new unsigned char[width * height * 4]; // RGBA

  // Seed the random number generator

  // Fill the image data with random values
  for (int i = 0; i < width * height * 4; ++i) {
    image_data[i] = static_cast<unsigned char>(0);
  }

  // Create an OpenGL texture identifier
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image_data);

  // Free the allocated image data
  delete[] image_data;

  *out_texture = texture_id;

  return true;
}

bool GenerateRandomTexture(GLuint *out_texture, int width, int height) {
  // Allocate memory for the random texture data
  unsigned char *image_data = new unsigned char[width * height * 4]; // RGBA

  // Seed the random number generator

  // Fill the image data with random values
  for (int i = 0; i < width * height * 4; ++i) {
    image_data[i] = static_cast<unsigned char>(std::rand() % 256);
  }

  // Create an OpenGL texture identifier
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image_data);

  // Free the allocated image data
  delete[] image_data;

  *out_texture = texture_id;

  return true;
}

bool generateUniformTexture(GLuint *out_texture, int width, int height, int r,
                            int g, int b, int alpha) {
  // Allocate memory for the random texture data
  unsigned char *image_data = new unsigned char[width * height * 4]; // RGBA

  // Seed the random number generator

  // Fill the image data with random values
  for (int i = 0; i < width * height * 4; i += 4) {
    image_data[i] = static_cast<unsigned char>(r);
    image_data[i + 1] = static_cast<unsigned char>(g);
    image_data[i + 2] = static_cast<unsigned char>(b);
    image_data[i + 3] = static_cast<unsigned char>(alpha);
  }

  // Create an OpenGL texture identifier
  GLuint texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Setup filtering parameters for display
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload pixels into texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image_data);

  // Free the allocated image data
  delete[] image_data;

  *out_texture = texture_id;

  return true;
}

std::vector<Layer> deepCopyLayers(std::vector<Layer> &layers) {
  std::vector<Layer> copiedLayers;
  copiedLayers.reserve(layers.size());

  for (auto &layer : layers) {
    Layer newLayer;
    newLayer.height = layer.height;
    newLayer.width = layer.width;
    newLayer.enabled = layer.enabled;

    bool ret = copyTexture(&(layer.layerData), &(newLayer.layerData),
                           newLayer.width, newLayer.height);
    IM_ASSERT(ret);

    copiedLayers.push_back(newLayer);
  }

  return copiedLayers;
}

bool showLayerInfo(std::vector<Layer> &layers, int window_width,
                   int window_height) {

  bool return_value = false;

  ImGui::SetNextWindowPos(ImVec2(window_width - 90, 20));
  ImGui::SetNextWindowSize(ImVec2(512 + 20, 512 + 20));

  static bool show_layer_info = true;
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
  ImGui::Begin("Layer Information", &show_layer_info, flags);
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

  for (int i = 0; i < layers.size(); i++) {
    static bool no_background = false;
    if (ImGui::Button((std::string("up##up: ") + std::to_string(i)).c_str())) {
      if (i < layers.size() - 1) {
        std::swap(layers[i], layers[i + 1]);
        return_value = true;
      }
    }
    return_value =
        return_value ||
        ImGui::Checkbox((std::string("Layer: ") + std::to_string(i)).c_str(),
                        &layers[i].enabled);
    if (ImGui::Button(
            (std::string("down##dowb: ") + std::to_string(i)).c_str())) {
      if (i > 0) {
        std::swap(layers[i - 1], layers[i]);
        return_value = true;
      }
    }
  }

  ImGui::End();

  return return_value;
}

void showBrushSettingsPopup(ProgramState *state) {

  bool return_value = false;

  if (state->brushSettingsOpen) {
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Brush Settings", &(state->brushSettingsOpen));

    ImGui::DragInt("Brush Radius", &(state->brushState.radius), 1.0f, 1, 100);
    ImGui::ColorEdit4("Brush Color/Transparency", state->brushState.RGBA);

    if (ImGui::Button("OK")) {
      state->brushSettingsOpen = false;
    }

    ImGui::End();
  }
}

void showWarningPopup(ProgramState *state) {

  bool return_value = false;

  if (state->warningDialogOpen) {
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Warning", &(state->warningDialogOpen));
    ImGui::Text(state->warningMessage.c_str());
    if (ImGui::Button("OK")) {
      state->warningDialogOpen = false;
    }

    ImGui::End();
  }
}

void showLayerResizePopup(ProgramState *state, Layer *topActiveLayer) {

  bool return_value = false;

  if (state->resizeLayerDialogOpen) {
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Resize Layer", &(state->resizeLayerDialogOpen));

    ImGui::DragInt("Width:", &(state->layerResizeState.targetWidth), 1.0f, 1,
                   10000);
    ImGui::DragInt("Height:", &(state->layerResizeState.targetHeight), 1.0f, 1,
                   10000);

    if (ImGui::Button("OK")) {
      state->resizeLayerDialogOpen = false;

      GLuint resizedTexture;

      generateUniformTexture(
          &resizedTexture, state->layerResizeState.targetWidth,
          state->layerResizeState.targetHeight, 255, 255, 255, 255);
      copyTextureToRegion(&(topActiveLayer->layerData), &resizedTexture,
                          topActiveLayer->width, topActiveLayer->height,
                          state->layerResizeState.targetWidth,
                          state->layerResizeState.targetHeight, 0, 0, true);

      topActiveLayer->layerData = resizedTexture;
      topActiveLayer->width = state->layerResizeState.targetWidth;
      topActiveLayer->height = state->layerResizeState.targetHeight;
    }

    ImGui::End();
  }
}

void showGenerationSettingsPopup(ProgramState *state) {

  bool return_value = false;

  if (state->generationSettingsOpen) {
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Generation Settings", &(state->generationSettingsOpen));

    // ImGui::DragInt("Height", &(state->generationState.height), 1.0f, 1,
    // 1024); ImGui::DragInt("Width", &(state->generationState.width), 1.0f, 1,
    // 1024);

    std::string path = state->tempEnvironmentState.stable_diffusion_path;

    static char text[1024];

    std::copy(path.begin(), path.end(), text);
    text[path.size()] = '\0'; // Manually add the null terminator

    ImGui::InputText("Model Path", text, IM_ARRAYSIZE(text));

    state->tempEnvironmentState.stable_diffusion_path = std::string(text);

    if (ImGui::Button("OK")) {
      state->generationSettingsOpen = false;

      json settings = load_settings();
      settings["stable_diffusion_path_set"] = true;
      settings["stable_diffusion_path"] = std::string(text);
      save_settings(settings);
    }

    ImGui::End();
  }
}

bool ShowGenerateTextInputPopup(ProgramState *state, GLuint *texture_id,
                                int *width, int *height, bool *open) {

  bool return_value = false;

  if (*open) {
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Enter Prompt", open);
    // Pass a pointer to our bool variable (the window will have a closing
    // button that will clear the bool when clicked)

    static char text[128] = "";
    ImGui::InputText("Input", text, IM_ARRAYSIZE(text));

    if (ImGui::Button("OK")) {
      *open = false;
      std::string prompt = std::string(text);

      json settings = load_settings();
      if (settings["stable_diffusion_path_set"]) {
        GenerateTexture(state, texture_id, prompt, state->generationState.width,
                        state->generationState.height,
                        settings["stable_diffusion_path"]);
        *width = state->generationState.width;
        *height = state->generationState.height;
      } else {
        state->warningDialogOpen = true;
        state->warningMessage =
            "Please Specify a model path. Generate -> Generation Settings.";
      }

      return_value = true;
    }

    ImGui::End();
  }

  return return_value;
}

int getTopActiveLayerIndex(std::vector<Layer> &layers) {
  int index = -1;
  for (int i = 0; i < layers.size(); i++) {
    if (layers[i].enabled) {
      index = i;
    }
  }
  return index;
}

bool checkContiguousSelection(std::vector<Layer> &layers) {
  bool foundActiveLayer = false;
  bool endedActiveSegment = false;
  int activeCount = 0;
  for (int i = 0; i < layers.size(); i++) {
    if (!layers[i].enabled && !foundActiveLayer) {

    } else if (layers[i].enabled && !foundActiveLayer) {
      foundActiveLayer = true;
      activeCount += 1;
    } else if (layers[i].enabled && foundActiveLayer && !endedActiveSegment) {
      activeCount += 1;
    } else if (!layers[i].enabled && !endedActiveSegment) {
      endedActiveSegment = true;
    } else {
      return false;
    }
  }
  return true && activeCount >= 2;
}

struct FlattenedLayerData getFlattenedLayerData(std::vector<Layer> &layers) {
  if (layers.empty()) {
    FlattenedLayerData empty;
    empty.height = -1;
    empty.width = -1;
    empty.data = nullptr;
    return empty;
  }

  // Determine the dimensions of the largest layer
  int maxWidth = 0;
  int maxHeight = 0;

  for (const auto &layer : layers) {
    if (layer.enabled) {
      maxWidth = max(maxWidth, layer.width);
      maxHeight = max(maxHeight, layer.height);
    }
  }

  if (maxWidth == 0 || maxHeight == 0) {
    FlattenedLayerData empty;
    empty.height = -1;
    empty.width = -1;
    empty.data = nullptr;
    return empty;
  }

  // Allocate buffer for the flattened output
  unsigned char *output = new unsigned char[maxWidth * maxHeight * 4]; // RGBA
  std::memset(output, 0,
              maxWidth * maxHeight * 4); // Initialize to transparent black

  // Function to read pixels from an OpenGL texture
  auto readTexturePixels = [](GLuint textureID, int width, int height,
                              unsigned char *buffer) {
    glBindTexture(GL_TEXTURE_2D, textureID);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
  };

  // Blend layer data into the output buffer
  for (const auto &layer : layers) {
    if (layer.enabled) {
      unsigned char *layerData =
          new unsigned char[layer.width * layer.height * 4];

      readTexturePixels(layer.layerData, layer.width, layer.height, layerData);

      for (int y = 0; y < layer.height; ++y) {
        for (int x = 0; x < layer.width; ++x) {
          int layerIndex = (y * layer.width + x) * 4;
          int outputIndex = (y * maxWidth + x) * 4;

          unsigned char r = layerData[layerIndex];
          unsigned char g = layerData[layerIndex + 1];
          unsigned char b = layerData[layerIndex + 2];
          unsigned char a = layerData[layerIndex + 3];

          float newAlpha = a / 255.0f;
          float oldAlpha = output[outputIndex + 3] / 255.0f;

          float blendedAlpha = newAlpha + oldAlpha * (1 - newAlpha);

          float alpha_out = oldAlpha + (newAlpha * (1 - oldAlpha));
          output[outputIndex + 3] = static_cast<unsigned char>(alpha_out * 255);

          output[outputIndex] = static_cast<unsigned char>(
              (((output[outputIndex] * (oldAlpha) * (1 - newAlpha)) +
                layerData[layerIndex] * newAlpha) /
               blendedAlpha));
          output[outputIndex + 1] = static_cast<unsigned char>(
              (((output[outputIndex + 1] * (oldAlpha) * (1 - newAlpha)) +
                layerData[layerIndex + 1] * newAlpha) /
               blendedAlpha));
          output[outputIndex + 2] = static_cast<unsigned char>(
              (((output[outputIndex + 2] * (oldAlpha) * (1 - newAlpha)) +
                layerData[layerIndex + 2] * newAlpha) /
               blendedAlpha));
        }
      }
      delete[] layerData;
    }
  }

  struct FlattenedLayerData data;

  data.width = maxWidth;
  data.height = maxHeight;
  data.data = output;

  return data;
}

void getInpaintResult(Layer &layer, std::string prompt) {

  std::vector<Layer> layers = {layer};

  const unsigned char *result = getFlattenedLayerData(layers).data;

  save_png("to_inpaint.png", result, layer.width, layer.height);

  // Define the URL
  std::string url = "http://127.0.0.1:7860/sdapi/v1/img2img";

  // Encode images to base64
  std::string init_image_base64;
  std::string mask_base64;
  try {
    init_image_base64 =
        encode_file_to_base64("to_inpaint.png");
    mask_base64 = encode_file_to_base64("mask.png");
  } catch (const std::runtime_error &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return;
  }

  /**/
  // Create the payload as a JSON string
  std::string payload = R"({
        "prompt": ")" + prompt +
                        R"(",
        "seed": 1,
        "steps": 20,
        "resize_mode": 1,
        "inpainting_fill": 0,
        "inpainting_mask_invert": 0,
        "mask_blur": 4,
        "include_init_images": true,
        "width": )" + std::to_string(layer.width) +
                        R"(,
        "height":)" + std::to_string(layer.height) +
                        R"(,
        "denoising_strength": 0.75,
        "cfg_scale": 7,
        "n_iter": 1,
        "init_images": [")" +
                        init_image_base64 + R"("],
        "batch_size": 1,
        "mask": ")" + mask_base64 +
                        R"(",
        "inpaint_full_res": 0
    })";

  try {
    http::Request request{"http://127.0.0.1:7860/sdapi/v1/img2img"};
    // const std::string body = "{\"foo\": 1, \"bar\": \"baz\"}";
    const auto response =
        request.send("POST", payload, {{"Content-Type", "application/json"}});
    std::string responseString =
        std::string{response.body.begin(), response.body.end()};
    std::string imageString =
        to_string(json::parse(responseString)["images"][0]);

    imageString = imageString.substr(1, imageString.length() - 2);

    std::string imageData = base64::from_base64(imageString);

    // Write the decoded image to a file
    std::ofstream out_file("output.png", std::ios::binary);
    out_file.write(imageData.c_str(), imageData.size());
    out_file.close();

    LoadTextureFromFile("output.png", &(layer.layerData), &layer.width,
                        &layer.height);

  } catch (const std::exception &e) {
    std::cerr << "Request failed, error: " << e.what() << '\n';
  }
}

void saveMaskPng(GLuint maskTexture, int width, int height) {
  // Step 1: Read the texture data from the GPU
  std::vector<unsigned char> pixels(width * height * 4); // RGBA format

  glBindTexture(GL_TEXTURE_2D, maskTexture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  // Step 2: Process the texture data
  std::vector<unsigned char> imageData(width * height * 4); // RGBA format

  for (int i = 0; i < width * height; ++i) {
    unsigned char r = pixels[i * 4];
    unsigned char g = pixels[i * 4 + 1];
    unsigned char b = pixels[i * 4 + 2];

    // If any color component is non-zero, we set the pixel to white; otherwise,
    // black
    if (r > 0 || g > 0 || b > 0) {
      imageData[i * 4] = 255;     // Red
      imageData[i * 4 + 1] = 255; // Green
      imageData[i * 4 + 2] = 255; // Blue
    } else {
      imageData[i * 4] = 0;     // Red
      imageData[i * 4 + 1] = 0; // Green
      imageData[i * 4 + 2] = 0; // Blue
    }

    imageData[i * 4 + 3] = 255; // Alpha (fully opaque)
  }

  // Step 3: Write the processed image data to a PNG file
  stbi_write_png("mask.png", width, height, 4, imageData.data(), width * 4);
}

bool ShowInpaintTextInputPopup(Layer &layer, GLuint inpaintOverlay,
                               bool *open) {

  bool return_value = false;

  if (*open) {
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Enter Prompt", open);
    // Pass a pointer to our bool variable (the window will have a closing
    // button that will clear the bool when clicked)

    static char text[128] = "";
    ImGui::InputText("Input", text, IM_ARRAYSIZE(text));

    if (ImGui::Button("OK")) {
      *open = false;
      std::string prompt = std::string(text);
      saveMaskPng(inpaintOverlay, layer.width, layer.height);
      getInpaintResult(layer, prompt);
      return_value = true;
    }

    ImGui::End();
  }

  return return_value;
}

static void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Main code
int main(int, char **) {
 
  bool prompt_popup_open = false;
  std::string prompt_string = "";

  FilePickerActionType currentFilePickerAction = FilePickerActionType::None;

  std::srand((unsigned int)std::time(nullptr));

  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
    return 1;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
  // GL 3.2 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 3.2+ only
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on Mac
#else
  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);  // 3.2+
  // only glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // 3.0+ only
#endif

  // Create window with graphics context
  GLFWwindow *window = glfwCreateWindow(612, 612, "SLOP", nullptr, nullptr);
  if (window == nullptr)
    return 1;
  glfwMakeContextCurrent(window);
  // glfwSwapInterval(1); // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;

  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
  ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
  ImGui_ImplOpenGL3_Init(glsl_version);

  std::stack<std::vector<Layer>> history;

  std::vector<Layer> initialLayers;

  Layer initialLayer;
  initialLayer.width = 512;
  initialLayer.height = 512;
  initialLayer.layerData = 0;
  initialLayer.enabled = true;
  bool ret =
      generateUniformTexture(&(initialLayer.layerData), initialLayer.width,
                             initialLayer.height, 255, 255, 255, 255);
  IM_ASSERT(ret);
  initialLayers.push_back(initialLayer);

  history.push(deepCopyLayers(initialLayers));

  std::vector<Layer> layers = initialLayers;

  bool show_demo_window = true;
  bool show_another_window = false;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  struct ProgramState state;

  state.brushState.radius = 10;

  int viewOffsetX = 0;
  int viewOffsetY = 30;

  bool inpaintPromptMode = false;
  bool inpaintDrawMode = false;

  int scale_factor = 100;

  int my_image_width = 512;
  int my_image_height = 512;
  GLuint my_image_texture = 0;
  ret = GenerateRandomTexture(
      &my_image_texture, my_image_width,
      my_image_height); // LoadTextureFromFile("./image.jpg", &my_image_texture,
                        // &my_image_width, &my_image_height);
  IM_ASSERT(ret);

  GLuint inpaintOverlay = 0;
  GLuint selectionOverlay = 0;

  int prev_x_offset = -1;
  int prev_y_offset = -1;

  ImGui::FileBrowser filePicker;

  // Main loop
#ifdef __EMSCRIPTEN__
  // For an Emscripten build we are disabling file-system access, so let's not
  // attempt to do a fopen() of the imgui.ini file. You may manually call
  // LoadIniSettingsFromMemory() to load settings from your own storage.
  io.IniFilename = nullptr;
  EMSCRIPTEN_MAINLOOP_BEGIN
#else
  while (!glfwWindowShouldClose(window))
#endif
  {

    glfwPollEvents();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int topActiveIndex = getTopActiveLayerIndex(layers);

    // layers = history.top();
    bool historyNode = false;
    bool resetHistory = false;

    ActionType currentAction = ActionType::None;

    int x_offset;
    int y_offset;

    int i = 0;

    // for (Layer layer : layers)
    for (int i = 0; i < layers.size(); i++) {

      Layer layer = layers[i];

      if (!(layer.enabled)) {
        continue;
      }

      ImGui::SetNextWindowPos(ImVec2(0 + viewOffsetX, viewOffsetY));
      ImGui::SetNextWindowSize(
          ImVec2((scale_factor / 100) * layer.width + 40,
                 (scale_factor / 100) * layer.height + 40));

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus |
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoBackground;
      ;

      ImGui::Begin((std::string("Image Window") + std::to_string(i)).c_str(),
                   &show_another_window, flags);
      // i+=1; // Pass a pointer to our bool variable (the window will have a
      // closing button that will clear the bool when clicked)
      ImGui::Image((void *)(intptr_t)layer.layerData,
                   ImVec2((scale_factor / 100) * layer.width,
                          (scale_factor / 100) * layer.height));
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

      x_offset = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x) /
                 (scale_factor / 100.0);
      y_offset = (ImGui::GetMousePos().y - ImGui::GetItemRectMin().y) /
                 (scale_factor / 100.0);

      if (i == topActiveIndex) {
        if (!ImGui::GetIO().KeyCtrl &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            ImGui::IsWindowFocused() && x_offset < layer.width &&
            x_offset >= 0 && y_offset < layer.height && y_offset >= 0) {
          if (state.drawMode) {

            if (prev_x_offset != -1 && prev_y_offset != -1) {

              drawLine(layer.layerData, prev_x_offset, prev_y_offset, x_offset,
                       y_offset, layer.width, layer.height,
                       state.brushState.radius, state.brushState.RGBA[0] * 255,
                       state.brushState.RGBA[1] * 255,
                       state.brushState.RGBA[2] * 255,
                       state.brushState.RGBA[3] * 255);
            }

            drawCircle(
                layer.layerData, x_offset, y_offset, layer.width, layer.height,
                state.brushState.radius, state.brushState.RGBA[0] * 255,
                state.brushState.RGBA[1] * 255, state.brushState.RGBA[2] * 255,
                state.brushState.RGBA[3] * 255);

          } else {
            state.drawMode = true;
          }
          prev_x_offset = x_offset;
          prev_y_offset = y_offset;

        } else if (state.drawMode) {
          state.drawMode = false;
          historyNode = true;
        }

        if (ImGui::GetIO().KeyCtrl) {

          if (io.MouseWheel != 0) {

            int old_scale_factor = scale_factor;

            int windowX = ImGui::GetContentRegionAvail().x;

            int windowY = ImGui::GetContentRegionAvail().y;

            int oldXDist = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x);
            int oldYDist = (ImGui::GetMousePos().y - ImGui::GetItemRectMin().y);

            scale_factor += io.MouseWheel * 100;

            if (scale_factor < 100) {
              scale_factor = 100;
            }

            float ratio = (float)scale_factor / (float)old_scale_factor;

            int newXDist = ratio * oldXDist;
            int newYDist = ratio * oldYDist;

            viewOffsetX = (ImGui::GetMousePos().x - newXDist);
            viewOffsetY = (ImGui::GetMousePos().y - newYDist);
          }
        }
      }

      ImGui::End();
    }

    {

      ImGui::BeginMainMenuBar();
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

      if (ImGui::BeginMenu("File")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        if (ImGui::MenuItem("Save")) {
          currentAction = ActionType::Save;
        }
        if (ImGui::MenuItem("Load")) {
          currentAction = ActionType::Load;
        }
        if (ImGui::MenuItem("Import")) {
          currentAction = ActionType::Import;
        }
        if (ImGui::MenuItem("Export")) {
          currentAction = ActionType::Export;
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Edit")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        if (ImGui::MenuItem("Undo")) {
          currentAction = ActionType::Undo;
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Brush")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        if (ImGui::MenuItem("Brush Settings")) {
          currentAction = ActionType::BrushSettings;
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Layer")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        if (ImGui::MenuItem("Add Layer")) {
          currentAction = ActionType::AddLayer;
        }
        if (ImGui::MenuItem("Remove Layer")) {
          currentAction = ActionType::RemoveLayer;
        }
        if (ImGui::MenuItem("Resize Layer")) {
          currentAction = ActionType::ResizeLayer;
        }
        if (ImGui::MenuItem("Merge Active Layers")) {
          currentAction = ActionType::MergeActiveLayers;
        }

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Generate")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        if (ImGui::MenuItem("Generate")) {
          currentAction = ActionType::Generate;
        }
        if (ImGui::MenuItem("Generation Settings")) {
          currentAction = ActionType::GenerationSettings;
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Inpaint")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        if (ImGui::MenuItem("Inpaint")) {
          currentAction = ActionType::Inpaint;
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Selection")) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        if (ImGui::MenuItem("Box Select")) {
          currentAction = ActionType::BoxSelect;
        }
        ImGui::EndMenu();
      }

        ImGui::EndMainMenuBar();
    }
    if (currentAction == ActionType::Import) {
      currentFilePickerAction = FilePickerActionType::Import;

      ImGuiFileBrowserFlags filePickerFlags = 0;
      filePicker = ImGui::FileBrowser(filePickerFlags);
      filePicker.SetTitle("Choose an image to load from file.");
      filePicker.SetTypeFilters({".jpg", ".jpeg", ".png"});
      filePicker.Open();
    } else if (currentAction == ActionType::Save) {
      currentFilePickerAction = FilePickerActionType::Save;
      ImGuiFileBrowserFlags filePickerFlags =
          ImGuiFileBrowserFlags_EnterNewFilename;
      filePicker = ImGui::FileBrowser(filePickerFlags);
      filePicker.SetTitle("Choose a save location.");
      filePicker.SetTypeFilters({".slop"});
      filePicker.Open();
    } else if (currentAction == ActionType::Load) {
      currentFilePickerAction = FilePickerActionType::Load;
      ImGuiFileBrowserFlags filePickerFlags = 0;
      filePicker = ImGui::FileBrowser(filePickerFlags);
      filePicker.SetTitle("Choose a slop file to load.");
      filePicker.SetTypeFilters({".slop"});
      filePicker.Open();
    } else if (currentAction == ActionType::Export) {
      currentFilePickerAction = FilePickerActionType::Export;
      ImGuiFileBrowserFlags filePickerFlags =
          ImGuiFileBrowserFlags_EnterNewFilename;
      filePicker = ImGui::FileBrowser(filePickerFlags);
      filePicker.SetTitle("Choose an Export location.");
      filePicker.SetTypeFilters({".png"});
      filePicker.Open();
    } else if (currentAction == ActionType::Generate) {
      prompt_popup_open = true;
    } else if (currentAction == ActionType::Inpaint) {
      state.inpaintMode = true;
      ret = GenerateEmptyTexture(&inpaintOverlay, layers[topActiveIndex].width,
                                 layers[topActiveIndex].height);
      IM_ASSERT(ret);
    } else if (currentAction == ActionType::BrushSettings) {
      state.brushSettingsOpen = true;
    } else if (currentAction == ActionType::GenerationSettings) {
      json tempEnvironmentJson = load_settings();
      state.tempEnvironmentState.stable_diffusion_path_set =
          tempEnvironmentJson["stable_diffusion_path_set"];
      state.tempEnvironmentState.stable_diffusion_path =
          tempEnvironmentJson["stable_diffusion_path"];
      state.generationSettingsOpen = true;
    } else if (currentAction == ActionType::BoxSelect) {
      ret =
          GenerateEmptyTexture(&selectionOverlay, layers[topActiveIndex].width,
                               layers[topActiveIndex].height);
      IM_ASSERT(ret);
      state.selectionMode = true;
    } else if (currentAction == ActionType::ResizeLayer) {
      state.layerResizeState.targetWidth = layers[topActiveIndex].width;
      state.layerResizeState.targetHeight = layers[topActiveIndex].height;
      state.resizeLayerDialogOpen = true;
    } else if (currentAction == ActionType::MergeActiveLayers) {
      if (!checkContiguousSelection(layers)) {
        state.warningDialogOpen = true;
        state.warningMessage = "Active Layers must be contiguous; there must "
                               "be 2 or more selected layers.";
      } else {
        FlattenedLayerData result = getFlattenedLayerData(layers);
        unsigned char *data = result.data;
        std::vector<int> toRemove;

        for (int i = 0; i < layers.size(); i++) {
          if (layers[i].enabled) {
            toRemove.push_back(i);
          }
        }
        toRemove.pop_back();

        removeLayers(layers, toRemove);

        GLuint flat_texture_id;
        glGenTextures(1, &flat_texture_id);
        glBindTexture(GL_TEXTURE_2D, flat_texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result.width, result.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
        delete[] data;

        layers[toRemove[0]].layerData = flat_texture_id;
        layers[toRemove[0]].width = result.width;
        layers[toRemove[0]].height = result.height;

        topActiveIndex = getTopActiveLayerIndex(layers);

        historyNode = true;
      }
    }

    if (ShowGenerateTextInputPopup(&state, &(layers[topActiveIndex].layerData),
                                   &(layers[topActiveIndex].width),
                                   &(layers[topActiveIndex].height),
                                   &prompt_popup_open)) {
      historyNode = true;
    }

    showBrushSettingsPopup(&state);
    showGenerationSettingsPopup(&state);
    showLayerResizePopup(&state, &(layers[topActiveIndex]));
    showWarningPopup(&state);

    int window_width;
    int window_height;
    glfwGetWindowSize(window, &window_width, &window_height);

    if (showLayerInfo(layers, window_width, window_height)) {
      historyNode = true;
    }

    filePicker.Display();

    if (filePicker.HasSelected()) {

      if (currentFilePickerAction == FilePickerActionType::Import) {
        GLuint load_target = 0;
        int load_target_width = 0;
        int load_target_height = 0;

        LoadTextureFromFile(filePicker.GetSelected().string().c_str(),
                            &(load_target), &load_target_width,
                            &load_target_height);
        layers[topActiveIndex].layerData = load_target;
        layers[topActiveIndex].width = load_target_width;
        layers[topActiveIndex].height = load_target_height;

        filePicker.ClearSelected();
        // history.push(layers);
        historyNode = true;
      }

      if (currentFilePickerAction == FilePickerActionType::Save) {
        saveLayersToFile(layers, filePicker.GetSelected().string().c_str());
        filePicker.ClearSelected();
      }

      if (currentFilePickerAction == FilePickerActionType::Load) {
        loadLayersFromFile(layers, filePicker.GetSelected().string().c_str());

        filePicker.ClearSelected();
        historyNode = true;
        resetHistory = true;
      }

      if (currentFilePickerAction == FilePickerActionType::Export) {
        unsigned char *result = getFlattenedLayerData(layers).data;

        int maxWidth = 0;
        int maxHeight = 0;

        for (const auto &layer : layers) {
          if (layer.enabled) {
            maxWidth = max(maxWidth, layer.width);
            maxHeight = max(maxHeight, layer.height);
          }
        }
        save_png(filePicker.GetSelected().string().c_str(), result, maxWidth, maxHeight);
        filePicker.ClearSelected();
      }
    }

    if (currentAction == ActionType::AddLayer) {
      Layer newLayer;
      newLayer.width = 512;
      newLayer.height = 512;
      newLayer.layerData = 0;
      newLayer.enabled = true;
      bool ret = generateUniformTexture(&(newLayer.layerData), newLayer.width,
                                        newLayer.height, 0, 0, 0, 0);
      IM_ASSERT(ret);
      layers.push_back(newLayer);
      historyNode = true;
    }

    if (currentAction == ActionType::RemoveLayer) {
      if (layers.size() > 1 && topActiveIndex != -1) {
        layers.erase(layers.begin() + topActiveIndex);
        historyNode = true;
      }
    }

    if (state.inpaintMode) {
      ImGui::SetNextWindowPos(ImVec2(0 + viewOffsetX, viewOffsetY));
      ImGui::SetNextWindowSize(
          ImVec2((scale_factor / 100.0) * layers[topActiveIndex].width + 40,
                 (scale_factor / 100.0) * layers[topActiveIndex].height + 40));

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;

      ImGui::Begin("Inpaint Overlay", &(state.inpaintMode), flags);
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

      ImGui::Image(
          (void *)(intptr_t)inpaintOverlay,
          ImVec2((scale_factor / 100) * layers[topActiveIndex].width,
                 (scale_factor / 100) * layers[topActiveIndex].height));

      x_offset = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x) /
                 (scale_factor / 100.0);
      y_offset = (ImGui::GetMousePos().y - ImGui::GetItemRectMin().y) /
                 (scale_factor / 100.0);

      if (!ImGui::GetIO().KeyCtrl &&
          ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
          ImGui::IsWindowFocused() && x_offset < layers[topActiveIndex].width &&
          x_offset >= 0 && y_offset < layers[topActiveIndex].height &&
          y_offset >= 0) {
        if (inpaintDrawMode) {
          if (prev_x_offset != -1 && prev_y_offset != -1) {

            drawLine(inpaintOverlay, prev_x_offset, prev_y_offset, x_offset,
                     y_offset, layers[topActiveIndex].width,
                     layers[topActiveIndex].height, 10, 100, 100, 0, 100);
          }

          drawCircle(inpaintOverlay, x_offset, y_offset,
                     layers[topActiveIndex].width,
                     layers[topActiveIndex].height, 10, 100, 100, 0, 100);

        } else {
          inpaintDrawMode = true;
        }
        prev_x_offset = x_offset;
        prev_y_offset = y_offset;

      } else if (inpaintDrawMode) {
        inpaintDrawMode = false;
      }
      if (ImGui::Button("OK")) {

        inpaintPromptMode = true;
        historyNode = true;
      }
      if (ShowInpaintTextInputPopup(layers[topActiveIndex], inpaintOverlay,
                                    &inpaintPromptMode)) {
        state.inpaintMode = false;
        historyNode = true;
      }

      ImGui::End();
    }

    if (state.selectionMode) {

      if (state.selectionState.completeSelection &&
          !(state.selectionState.dragging)) {

        ImGui::SetNextWindowPos(ImVec2(
            0 + viewOffsetX +
                state.selectionState.corner1[0] * (scale_factor / 100.0) +
                state.selectionState.selectionXOffset *
                    ((float)scale_factor / state.selectionState.selectionZoom),
            viewOffsetY +
                state.selectionState.corner1[1] * (scale_factor / 100.0) +
                state.selectionState.selectionYOffset *
                    ((float)scale_factor /
                     state.selectionState.selectionZoom)));
        ImGui::SetNextWindowSize(
            ImVec2((scale_factor / 100.0) * (state.selectionState.corner2[0] -
                                             state.selectionState.corner1[0]) +
                       40,
                   (scale_factor / 100.0) * (state.selectionState.corner2[1] -
                                             state.selectionState.corner1[1]) +
                       40));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;

        ImGui::Begin("Selection Content Overlay", &(state.selectionMode),
                     flags);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        ImGui::Image(
            (void *)(intptr_t)state.selectionState.selection,
            ImVec2(ImVec2(
                (scale_factor / 100.0) * (state.selectionState.corner2[0] -
                                          state.selectionState.corner1[0]),
                (scale_factor / 100.0) * (state.selectionState.corner2[1] -
                                          state.selectionState.corner1[1]))));

        ImGui::End();
      }

      ImGui::SetNextWindowPos(ImVec2(
          0 + viewOffsetX +
              state.selectionState.selectionXOffset *
                  ((float)scale_factor / state.selectionState.selectionZoom),
          viewOffsetY +
              state.selectionState.selectionYOffset *
                  ((float)scale_factor / state.selectionState.selectionZoom)));
      ImGui::SetNextWindowSize(
          ImVec2((scale_factor / 100.0) * layers[topActiveIndex].width + 40,
                 (scale_factor / 100.0) * layers[topActiveIndex].height + 40));

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;

      ImGui::Begin("Selection Overlay", &(state.selectionMode), flags);
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

      ImGui::Image(
          (void *)(intptr_t)selectionOverlay,
          ImVec2((scale_factor / 100) * layers[topActiveIndex].width,
                 (scale_factor / 100) * layers[topActiveIndex].height));

      x_offset = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x) /
                 (scale_factor / 100.0);
      y_offset = (ImGui::GetMousePos().y - ImGui::GetItemRectMin().y) /
                 (scale_factor / 100.0);

      if (!(state.selectionState.dragging) &&
          !state.selectionState.completeSelection &&
          !ImGui::GetIO().KeyCtrl &&
          ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
          ImGui::IsWindowFocused() && x_offset < layers[topActiveIndex].width &&
          x_offset >= 0 && y_offset < layers[topActiveIndex].height &&
          y_offset >= 0) {

        state.selectionState.dragging = true;
        state.selectionState.corner1[0] = x_offset;
        state.selectionState.corner1[1] = y_offset;

      } else if (state.selectionState.dragging &&
                 ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                 ImGui::IsWindowFocused()) {

        state.selectionState.corner2[0] =
            min(x_offset, layers[topActiveIndex].width);
        state.selectionState.corner2[1] =
            min(y_offset, layers[topActiveIndex].height);

        state.selectionState.completeSelection = true;

        drawSelectionBox(
            selectionOverlay, state.selectionState.corner1[0],
            state.selectionState.corner1[1], state.selectionState.corner2[0],
            state.selectionState.corner2[1], layers[topActiveIndex].width,
            layers[topActiveIndex].height, 1, 0, 0, 0, 255, false);

      } else if (!(ImGui::IsMouseDown(ImGuiMouseButton_Left)) &&
                 ImGui::IsWindowFocused() && state.selectionState.dragging) {

        int tempX = state.selectionState.corner1[0];
        int tempY = state.selectionState.corner1[1];

        state.selectionState.corner1[0] =
            min(state.selectionState.corner2[0], tempX);
        state.selectionState.corner1[1] =
            min(state.selectionState.corner2[1], tempY);

        state.selectionState.corner2[0] =
            max(state.selectionState.corner2[0], tempX);
        state.selectionState.corner2[1] =
            max(state.selectionState.corner2[1], tempY);

        copyTextureSubset(
            &(layers[topActiveIndex].layerData),
            &(state.selectionState.selection), layers[topActiveIndex].width,
            layers[topActiveIndex].height, state.selectionState.corner1[0],
            state.selectionState.corner1[1], state.selectionState.corner2[0],
            state.selectionState.corner2[1]);
        drawBox(
            layers[topActiveIndex].layerData, state.selectionState.corner1[0],
            state.selectionState.corner1[1], state.selectionState.corner2[0],
            state.selectionState.corner2[1], layers[topActiveIndex].width,
            layers[topActiveIndex].height, 1, 0, 0, 0, 0, true);

        state.selectionState.dragging = false;
      }
      // begin drag
      else if (((ImGui::IsMouseDown(ImGuiMouseButton_Left)) &&
                ImGui::IsWindowFocused() && !(state.selectionState.dragging) &&
                !state.selectionState.selectionDragMode &&
                state.selectionState.completeSelection) &&
               x_offset >= state.selectionState.corner1[0] &&
               x_offset <= state.selectionState.corner2[0] &&
               y_offset >= state.selectionState.corner1[1] &&
               y_offset <= state.selectionState.corner2[1]) {
        state.selectionState.selectionDragMode = true;
        state.selectionState.initialDragX =
            ImGui::GetMousePos().x - state.selectionState.selectionXOffset;
        state.selectionState.initialDragY =
            ImGui::GetMousePos().y - state.selectionState.selectionYOffset;
      }
      // continue drag
      else if ((!ImGui::GetIO().KeyCtrl) &&
               ((ImGui::IsMouseDown(ImGuiMouseButton_Left)) &&
                ImGui::IsWindowFocused() &&
                state.selectionState.selectionDragMode)) {
        state.selectionState.selectionXOffset =
            (ImGui::GetMousePos().x - state.selectionState.initialDragX);
        state.selectionState.selectionYOffset =
            (ImGui::GetMousePos().y - state.selectionState.initialDragY);
        state.selectionState.selectionZoom = scale_factor;
      } else if (state.selectionState.selectionDragMode &&
                 !((ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
        state.selectionState.selectionDragMode = false;
      } else if (((ImGui::IsMouseDown(ImGuiMouseButton_Left))) &&
                 !(x_offset >= state.selectionState.corner1[0] &&
                   x_offset <= state.selectionState.corner2[0] &&
                   y_offset >= state.selectionState.corner1[1] &&
                   y_offset <= state.selectionState.corner2[1])) {
        copyTextureToRegion(
            &(state.selectionState.selection),
            &(layers[topActiveIndex].layerData),
            state.selectionState.corner2[0] - state.selectionState.corner1[0],
            state.selectionState.corner2[1] - state.selectionState.corner1[1],
            layers[topActiveIndex].width, layers[topActiveIndex].height,
            state.selectionState.selectionXOffset / (scale_factor / 100.0) +
                state.selectionState.corner1[0],
            state.selectionState.selectionYOffset / (scale_factor / 100.0) +
                state.selectionState.corner1[1],
            false);
        state.selectionState.dragging = false;
        state.selectionState.completeSelection = false;
        state.selectionState.selectionDragMode = false;
        state.selectionMode = false;
        state.selectionState.selectionXOffset = 0;
        state.selectionState.selectionYOffset = 0;
        historyNode = true;
      }
      ImGui::End();
    }

    // Rendering
    ImGui::Render();

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)) &&
        ImGui::GetIO().KeyCtrl ||
        currentAction == ActionType::Undo) {
      if (history.size() > 1) {

        for (int i = 0; i < history.top().size(); i++) {
          freeLayer(&(history.top()[i]));
        }

        history.pop();
        layers = deepCopyLayers(history.top());
      } else if (history.size() == 1) {
        layers = deepCopyLayers(history.top());
      }
    }

    if (!state.dragMode && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        ImGui::GetIO().KeyCtrl) {
      state.dragMode = true;
      state.dragInitX = ImGui::GetMousePos().x;
      state.dragInitY = ImGui::GetMousePos().y;

      state.oldViewOffsetX = viewOffsetX;
      state.oldViewOffsetY = viewOffsetY;

    } else if (state.dragMode && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        ImGui::GetIO().KeyCtrl) {
      viewOffsetX =
          state.oldViewOffsetX + ImGui::GetMousePos().x - state.dragInitX;
      viewOffsetY =
          state.oldViewOffsetY + ImGui::GetMousePos().y - state.dragInitY;

    } else if (state.dragMode) {
      state.dragMode = false;
    }

    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);

    if (historyNode) {
      if (resetHistory) {
        while (!history.empty()) {
          history.pop();
        }
      }
      history.push(deepCopyLayers(layers));
    }
  }
#ifdef __EMSCRIPTEN__
  EMSCRIPTEN_MAINLOOP_END;
#endif

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
