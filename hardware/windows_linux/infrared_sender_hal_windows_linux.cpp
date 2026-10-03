#include <stdio.h>
#include <string>
#include <list>

void init_infraredSender_HAL(void) {
}

// There is no IR LED in the simulator. Print what the remote would send, so key presses and scene sequences can be followed.
void sendIRcode_HAL(int protocol, std::list<std::string> commandPayloads, std::string additionalPayload) {
  std::string data = (additionalPayload != "") ? additionalPayload : (commandPayloads.empty() ? "" : commandPayloads.front());
  printf("IR: would send protocol %d: %s\r\n", protocol, data.c_str());
}
