#include <stdio.h>
#include <thread>
#include <vector>

#include "avkid.hpp"

using namespace avkid;

int main(int argc, char **argv)
{
  std::string in = argv[1];
  std::string out_0 = argv[2];
  std::string out_1 = argv[3];
  std::string out_2 = argv[4];
  int duration = atoi(argv[5]);

  HelpOP::global_init_ffmpeg();
  auto input = Input::create();
  auto decode = Decode::create(true);
  auto encode_0 = Encode::create(true);
  auto encode_1 = Encode::create(true);
  auto output_0 = Output::create(true);
  auto output_1 = Output::create(true);
  auto output_2 = Output::create(true);

  auto fbc = std::make_shared<FrameBroadcast>();
  auto pbc = std::make_shared<PacketBroadcast>();

  // 0.
  // combine(input, decode,encode_0, output_0);

  // combine(input, decode, combine(fbc, combine(encode_0, output_0), combine(encode_1, combine(pbc, output_1, output_2))));


  // // 1.
  // combine(input, decode, fbc);
  // combine(fbc, combine(encode_0, output_0), encode_1);
  // combine(encode_1, combine(pbc, output_1, output_2));

  // 2.
  combine(input, decode);
  combine(decode, fbc);
  fbc->add_data_handler(encode_0);
  fbc->add_data_handler(encode_1);
  combine(encode_0, output_0);
  combine(encode_1, pbc);
  pbc->add_data_handler(output_1);
  pbc->add_data_handler(output_2);

  input->open(in);
  decode->open(input->in_fmt_ctx());
  encode_0->open(input->in_fmt_ctx());
  output_0->open(out_0, input->in_fmt_ctx());
  encode_1->open(input->in_fmt_ctx());
  output_1->open(out_1, input->in_fmt_ctx());
  output_2->open(out_2, input->in_fmt_ctx());

  std::thread th([&]() { input->read(duration * 1000); });
  th.join();

  return 1;
}
