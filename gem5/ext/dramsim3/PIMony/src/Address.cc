#include "Common.h"
#include "Address.h"
namespace pimony
{
  namespace ADDRESS
  {

    Address::Address(std::string config_file)
    {
      std::ifstream file(config_file);
      std::string line;

      while (std::getline(file, line))
      {
        // Remove comments
        size_t comment_pos = line.find(';');
        if (comment_pos != std::string::npos)
          line = line.substr(0, comment_pos);

        // Trim whitespace
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

        // Skip empty lines and section headers
        if (line.empty() || line[0] == '[')
          continue;

        // Parse key = value
        size_t eq = line.find('=');
        if (eq == std::string::npos)
          continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "columns")
          columns = std::stoi(value);
        else if (key == "device_width")
          device_width = std::stoi(value);
        else if (key == "BL")
          BL = std::stoi(value);
        else if (key == "channels")
          channels = std::stoi(value);
        else if (key == "ranks")
          ranks = std::stoi(value);
        else if (key == "bankgroups")
          bankgroups = std::stoi(value);
        else if (key == "banks_per_group")
          banks_per_group = std::stoi(value);
        else if (key == "rows")
          rows = std::stoi(value);
        else if (key == "address_mapping")
          address_mapping = value;
      }

      // memory addresses are byte addressable, but each request comes with
      // multiple bytes because of bus width, and burst length
      request_size_bytes = device_width / 8 * BL;
      shift_bits = LogBase2(request_size_bytes);
      int col_low_bits = LogBase2(BL);
      int actual_col_bits = LogBase2(columns) - col_low_bits;

      // has to strictly follow the order of chan, rank, bg, bank, row, col
      std::map<std::string, int> field_widths;
      field_widths["ch"] = LogBase2(channels);
      field_widths["ra"] = LogBase2(ranks);
      field_widths["bg"] = LogBase2(bankgroups);
      field_widths["ba"] = LogBase2(banks_per_group);
      field_widths["ro"] = LogBase2(rows);
      field_widths["co"] = actual_col_bits;

      // // get address mapping position fields from config
      // // each field must be 2 chars
      std::vector<std::string> fields;
      for (size_t i = 0; i < address_mapping.size(); i += 2)
      {
        std::string token = address_mapping.substr(i, 2);
        fields.push_back(token);
      }

      std::map<std::string, int> field_pos;
      int pos = 0;
      while (!fields.empty())
      {
        auto token = fields.back();
        fields.pop_back();
        if (field_widths.find(token) == field_widths.end())
        {
          std::cerr << "Unrecognized field: " << token << std::endl;
          exit(-1);
        }
        field_pos[token] = pos;
        pos += field_widths[token];
      }

      ch_pos = field_pos.at("ch");
      ra_pos = field_pos.at("ra");
      bg_pos = field_pos.at("bg");
      ba_pos = field_pos.at("ba");
      ro_pos = field_pos.at("ro");
      co_pos = field_pos.at("co");

      ch_mask = (1 << field_widths.at("ch")) - 1;
      ra_mask = (1 << field_widths.at("ra")) - 1;
      bg_mask = (1 << field_widths.at("bg")) - 1;
      ba_mask = (1 << field_widths.at("ba")) - 1;
      ro_mask = (1 << field_widths.at("ro")) - 1;
      co_mask = (1 << field_widths.at("co")) - 1;
    }

    uint64_t Address::ReverseAddressMapping(int channel, int rank, int bankgroup, int bank, int row, int column) const
    {
      uint64_t hex_addr = 0;

      // Insert each field at the correct position
      hex_addr |= (static_cast<uint64_t>(channel) << ch_pos);
      hex_addr |= (static_cast<uint64_t>(rank) << ra_pos);
      hex_addr |= (static_cast<uint64_t>(bankgroup) << bg_pos);
      hex_addr |= (static_cast<uint64_t>(bank) << ba_pos);
      hex_addr |= (static_cast<uint64_t>(row) << ro_pos);
      hex_addr |= (static_cast<uint64_t>(column) << co_pos);

      // Shift back to get the actual hex address (undo earlier >> shift_bits)
      hex_addr <<= shift_bits;

      return hex_addr;
    }

  } // namespace ADDRESS
} // namespace pimony