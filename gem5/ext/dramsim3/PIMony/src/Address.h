namespace pimony
{
  namespace ADDRESS
  {
    class Address
    {
    public:
      Address(std::string config_file);
      uint64_t ReverseAddressMapping(int channel, int rank, int bankgroup, int bank, int row, int column) const;

      int columns;
      int device_width;
      int BL;
      int channels;
      int ranks;
      int bankgroups;
      int banks_per_group;
      int rows;
      int shift_bits;
      int ch_pos, ra_pos, bg_pos, ba_pos, ro_pos, co_pos;
      uint64_t ch_mask, ra_mask, bg_mask, ba_mask, ro_mask, co_mask;

    private:
      std::string address_mapping;

      int request_size_bytes;
    };
  } // namespace ADDRESS
} // namespace pimony