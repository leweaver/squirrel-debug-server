//
// Created by Lewis weaver on 5/30/2021.
//

#pragma once

#ifndef LISTENER_CONFIG_H
#define LISTENER_CONFIG_H

#include <string>

struct ListenerConfig {
  uint16_t port = 8000U;
  std::string hostName = "localhost";
};

#endif