/**
 * Copyright FunASR (https://github.com/alibaba-damo-academy/FunASR). All Rights
 * Reserved. MIT License  (https://opensource.org/licenses/MIT)
 */
/* 2022-2023 by zhaomingwork */
#ifdef ASR_SERVER_DLL_EXPORTS
#define ASR_SERVER_DLL_EXPORTS __declspec(dllexport)  // 编译 DLL 时导出
#else
#define ASR_SERVER_DLL_EXPORTS __declspec(dllimport)  // 使用 DLL 时导入
#endif

#include "websocket-server-2pass.h"
#ifdef _WIN32
#include "win_func.h"
#include <windows.h>
#include <stringapiset.h>
#include <filesystem>
#include <thread>
#include <functional>
#else
#include <unistd.h>
#endif
#include <fstream>
#include "util.h"




// hotwords
std::unordered_map<std::string, int> hws_map_;
int fst_inc_wts_=20;
float global_beam_, lattice_beam_, am_scale_;

using namespace std;

//static std::thread decodeThread;
static asio::io_context io_decoder;  // context for decoding
static asio::io_context io_server;   // context for server
static bool isRunning = false;




#ifdef __cplusplus
extern "C" {




#endif

    typedef void (*AsrInitResultFunctionPointer)(int success);

extern __declspec(dllexport) void  swAsrServerStart(
    AsrInitResultFunctionPointer cp,
    const char* _model_dir_str, const char* _online_model_dir_str,
    const char* _quantize_str, const char* _vad_dir_str,
    const char* _vad_quant_str, const char* _punc_dir_str,
    const char* _punc_quant_str, int port
) {

      SetConsoleOutputCP(65001);
  

      std::string model_dir_str;//(std::filesystem::u8path(_model_dir_str).generic_u8string());
      std::string online_model_dir_str;//(std::filesystem::u8path(_online_model_dir_str).generic_u8string());
      std::string quantize_str;//(std::filesystem::u8path(_quantize_str).generic_u8string());
      std::string vad_dir_str;//(std::filesystem::u8path(_vad_dir_str).generic_u8string());
      std::string vad_quant_str;//(std::filesystem::u8path(_vad_quant_str).generic_u8string());
      std::string punc_dir_str;//(std::filesystem::u8path(_punc_dir_str).generic_u8string());
      std::string punc_quant_str;//(std::filesystem::u8path(_punc_quant_str).generic_u8string());




      //std::string model_dir_str, online_model_dir_str, quantize_str, vad_dir_str,
      //    vad_quant_str, punc_dir_str, punc_quant_str, port_id_str;

      // 在Windows上转换UTF-8路径为本地编码，以便底层库正确处理
      auto convertUtf8ToLocal = [](const char* utf8_str) -> std::string {

          try {
              // 先转换为宽字符
              int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
              if (wlen == 0) {
                  //spdlog::error("Failed to convert UTF-8 to wide char: {}", utf8_str);
                  return utf8_str; // 回退到原始字符串
              }
              std::wstring wstr(wlen, 0);
              MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, &wstr[0], wlen);

              // 再转换为本地ANSI编码（CP_ACP）
              int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
              if (len == 0) {
                 // spdlog::error("Failed to convert wide char to ANSI: {}", utf8_str);
                  return utf8_str; // 回退到原始字符串
              }
              std::string result(len, 0);
              WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &result[0], len, NULL, NULL);

              // 移除末尾的null字符
              if (!result.empty() && result.back() == '\0') {
                  result.pop_back();
              }

              return result;
          }
          catch (const std::exception& e) {
              //spdlog::error("Exception during path conversion: {}", e.what());
              return utf8_str; // 回退到原始字符串
          }

          };

      try {
          model_dir_str = convertUtf8ToLocal(_model_dir_str);
         // spdlog::debug("Converted model_dir_str: '{}'", model_dir_str);

          online_model_dir_str = convertUtf8ToLocal(_online_model_dir_str);
          //spdlog::debug("Converted online_model_dir_str: '{}'", online_model_dir_str);

          quantize_str = _quantize_str;
          //spdlog::debug("Using quantize_str: '{}'", quantize_str);

          vad_dir_str = convertUtf8ToLocal(_vad_dir_str);
          //spdlog::debug("Converted vad_dir_str: '{}'", vad_dir_str);

          vad_quant_str = _vad_quant_str;
          //spdlog::debug("Using vad_quant_str: '{}'", vad_quant_str);

          punc_dir_str = convertUtf8ToLocal(_punc_dir_str);
          //spdlog::debug("Converted punc_dir_str: '{}'", punc_dir_str);

          punc_quant_str = _punc_quant_str;
          //spdlog::debug("Using punc_quant_str: '{}'", punc_quant_str);

         // port_id_str = _port_id_str;
          //spdlog::debug("Port ID: '{}'", port_id_str);
      }
      catch (const std::exception& e) {
          //spdlog::error("Failed to convert parameters to UTF-8: {}", e.what());
          if (cp != nullptr) {
              cp(0);
          }
          return;
      }

      //spdlog::info("Parameter processing completed successfully");

      // 记录转换后的路径用于调试
     // spdlog::info("Converted paths for ASR server:");
      //spdlog::info("  - Model dir: '{}'", model_dir_str);
      //spdlog::info("  - Online model dir: '{}'", online_model_dir_str);
      //spdlog::info("  - VAD dir: '{}'", vad_dir_str);
      //spdlog::info("  - Punctuation dir: '{}'", punc_dir_str);

      // 检查文件是否存在
     // spdlog::info("Validating model paths...");

      bool pathsValid = true;

      // 在Windows上使用UTF-8到宽字符的转换来正确处理中文路径
      auto checkPathExists = [&](const std::string& path_str, const std::string& name) -> bool {
          try {
#ifdef _WIN32
              // 将UTF-8字符串转换为宽字符
              int wlen = MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), -1, NULL, 0);
              if (wlen == 0) {
                 // spdlog::error("Failed to convert {} path to wide char", name);
                  return false;
              }
              std::wstring wpath(wlen, 0);
              MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), -1, &wpath[0], wlen);

              // 使用宽字符路径检查存在性
              std::filesystem::path fs_path(wpath);
              bool exists = std::filesystem::exists(fs_path);
             // spdlog::info("{} path '{}' exists: {}", name, path_str, exists);
              if (!exists) {
             //     spdlog::error("CRITICAL: {} path does not exist: {}", name, path_str);
              }
              return exists;
#else
              std::filesystem::path fs_path(path_str);
              bool exists = std::filesystem::exists(fs_path);
              spdlog::info("{} path '{}' exists: {}", name, path_str, exists);
              if (!exists) {
                  spdlog::error("CRITICAL: {} path does not exist: {}", name, path_str);
              }
              return exists;
#endif
          }
          catch (const std::exception& e) {
            //  spdlog::error("Exception checking {} path '{}': {}", name, path_str, e.what());
              return false;
          }
          };

      // 检查所有路径（使用原始UTF-8路径进行验证，因为我们的验证函数已经处理了编码）
      pathsValid &= checkPathExists(_model_dir_str, "Model");
      pathsValid &= checkPathExists(_online_model_dir_str, "Online model");
      pathsValid &= checkPathExists(_vad_dir_str, "VAD");
      pathsValid &= checkPathExists(_punc_dir_str, "Punctuation");

      // 验证端口号
      try {
         // int port = std::stoi(port_id_str);
          if (port < 1 || port > 65535) {
             // spdlog::error("INVALID PORT: Port {} is out of valid range (1-65535)", port);
              pathsValid = false;
          }
          else {
            //  spdlog::info("Port validation passed: {}", port);
          }
      }
      catch (const std::exception& e) {
        //  spdlog::error("INVALID PORT: Failed to parse port '{}': {}", port_id_str, e.what());
          pathsValid = false;
      }

      if (!pathsValid) {
         // spdlog::error("Path/Port validation failed - aborting ASR server startup");
          if (cp != nullptr) {
              cp(0);
          }
          return;
      }

     // spdlog::info("Starting ASR Server with validated parameters...");



  
      try {
        google::InitGoogleLogging("FunAsrWebSocketServer");
        FLAGS_logtostderr = true;
        std::string tpass_version = "0.1.0";

    


    std::map<std::string, std::string> model_path;



    model_path[MODEL_DIR] = model_dir_str;
    model_path[ONLINE_MODEL_DIR] = online_model_dir_str;
    model_path[QUANTIZE] = quantize_str;
    model_path[VAD_DIR] = vad_dir_str;
    model_path[VAD_QUANT] = vad_quant_str;
    model_path[PUNC_DIR] = punc_dir_str;
    model_path[PUNC_QUANT] = punc_quant_str;






    std::string s_listen_ip = "0.0.0.0";
    int s_port = port;
    int s_io_thread_num = 1;
    int s_decoder_thread_num = 1;

    int s_model_thread_num = 1;

    //asio::io_context io_decoder;  // context for decoding
    //asio::io_context io_server;   // context for server

    //std::vector<std::thread> decoder_threads;

    std::string s_certfile = "";
    std::string s_keyfile = "";

    // hotword file
    //std::string hotword_path;
    //hotword_path = model_path.at(HOTWORD);
    //fst_inc_wts_ = fst_inc_wts.getValue();
    //LOG(INFO) << "hotword path: " << hotword_path;
    //funasr::ExtractHws(hotword_path, hws_map_);

    bool is_ssl = false;
    if (!s_certfile.empty() && access(s_certfile.c_str(), F_OK) == 0) {
      is_ssl = true;
    }

    auto conn_guard = asio::make_work_guard(
        io_decoder);  // make sure threads can wait in the queue
    auto server_guard = asio::make_work_guard(
        io_server);  // make sure threads can wait in the queue
    // create threads pool
    //for (int32_t i = 0; i < s_decoder_thread_num; ++i) {
    //  decoder_threads.emplace_back([&io_decoder]() { io_decoder.run(); });
    //}

    server server_;  // server for websocket
    wss_server wss_server_;
    server* server = nullptr;
    wss_server* wss_server = nullptr;
    if (is_ssl) {
      LOG(INFO)<< "SSL is opened!";
      wss_server_.init_asio(&io_server);  // init asio
      wss_server_.set_reuse_addr(
          true);  // reuse address as we create multiple threads

      // list on port for accept
      wss_server_.listen(asio::ip::address::from_string(s_listen_ip), s_port);
      wss_server = &wss_server_;

    } else {
      LOG(INFO)<< "SSL is closed!";
      server_.init_asio(&io_server);  // init asio
      server_.set_reuse_addr(
          true);  // reuse address as we create multiple threads

      // list on port for accept
      server_.listen(asio::ip::address::from_string(s_listen_ip), s_port);
      server = &server_;

    }

    WebSocketServer websocket_srv(
        io_decoder, is_ssl, server, wss_server, s_certfile,
        s_keyfile);  // websocket server for asr engine
    websocket_srv.initAsr(model_path, s_model_thread_num);  // init asr model


   

    LOG(INFO) << "decoder-thread-num: " << s_decoder_thread_num;
    LOG(INFO) << "io-thread-num: " << s_io_thread_num;
    LOG(INFO) << "model-thread-num: " << s_model_thread_num;
    LOG(INFO) << "asr model init finished. listen on port:" << s_port;

    if (cp != nullptr)
    {
        cp(1);
    }
    isRunning = true;

    std::vector<std::thread> decoder_threads;

    for (int32_t i = 0; i < s_decoder_thread_num; ++i) {
        decoder_threads.emplace_back([]() { io_decoder.run(); });
    }

    // Start the ASIO network io_service run loop
    std::vector<std::thread> ts;
    // create threads for io network
    for (size_t i = 0; i < s_io_thread_num; i++) {
        ts.emplace_back([]() { io_server.run(); });
    }
    // wait for theads
    for (size_t i = 0; i < s_io_thread_num; i++) {
        ts[i].join();
    }

    // wait for theads
    for (auto& t : decoder_threads) {
        t.join();
    }


  } catch (std::exception const& e) {
    LOG(ERROR) << "Error: " << e.what();
    if (cp != NULL)
    {
        cp(0);
    }
  }



}

extern __declspec(dllexport) void  swAsrServerStop(
) {
    if (!io_decoder.stopped()) {

        io_decoder.stop();
    }
    if (!io_server.stopped()) {

        io_server.stop();
    }

    isRunning = false;
}


extern __declspec(dllexport) int swAsrIsServerRunning() {
   // bool isRunning = (!io_decoder.stopped()) && (!io_decoder.stopped());

   //// spdlog::debug("swAsrIsServerRunning check: {}", isRunning);
    return isRunning ? 1 : 0;
}

#ifdef __cplusplus
}
#endif



int main(int argc, char* argv[]) {

    std::string  base = "C:/Users/13910/AppData/Roaming/AIPC/funasr/";

    swAsrServerStart(nullptr,
        (base + std::string("speech_paraformer-large-vad-punc_asr_nat-zh-cn-16k-common-vocab8404-onnx")).c_str(),
        (base + std::string("speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online-onnx")).c_str(),
        "true",
        (base + std::string("speech_fsmn_vad_zh-cn-16k-common-onnx")).c_str(),
        "true",
        (base + std::string("punc_ct-transformer_zh-cn-common-vad_realtime-vocab272727-onnx")).c_str(),
        "true",
        9996
    );

}