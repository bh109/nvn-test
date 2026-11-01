#include "nvn/online_settings.h"
#include <cassert>
int main() {
    std::string value;
    for(const char *text:{"127.0.0.1:39421","192.0.2.1:39421 asia","smash.recomps.wtf:39421","off"}) {
        assert(nvn_online::server_line(text,value));assert(value==text);
    }
    for(const char *text:{"", "off extra", "host", ":39421", "host:0", "host:65536",
        "host:99999999999999999999999", "host:39421 other extra", "host:39421 code;run",
        "host&run:39421", "http://host:39421", "[::1]:39421"}) {
        assert(!nvn_online::server_line(text,value));
    }
    assert(nvn_online::server_line("  localhost:39421  asia  ",value));
    assert(value=="localhost:39421 asia");
    assert(nvn_online::player_name("Player 1"));
    assert(!nvn_online::player_name(""));assert(!nvn_online::player_name("   "));
    assert(!nvn_online::player_name("Player\n2"));
    assert(!nvn_online::player_name(std::string(33,'x')));
}
