#include "StyleImageCache.h"


namespace fs = std::filesystem;


namespace {
  cv::Mat cropToSquare(const cv::Mat& image) {
    int height = image.rows;
    int width = image.cols;

    // Determine the size of the square
    int minDim = height < width ? height : width;

    // Calculate the top-left corner of the square
    int x = (width - minDim) / 2;
    int y = (height - minDim) / 2;

    // Crop the image to the square
    cv::Rect square_roi(x, y, minDim, minDim);
    cv::Mat cropped_image = image(square_roi);

    return cropped_image;
  }



  ID3D11ShaderResourceView* MatToTexture(ID3D11Device* device, ID3D11DeviceContext* context, const cv::Mat& mat) {
    if (mat.empty())
      return nullptr;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = mat.cols;
    desc.Height = mat.rows;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    std::vector<unsigned char> imageData(mat.cols * mat.rows * 4);
    if (mat.channels() == 1) {
      for (int y = 0; y < mat.rows; ++y) {
        for (int x = 0; x < mat.cols; ++x) {
          unsigned char value = mat.at<unsigned char>(y, x);
          imageData[(y * mat.cols + x) * 4 + 0] = value;
          imageData[(y * mat.cols + x) * 4 + 1] = value;
          imageData[(y * mat.cols + x) * 4 + 2] = value;
          imageData[(y * mat.cols + x) * 4 + 3] = 255;
        }
      }
    }
    else if (mat.channels() == 3) {
      for (int y = 0; y < mat.rows; ++y) {
        for (int x = 0; x < mat.cols; ++x) {
          cv::Vec3b value = mat.at<cv::Vec3b>(y, x);
          imageData[(y * mat.cols + x) * 4 + 0] = value[0];
          imageData[(y * mat.cols + x) * 4 + 1] = value[1];
          imageData[(y * mat.cols + x) * 4 + 2] = value[2];
          imageData[(y * mat.cols + x) * 4 + 3] = 255;
        }
      }
    }
    else if (mat.channels() == 4) {
      for (int y = 0; y < mat.rows; ++y) {
        for (int x = 0; x < mat.cols; ++x) {
          cv::Vec4b value = mat.at<cv::Vec4b>(y, x);
          imageData[(y * mat.cols + x) * 4 + 0] = value[0];
          imageData[(y * mat.cols + x) * 4 + 1] = value[1];
          imageData[(y * mat.cols + x) * 4 + 2] = value[2];
          imageData[(y * mat.cols + x) * 4 + 3] = value[3];
        }
      }
    }

    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = imageData.data();
    subResource.SysMemPitch = mat.cols * 4;
    subResource.SysMemSlicePitch = 0;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &subResource, &texture);
    if (FAILED(hr)) {
      std::cout << "Failed to create texture from image data" << std::endl;
      return nullptr;
    }

    ID3D11ShaderResourceView* textureView = nullptr;
    hr = device->CreateShaderResourceView(texture, nullptr, &textureView);
    texture->Release();

    if (FAILED(hr)) {
      std::cout << "Failed to create shader resource view from texture" << std::endl;
      return nullptr;
    }

    return textureView;
  }
}



StyleImageCache::~StyleImageCache() {
  clear();
}



void StyleImageCache::load(const std::string& folder, ID3D11Device* device, ID3D11DeviceContext* context) {
  m_PathToFolder = folder;
  
  // Iterate through the files in the directory
  for (const auto& entry : fs::directory_iterator(folder)) {
    std::string filePath = entry.path().string();
    fs::file_time_type lastWrite = fs::last_write_time(filePath);

    // don't reload unchanged files
    auto it = m_Images.find(filePath);
    if (it != m_Images.end() && it->second.lastWrite == lastWrite) {
      continue;
    }

    cv::Mat img = cv::imread(filePath, cv::IMREAD_COLOR);
    if (img.empty()) {
      continue;
    }

    img = cropToSquare(img);

    // prep for Inference
    
    cv::Size sz(m_ImgSize.first, m_ImgSize.second);

    cv::Mat resizedImg;
    cv::resize(img, resizedImg, sz, cv::INTER_AREA);

    cv::Mat styleImg;
    resizedImg.convertTo(styleImg, CV_32FC3, 1.0 / 255.0);

    auto width = styleImg.size().width;
    auto height = styleImg.size().height;
    auto channels = 3;

    auto blob = std::vector<float>(height * width * channels);

    for (int h = 0; h < height; ++h) {
      for (int w = 0; w < width; ++w) {
        for (int c = 0; c < channels; ++c) {
          blob[h * width * channels + w * channels + c] = styleImg.at<cv::Vec3f>(h, w)[c];
        }
      }
    }


    // prep thumbnail
    
    auto* thumbnail = MatToTexture(device, context, resizedImg);


    // done

    m_Images.emplace(filePath, StyleImage{ filePath, lastWrite, styleImg, blob, thumbnail });
  }

  auto it = m_Images.find(m_ActiveImage);
  if (it == m_Images.end()) {
    m_ActiveImage = "";
  }

  if (m_ActiveImage.empty() && m_Images.size() > 0) {
    m_ActiveImage = m_Images.begin()->first;
  }
}



const StyleImageCache::StyleImage* StyleImageCache::getActiveImage() const {
  auto it = m_Images.find(m_ActiveImage);

  if (it == m_Images.end()) {
    return {};
  }
  
  return &(it->second);
}



void StyleImageCache::setActiveImage(std::string path) {
  auto it = m_Images.find(path);

  if (it != m_Images.end()) {
    m_ActiveImage = it->first;
  }
}


void StyleImageCache::clear() {
  for (const auto& [_, img] : m_Images) {
    if (img.m_Thumbnail) {
      img.m_Thumbnail->Release();
    }
  }
}