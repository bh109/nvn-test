#pragma once
#include <string>
#include <sstream>
#include <cctype>

namespace nvn_online {
// Settings are data, never shell arguments. Require an explicit port so a
// custom Online server cannot accidentally select the Local Wireless port.
inline bool server_line(const std::string &text, std::string &normalized) {
    std::istringstream input(text);
    std::string endpoint, code, extra;
    if (!(input >> endpoint) || endpoint.size()>255) return false;
    input >> code;
    if (input >> extra) return false;
    if (endpoint == "off") { if (!code.empty()) return false; normalized="off"; return true; }
    auto colon=endpoint.find(':');
    if (colon==std::string::npos || colon==0 || colon>253) return false;
    for (size_t i=0;i<colon;i++) {
        unsigned char ch=endpoint[i];
        if (!std::isalnum(ch) && ch!='.' && ch!='-') return false;
    }
    unsigned port=0;
    if (colon+1==endpoint.size()) return false;
    for (size_t i=colon+1;i<endpoint.size();i++) {
        unsigned char ch=endpoint[i];
        if (ch<'0' || ch>'9' || port>6553) return false;
        port=port*10+ch-'0';
        if (port>65535) return false;
    }
    if (!port || code.size()>32) return false;
    for (unsigned char ch:code) if (!std::isalnum(ch) && ch!='_' && ch!='-') return false;
    normalized=endpoint+(code.empty()?"":" "+code);
    return true;
}
inline bool player_name(const std::string &text) {
    if (text.empty() || text.size()>32) return false;
    bool visible=false;
    for (unsigned char ch:text) { if(ch<32 || ch>126) return false; if(ch!=' ') visible=true; }
    return visible;
}
}
