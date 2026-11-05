#include "../src/FirmwareProgram.hpp"
#include <fstream>
#include <vector>
#include <iostream>
int main(int argc,char **argv) {
 if(argc<2)return 2;
 for(int i=1;i<argc;++i) {
  std::ifstream input(argv[i],std::ios::binary);
  if(!input)return 2;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
  realtek_next::FirmwareProgram program;
  auto status=program.parse(data.data(),data.size());
  std::cout<<argv[i]<<": status="<<unsigned(status)<<", instructions="<<program.instructionCount()<<"\n";
  if(status!=realtek_next::AccessStatus::Ok)return 1;
 }
}
