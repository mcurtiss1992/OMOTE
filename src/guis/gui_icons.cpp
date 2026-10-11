#include <string>
#include <algorithm>
#include <ctype.h>
#include "guis/gui_icons.h"

const lv_img_dsc_t* icon_forName(const char* name) {
  std::string n = name;
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c){return tolower(c);});
  auto has = [&n](const char* part) {return n.find(part) != std::string::npos;};

  if (has("apple"))                                     {return &icon_apple;}
  if (has("htpc") || has("linux"))                      {return &icon_linux;}
  if (has("ps5") || has("ps4") || has("playstation"))   {return &icon_playstation;}
  if (has("jellyfin"))                                  {return &icon_jellyfin;}
  if (has("spotify"))                                   {return &icon_spotify;}
  if (has("prime") || has("amazon"))                    {return &icon_primevideo;}
  if (has("plex"))                                      {return &icon_plex;}
  return NULL;
}
