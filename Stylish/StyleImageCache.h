#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <opencv2/opencv.hpp>

#include <d3d11.h>


class StyleImageCache {
public:
  struct StyleImage {
    std::string path;
    std::filesystem::file_time_type lastWrite;

    cv::Mat m_Image;
    std::vector<float> m_Blob;

    ID3D11ShaderResourceView* m_Thumbnail;
  };

  StyleImageCache() = default;

  StyleImageCache(const StyleImageCache& other) = delete;
  StyleImageCache(StyleImageCache&& other) = delete;

  StyleImageCache& operator=(const StyleImageCache& other) = delete;
  StyleImageCache& operator=(StyleImageCache&& other) = delete;

  virtual ~StyleImageCache();

  void load(const std::string& folder, ID3D11Device* device, ID3D11DeviceContext* context);

  void clear();
  
  std::string getPathToStyleFolder() const { return m_PathToFolder; }

  std::pair<int, int> getImageSize() const { return m_ImgSize; };

  const std::unordered_map<std::string, StyleImage>& getImages() const { return m_Images; }

  const StyleImage* getActiveImage() const;

  void setActiveImage(std::string path);

private:

  std::pair<int, int> m_ImgSize = { 256, 256 };

  std::string m_PathToFolder;

  std::unordered_map<std::string, StyleImage> m_Images;
  
  std::string m_ActiveImage;
};