#ifndef DEBUG_VIEWER_CLIENT
#define DEBUG_VIEWER_CLIENT

#include <cstdlib>
#include <cstdio>
#include <string>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <math.h>
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <string>

#include "csimplesocket/ActiveSocket.h"

namespace {

struct LayerIndex {
  uint32 index;
  std::string name;
  std::string to_bytes() const {
    const size_t name_size = name.size();
    return std::string((char*)this, (char*)this + 4) +
        std::string((char*)(&name_size), (char*)(&name_size) + 4) + name;
  }
};

struct Camera {
  double x;
  double y;
  double angle;
  std::string to_bytes() const {
    return std::string((char*)this, (char*)this + 24);
  }
};

struct Marker {
  double x;
  double y;
  uint32 scalable; // idk why uint8 dont work
  uint32 color;
  double width;
  std::string text;
  std::string layer;
  std::string to_bytes() const {
    const size_t text_size = text.size();
    const size_t layer_size = layer.size();
    return std::string((char*)this, (char*)this + 32) +
        std::string((char*)(&text_size), (char*)(&text_size) + 4) + text +
        std::string((char*)(&layer_size), (char*)(&layer_size) + 4) + layer;
  }
};

struct Line {
  double x1;
  double y1;
  double x2;
  double y2;
  double width;
  uint32 color;
  std::string layer;
  std::string to_bytes() const {
    const size_t layer_size = layer.size();
    return std::string((char*)this, (char*)this + 44) +
        std::string((char*)(&layer_size), (char*)(&layer_size) + 4) + layer;
  }
};

struct Message {
  uint32 color;
  uint32 width;
  std::string text;
  // todo send index, not string
  std::string layer;
  std::string to_bytes() const {
    const size_t text_size = text.size();
    const size_t layer_size = layer.size();
    return std::string((char*)this, (char*)this + 8) +
        std::string((char*)(&text_size), (char*)(&text_size) + 4) + text +
        std::string((char*)(&layer_size), (char*)(&layer_size) + 4) + layer;
  }
};

} // namespace


struct FrameData {
  size_t frame_id = 0;
  Camera camera;
  std::vector<LayerIndex> layer_indexes;
  std::vector<Line> lines;
  std::vector<Marker> markers;
  std::vector<Message> messages;

  std::string to_bytes() const {
    std::string message;
    message += std::string((char*)(&frame_id), (char*)(&frame_id) + 4);

    auto vector_to_bytes = [](const auto& objects) {
      std::string result;
      for (const auto& object : objects) {
        result += object.to_bytes();
      }
      return result;
    };

    message += camera.to_bytes();

    const std::string& layer_indexes_bytes = vector_to_bytes(layer_indexes);
    auto size = static_cast<uint32_t>(layer_indexes.size());
    message += std::string((char*)(&size), (char*)(&size) + 4);
    message += layer_indexes_bytes;

    const std::string& lines_bytes = vector_to_bytes(lines);
    size = static_cast<uint32_t>(lines.size());
    message += std::string((char*)(&size), (char*)(&size) + 4);
    message += lines_bytes;

    const std::string& markers_bytes = vector_to_bytes(markers);
    size = static_cast<uint32_t>(markers.size());
    message += std::string((char*)(&size), (char*)(&size) + 4);
    message += markers_bytes;

    // std::cerr << "messages size: " << messages.size() << std::endl;

    const std::string& messages_bytes = vector_to_bytes(messages);
    size = static_cast<uint32_t>(messages.size());
    message += std::string((char*)(&size), (char*)(&size) + 4);
    message += messages_bytes;

    return message;
  }
};

class DebugViewerClient {
 public:
  static DebugViewerClient& GetInstance() {
    static DebugViewerClient instance;
    return instance;
  }


  static void ThreadParser(
      std::mutex& parse_mutex,
      std::mutex& send_mutex,
      std::queue<FrameData>& parse_queue,
      std::queue<std::string>& send_queue,
      bool& destruct_parsing) {
    while (true) {
      parse_mutex.lock();
      if (destruct_parsing) {
        parse_mutex.unlock();
        break;
      }
      parse_mutex.unlock();

      while (true) {
        parse_mutex.lock();
        const auto empty = parse_queue.empty();
        parse_mutex.unlock();
        if (empty) {
          break;
        }
        parse_mutex.lock();
        // std::cerr << "Parsing queue: " << parse_queue.size() << std::endl;
        const auto frame_data = std::move(parse_queue.front());
        parse_queue.pop();
        parse_mutex.unlock();

        // auto start = std::chrono::high_resolution_clock::now();
        const auto message = frame_data.to_bytes();
        // auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        //    std::chrono::high_resolution_clock::now() - start).count();
        // std::cerr << "Parsing time: " << ms << std::endl;

        send_mutex.lock();
        send_queue.push(message);
        send_mutex.unlock();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  static void ThreadSender(
      std::mutex& send_mutex,
      std::queue<std::string>& send_queue,
      bool& destruct_sending,
      CActiveSocket& socket) {
    while (true) {
      send_mutex.lock();
      if (destruct_sending) {
        send_mutex.unlock();
        break;
      }
      send_mutex.unlock();


      while (true) {
        send_mutex.lock();
        const auto empty = send_queue.empty();
        send_mutex.unlock();
        if (empty) {
          break;
        }
        send_mutex.lock();
        // std::cerr << "Sending queue: " << send_queue.size() << std::endl;
        const auto message = std::move(send_queue.front());
        send_queue.pop();
        send_mutex.unlock();


        auto int_to_bytes = [](const size_t value) {
          std::string bytes = "0000";
          for (size_t i = 0; i < 4; ++i) {
            bytes[3 - i] = (value >> (i * 8));
          }
          return bytes;
        };

        // auto start = std::chrono::high_resolution_clock::now();

        socket.Send(reinterpret_cast<const uint8_t*>(int_to_bytes(message.size()).c_str()), 4);
        socket.Send(reinterpret_cast<const uint8_t*>(message.c_str()), message.size());

        // auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        //    std::chrono::high_resolution_clock::now() - start).count();
        // std::cerr << "Sending time: " << ms << std::endl;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }


  DebugViewerClient() {
    socket_.Initialize();
    socket_.DisableNagleAlgoritm();
    socket_.Open(reinterpret_cast<const uint8_t*>("localhost"), 8888);
    parser_ = std::thread(
        ThreadParser,
        std::ref(parse_mutex_),
        std::ref(send_mutex_),
        std::ref(parse_queue_),
        std::ref(send_queue_),
        std::ref(destruct_parsing_));
    sender_ = std::thread(
        ThreadSender,
        std::ref(send_mutex_),
        std::ref(send_queue_),
        std::ref(destruct_sending_),
        std::ref(socket_));
  }

  FrameData& GetFrameData() {
    static thread_local FrameData frame_data_;
    return frame_data_;
  }


  void SetPresicion(const int precision) {
    precision_ = precision;
  }

  template<typename... Args>
  void SendMessage(const std::string& layer, Args... args) {
    GetFrameData().messages.push_back({16, 0, my_to_string(args...), layer});
  }

  template<typename T, typename... Args>
  inline std::string my_to_string(T fmt, Args... args) {
    return my_to_string(fmt) + my_to_string(args...);
  }

  inline std::string my_to_string(std::string fmt) {
    return fmt;
  }

  inline std::string my_to_string(double fmt) {
    std::stringstream _s;
    _s << std::fixed << std::setprecision(precision_) << fmt;
    return _s.str();
  }

  inline std::string my_to_string(const char*& fmt) {
    return std::string(fmt);
  }

  template<typename T>
  inline std::string my_to_string(T fmt) {
    return std::to_string(fmt);
  }

  void SendCamera(const Camera& camera) {
    GetFrameData().camera = camera;
  }

  void SendLayerIndex(const LayerIndex& layer_name) {
    GetFrameData().layer_indexes.push_back(layer_name);
  }

  void SendLayerIndex(const std::string& name, const uint32 index) {
    SendLayerIndex(LayerIndex{index, name});
  }

  void DrawLine(const Line& line) {
    GetFrameData().lines.push_back(line);
  }

  void DrawMarker(const Marker& marker) {
    GetFrameData().markers.push_back(marker);
  }

  void DrawLine(
      const double x1,
      const double y1,
      const double x2,
      const double y2,
      const double width = 1,
      const uint32 color = 0,
      const std::string& layer = "undefined") {
    DrawLine({x1, y1, x2, y2, width, color, layer});
  }

  void EndFrame() {
    parse_mutex_.lock();
    GetFrameData().frame_id = frame_id_;
    parse_queue_.push(GetFrameData());
    // std::cerr << "EndFrame: " << frame_id_ << std::endl;
    ++frame_id_;
    parse_mutex_.unlock();
    GetFrameData().layer_indexes.clear();
    GetFrameData().lines.clear();
    GetFrameData().markers.clear();
    GetFrameData().messages.clear();
  }

  ~DebugViewerClient() {
    parse_mutex_.lock();
    destruct_parsing_ = true;
    parse_mutex_.unlock();
    parser_.join();
    send_mutex_.lock();
    destruct_sending_ = true;
    send_mutex_.unlock();
    sender_.join();
  }

 private:
  CActiveSocket socket_;

  uint32 frame_id_;

  std::thread parser_;
  std::queue<FrameData> parse_queue_;
  std::mutex parse_mutex_;
  bool destruct_parsing_ = false;

  std::thread sender_;
  std::queue<std::string> send_queue_;
  std::mutex send_mutex_;
  bool destruct_sending_ = false;

  int precision_ = 1; //todo all default values make static thread_local
};

#endif
