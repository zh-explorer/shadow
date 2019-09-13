//
// Created by explorer on 9/4/19.
//

#include "ip_lib.h"
#include <string>
#include <regex>

std::string ip4_regex = "^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)a\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$";

bool is_ip(char *ip) {
    std::regex re(ip4_regex);
    bool ret = std::regex_match(ip, re);
    return ret;
}