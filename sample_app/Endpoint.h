//
// Created by Lewis weaver on 5/30/2021.
//

#pragma once

#ifndef SAMPLE_APP_ENDPOINT_H
#define SAMPLE_APP_ENDPOINT_H


class Endpoint {
 public:
  static void InitEnvironment();
  static void ShutdownEnvironment();

  void Start();
};


#endif//SAMPLE_APP_ENDPOINT_H
