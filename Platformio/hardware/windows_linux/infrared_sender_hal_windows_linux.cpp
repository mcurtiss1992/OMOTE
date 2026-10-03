#include <stdio.h>
#include <string>
#include <list>

void init_infraredSender_HAL(void) {
}

// IR protocols
enum IRprotocols {
  IR_PROTOCOL_GC = 0,
  IR_PROTOCOL_NEC = 1,
  IR_PROTOCOL_SAMSUNG = 2,
  IR_PROTOCOL_SONY = 3,
  IR_PROTOCOL_RC5 = 4,
  IR_PROTOCOL_DENON = 5,
  IR_PROTOCOL_SAMSUNG36 = 6,
  IR_PROTOCOL_EPSON = 7
};

// There is no IR LED in the simulator. Print what the remote would send, so key presses and scene sequences can be followed.
void sendIRcode_HAL(int protocol, std::list<std::string> commandPayloads, std::string additionalPayload) {
  static const char* names[] = {"Global Cache", "NEC", "Samsung", "Sony", "RC5", "Denon", "Samsung36", "Epson"};
  const char* name = (protocol >= 0 && protocol <= IR_PROTOCOL_EPSON) ? names[protocol] : "unknown protocol";
  std::string data = (additionalPayload != "") ? additionalPayload : (commandPayloads.empty() ? "" : commandPayloads.front());
  printf("IR: would send %s %s\r\n", name, data.c_str());
}
