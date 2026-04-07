#include "helper/CommandLineParser.h"
#include "Dram.h"
#include "Request.h"

namespace po = boost::program_options;
using namespace pimony;
int main(int argc, char **argv)
{
  // parse command line argumnet
  CommandLineParser cmd_parser = CommandLineParser();
  cmd_parser.add_command_line_option<std::string>("mem_config",
                                                  "Path for memory configuration file");
  cmd_parser.add_command_line_option<std::string>("model_config",
                                                  "Path for model configuration file");
  cmd_parser.add_command_line_option<std::string>("normal_trace",
                                                  "Path for normal memory access trace file");
  cmd_parser.add_command_line_option<std::string>("log_dir",
                                                  "Path for experiment result log directory");
  cmd_parser.add_command_line_option<std::string>(
      "log_level", "Set for log level [off, trace, debug, info], default = info");

  try
  {
    cmd_parser.parse(argc, argv);
  }
  catch (const CommandLineParser::ParsingError &e)
  {
    spdlog::error("Command line argument parrsing error captured. Error message: {}", e.what());
    throw(e);
  }

  std::string level = "info";
  cmd_parser.set_if_defined("log_level", &level);
  if (level == "trace")
    spdlog::set_level(spdlog::level::trace);
  else if (level == "debug")
    spdlog::set_level(spdlog::level::debug);
  else if (level == "info")
    spdlog::set_level(spdlog::level::info);
  // else if (level == "off")
  //   spdlog::set_level(spdlog::level::off);

  Config::global_config.log_level = level;

  std::string mem_config_path;
  cmd_parser.set_if_defined("mem_config", &mem_config_path);

  std::string model_config_path;
  cmd_parser.set_if_defined("model_config", &model_config_path);

  std::string normal_trace_path;
  cmd_parser.set_if_defined("normal_trace", &normal_trace_path);

  std::string log_dir_path;
  cmd_parser.set_if_defined("log_dir", &log_dir_path);

  initialize_memory_config(mem_config_path);
  initialize_model_config(model_config_path);

  Config::global_config.log_dir = log_dir_path;

  PIM *dram = new PIM(Config::global_config);

  Request::TraceRequestHandler *request_handler =
      new Request::TraceRequestHandler(Config::global_config, normal_trace_path);

  MemoryAccess *mem_request;
  MemoryAccess *mem_response;

  int channels = Config::global_config.dram_channels;
std::vector<bool> pim_done;
  pim_done.resize(channels, true);
  int num_bankgroups = Config::global_config.dram_bankgroups_per_ch;

  std::vector<std::vector<bool>> pim_done_bg;
  pim_done_bg.resize(channels);
  for (size_t ch = 0; ch < channels; ch++)
    pim_done_bg[ch].resize(num_bankgroups, true);
  cycle_type i = 0;

  while (!request_handler->is_pim_operation_done())
  {

    for (int ch = 0; ch < channels; ch++)
    {
      mem_request = request_handler->getNextAccess(ch, i, pim_done[ch], pim_done_bg[ch]);
      if (mem_request->request != false)
      {
        dram->push(ch, mem_request);
      }
      else
      {
        delete mem_request;
      }

      if (pim_done[ch])
      {
        pim_done[ch] = false;
      }

      for (size_t bg = 0; bg < num_bankgroups; bg++)
      {
        if (pim_done_bg[ch][bg])
        {
          pim_done_bg[ch][bg] = false;
        }
      }
    }

    dram->cycle();

    for (int ch = 0; ch < channels; ch++)
    {
      while (!dram->is_empty(ch))
      {
        mem_response = dram->top(ch);
        if (mem_response->req_type == MemoryAccessType::READ)
        {
          request_handler->update_latency(ch, i, true, false, false, mem_response->id);
        }
        else if (mem_response->req_type == MemoryAccessType::WRITE)
        {
          request_handler->update_latency(ch, i, false, true, false, mem_response->id);
        }
        else
        {
          if (mem_response->pim_last)
          {
            request_handler->update_latency(ch, i, false, false, true, mem_response->id);
            if (mem_response->bankgroup == -1)
              pim_done[ch] = true;
            else
            {
              pim_done_bg[ch][mem_response->bankgroup] = true;
            }
          }
        }
        delete mem_response;
        dram->pop(ch);
      }
    }
    i++;
  }

  request_handler->print_state();
  dram->print_stat();
  delete request_handler;
  delete mem_request;
  delete mem_response;
  delete dram;
  return 0;
}