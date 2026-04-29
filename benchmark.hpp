#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <random>
#include <iostream>
#include <fins/node.hpp>

struct BenchmarkData {
  std::vector<uint8_t> data;

  BenchmarkData() = default;
  
  BenchmarkData(const BenchmarkData& other) : data(other.data) {
    std::cout << "[WARNING] BenchmarkData 发生了拷贝构造！数据大小: " << data.size() << " bytes" << std::endl;
  }

  BenchmarkData& operator=(const BenchmarkData& other) {
    if (this != &other) {
      data = other.data;
      std::cout << "[WARNING] BenchmarkData 发生了拷贝赋值！数据大小: " << data.size() << " bytes" << std::endl;
    }
    return *this;
  }

  BenchmarkData(BenchmarkData&&) = default;
  BenchmarkData& operator=(BenchmarkData&&) = default;
};

class BenchmarkSource : public fins::Node {
public:
  BenchmarkSource() = default;

  void define() override {
    set_name("BenchmarkSource");
    set_description("产生 Benchmark 消息");
    set_category("Benchmark");

    register_output<BenchmarkData>("data");
    register_parameter<int>("rate", &BenchmarkSource::set_rate, 1000);
    register_parameter<int>("kb", &BenchmarkSource::set_kb, 1);
  }

  void initialize() override {
    update_msg();
    logger->info("Source 初始化完成.");
    running_ = false;
  }

  void run() override {
    if (running_) return;
    running_ = true;

    worker_ = std::thread([this]() {
      logger->info("数据源线程已启动. 发送速率: {} Hz, 消息大小: {} KB", rate_, kb_);
      
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 255);

      auto msg_ptr = std::make_shared<BenchmarkData>();
      msg_ptr->data.resize(kb_ * 1024);

      while (running_) {
        size_t bytes_to_update = std::min<size_t>(msg_ptr->data.size(), 8);
        for (size_t i = 0; i < bytes_to_update; ++i) {
          msg_ptr->data[i] = static_cast<uint8_t>(dis(gen));
        }

        this->send_ptr("data", msg_ptr);
        std::this_thread::sleep_for(std::chrono::microseconds(1000000 / rate_));
      }
    });
  }

  void pause() override {
    running_ = false;
    if (worker_.joinable()) {
      worker_.join();
    }
    logger->info("Source 已暂停.");
  }

  void reset() override { logger->info("Source 重置."); }

  void set_rate(int rate) {
    if (rate <= 0) {
      logger->warn("无效的速率: {}. 速率必须为正整数.", rate);
      return;
    }
    rate_ = rate;
    logger->info("更新发送速率为 {} 消息/秒.", rate_);
  }

  void set_kb(int kb) {
    if (kb <= 0) {
      logger->warn("无效的消息大小: {}. 消息大小必须为正整数.", kb);
      return;
    }
    kb_ = kb;
    update_msg();
    logger->info("更新消息大小为 {} KB.", kb_);
  }

private:
  void update_msg() {
    msg_.data.resize(kb_ * 1024);
  }

  std::thread worker_;
  std::atomic<bool> running_{false};
  int rate_ = 1000;
  int kb_ = 1;
  BenchmarkData msg_;
};

EXPORT_NODE(BenchmarkSource)
class BenchmarkPrinter : public fins::Node {
public:
  BenchmarkPrinter() = default;

  void define() override {
    set_name("BenchmarkPrinter");
    set_description("统计收到的 Benchmark 消息");
    set_category("Benchmark");

    register_input<BenchmarkData>("data", &BenchmarkPrinter::receive_msg);
  }

  void initialize() override {
    reset_statistics();
    last_report_time_ = std::chrono::steady_clock::now();
    logger->info("Printer 初始化.");
  }

  void run() override { logger->info("Printer 进入运行状态."); }

  void pause() override { logger->info("Printer 进入暂停状态."); }

  void reset() override { 
    reset_statistics();
    logger->info("Printer 重置."); 
  }

  void receive_msg(const fins::Msg<BenchmarkData> &msg) {
    uint64_t latency = fins::latency_us(msg.acq_time);
    
    total_latency_ += latency;
    count_++;

    if (latency > max_latency_) {
      max_latency_ = latency;
    }
    if (latency < min_latency_) {
      min_latency_ = latency;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time_).count();

    if (elapsed >= 1) {
      if (count_ > 0) {
        double avg_latency = total_latency_ / static_cast<double>(count_);
        logger->info("统计结果 ({}s): 消息大小 {} bytes, 频率 {} Hz",
                     elapsed, msg->data.size(), count_ / elapsed);
        logger->info("时延统计: 平均 {} us, 最小 {} us, 最大 {} us",
                     avg_latency, min_latency_, max_latency_);
      }
      
      reset_statistics();
      last_report_time_ = now;
    }
  }

private:
  void reset_statistics() {
    count_ = 0;
    total_latency_ = 0;
    max_latency_ = 0;
    min_latency_ = std::numeric_limits<uint64_t>::max();
  }

  size_t count_ = 0;
  uint64_t total_latency_ = 0;
  uint64_t max_latency_ = 0;
  uint64_t min_latency_ = std::numeric_limits<uint64_t>::max();
  std::chrono::steady_clock::time_point last_report_time_;
};

EXPORT_NODE(BenchmarkPrinter)